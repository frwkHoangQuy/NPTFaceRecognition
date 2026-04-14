#include <jni.h>
#include <string>
#include <vector>
#include <numeric>
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <dlfcn.h>
#include <sys/system_properties.h>
#include <cstdio>
#include <cstring>
#include "include/rknn_api.h"

#define LOG_TAG "NPUFaceRecognition"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// =============================================================
extern "C" JNIEXPORT jstring JNICALL
Java_com_example_npufacerecognition_MainActivity_stringFromJNI(JNIEnv *env, jobject) {
    return env->NewStringUTF("Hello from C++");
}

// =============================================================
extern "C" JNIEXPORT jint JNICALL
Java_com_example_npufacerecognition_MainActivity_sumFromCPP(JNIEnv *env, jobject, jint a, jint b) {
    std::vector<int> v(100);
    std::iota(v.begin(), v.end(), 1);
    return (jint) std::accumulate(v.begin(), v.end(), 0);
}

// =============================================================
extern "C" JNIEXPORT jstring JNICALL
Java_com_example_npufacerecognition_MainActivity_process(JNIEnv *env, jobject instance) {
    jclass cls = env->GetObjectClass(instance);
    jmethodID mid = env->GetMethodID(cls, "processInJava", "()Ljava/lang/String;");
    if (mid == nullptr) return env->NewStringUTF("");
    auto jResult = (jstring) env->CallObjectMethod(instance, mid);
    const char *chars = env->GetStringUTFChars(jResult, nullptr);
    std::string msg = "Result from Java=> " + std::string(chars);
    env->ReleaseStringUTFChars(jResult, chars);
    return env->NewStringUTF(msg.c_str());
}

// =============================================================
// getNPUInfo: Kiểm tra toàn bộ thông tin NPU
//   PHẦN 1 — Load model thật từ assets → rknn_init → ctx hợp lệ
//   PHẦN 2 — Query SDK version (API + Driver)
//   PHẦN 3 — Query Input/Output tensor info
//   PHẦN 4 — Query Memory size (weight + internal)
//   PHẦN 5 — Devfreq: tần số & load thực tế (đường dẫn OEM mới)
//   PHẦN 6 — System properties: platform, hardware, model
// =============================================================
extern "C" JNIEXPORT jstring JNICALL
Java_com_example_npufacerecognition_MainActivity_getNPUInfo(JNIEnv *env, jobject thiz,
                                                             jobject assetManager) {
    std::string info;

    // ----------------------------------------------------------
    // HELPER: đọc 1 dòng từ file sysfs, trim newline
    // ----------------------------------------------------------
    auto read_sysfs = [](const char *path) -> std::string {
        FILE *f = fopen(path, "r");
        if (!f) return "N/A";
        char buf[256] = {0};
        fgets(buf, sizeof(buf), f);
        fclose(f);
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
        return std::string(buf);
    };

    // HELPER: Hz → MHz string
    auto hz_to_mhz = [](const std::string &hz) -> std::string {
        try { return std::to_string(std::stol(hz) / 1000000) + " MHz"; }
        catch (...) { return hz; }
    };

    // ==========================================================
    // PHẦN 1: LOAD MODEL TỪ ASSETS → rknn_init → ctx hợp lệ
    // ==========================================================
    AAssetManager *mgr   = AAssetManager_fromJava(env, assetManager);
    AAsset       *asset  = AAssetManager_open(mgr, "RetinaFace_mobile320.rknn", AASSET_MODE_BUFFER);

    rknn_context ctx      = 0;
    int          ret_init = -1;
    bool         ctx_valid = false;

    if (asset != nullptr) {
        off_t model_size = AAsset_getLength(asset);
        std::vector<uint8_t> model_buf(model_size);
        AAsset_read(asset, model_buf.data(), model_size);
        AAsset_close(asset);
        LOGI("[assets] Loaded RetinaFace_mobile320.rknn, size=%ld bytes", (long) model_size);

        ret_init  = rknn_init(&ctx, model_buf.data(), (uint32_t) model_size, 0, nullptr);
        ctx_valid = (ret_init == RKNN_SUCC);
        LOGI("[rknn_init] ret=%d, ctx_valid=%d", ret_init, ctx_valid);
    } else {
        // Fallback: dummy model để xác nhận driver còn sống
        LOGE("[assets] Không tìm thấy RetinaFace_mobile320.rknn — dùng dummy");
        uint8_t dummy[1] = {0};
        ret_init = rknn_init(&ctx, dummy, 1, RKNN_FLAG_COLLECT_MODEL_INFO_ONLY, nullptr);
        LOGI("[rknn_init] fallback dummy ret=%d", ret_init);
    }

    // Báo ret qua Java method Ret(int) để in ra Logcat
    jclass    cls_ma    = env->GetObjectClass(thiz);
    jmethodID retMethod = env->GetMethodID(cls_ma, "Ret", "(I)I");
    if (retMethod != nullptr) {
        jint retFromJava = env->CallIntMethod(thiz, retMethod, (jint) ret_init);
        LOGI("[JNI] Ret() from Java = %d", (int) retFromJava);
    }

    bool driver_alive = (ret_init == RKNN_SUCC) || (ret_init == RKNN_ERR_MODEL_INVALID);

    // ==========================================================
    // PHẦN 2: QUERY SDK VERSION
    // ==========================================================
    std::string sdk_api_version = "N/A";
    std::string sdk_drv_version = "N/A";

    if (ctx_valid) {
        rknn_sdk_version sdk;
        memset(&sdk, 0, sizeof(sdk));
        if (rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &sdk, sizeof(sdk)) == RKNN_SUCC) {
            sdk_api_version = std::string(sdk.api_version);
            sdk_drv_version = std::string(sdk.drv_version);
            LOGI("[SDK] API=%s | Drv=%s", sdk.api_version, sdk.drv_version);
        }
    }

    // ==========================================================
    // PHẦN 3: QUERY INPUT / OUTPUT TENSOR INFO
    // ==========================================================
    std::string tensor_info;

    if (ctx_valid) {
        rknn_input_output_num io_num;
        memset(&io_num, 0, sizeof(io_num));
        if (rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num)) == RKNN_SUCC) {
            tensor_info += "  Inputs  : " + std::to_string(io_num.n_input) + "\n";
            for (uint32_t i = 0; i < io_num.n_input; i++) {
                rknn_tensor_attr attr;
                memset(&attr, 0, sizeof(attr));
                attr.index = i;
                if (rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &attr, sizeof(attr)) == RKNN_SUCC) {
                    tensor_info += "    [" + std::to_string(i) + "] " + std::string(attr.name) + " [";
                    for (uint32_t d = 0; d < attr.n_dims; d++) {
                        if (d) tensor_info += "x";
                        tensor_info += std::to_string(attr.dims[d]);
                    }
                    tensor_info += "] fmt=" + std::to_string(attr.fmt)
                                 + " type=" + std::to_string(attr.type) + "\n";
                }
            }
            tensor_info += "  Outputs : " + std::to_string(io_num.n_output) + "\n";
            for (uint32_t i = 0; i < io_num.n_output; i++) {
                rknn_tensor_attr attr;
                memset(&attr, 0, sizeof(attr));
                attr.index = i;
                if (rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &attr, sizeof(attr)) == RKNN_SUCC) {
                    tensor_info += "    [" + std::to_string(i) + "] " + std::string(attr.name) + " [";
                    for (uint32_t d = 0; d < attr.n_dims; d++) {
                        if (d) tensor_info += "x";
                        tensor_info += std::to_string(attr.dims[d]);
                    }
                    tensor_info += "] type=" + std::to_string(attr.type) + "\n";
                }
            }
        }
    } else {
        tensor_info = "  (cần ctx hợp lệ)\n";
    }

    // ==========================================================
    // PHẦN 4: QUERY MEMORY SIZE
    // ==========================================================
    std::string mem_info;
    if (ctx_valid) {
        rknn_mem_size mem_size;
        memset(&mem_size, 0, sizeof(mem_size));
        if (rknn_query(ctx, RKNN_QUERY_MEM_SIZE, &mem_size, sizeof(mem_size)) == RKNN_SUCC) {
            mem_info += "  Weight  : " + std::to_string(mem_size.total_weight_size   / 1024) + " KB\n";
            mem_info += "  Internal: " + std::to_string(mem_size.total_internal_size / 1024) + " KB\n";
        } else {
            mem_info = "  N/A\n";
        }
        rknn_destroy(ctx);
    } else {
        mem_info = "  (cần ctx hợp lệ)\n";
    }

    // ==========================================================
    // PHẦN 5: DEVFREQ — tần số & load NPU thực tế
    // OEM Telpo đã di chuyển NPU interface sang devfreq framework:
    //   /sys/class/devfreq/fde40000.npu/  (địa chỉ vật lý RK3588)
    // Thay vì đường dẫn cũ /sys/kernel/debug/rknpu (bị SELinux chặn)
    // load=100% = OEM Hard-coded Performance Mode (preload camera AI)
    // ==========================================================
    const char *devfreq_base  = nullptr;
    const char *candidates[]  = {
        "/sys/class/devfreq/fde40000.npu",  // RK3588
        "/sys/class/devfreq/fdab0000.npu",  // RK3588s
        "/sys/class/devfreq/ff400000.npu",  // RK3566/3568
        nullptr
    };
    for (int i = 0; candidates[i] != nullptr; i++) {
        std::string probe = std::string(candidates[i]) + "/cur_freq";
        FILE *t = fopen(probe.c_str(), "r");
        if (t) { fclose(t); devfreq_base = candidates[i]; break; }
    }

    std::string npu_cur_freq = "N/A", npu_min_freq = "N/A", npu_max_freq = "N/A";
    std::string npu_governor = "N/A", npu_load     = "N/A", npu_available = "N/A";
    bool devfreq_found = (devfreq_base != nullptr);

    if (devfreq_found) {
        driver_alive = true;
        auto p = [&](const char *sub) { return std::string(devfreq_base) + "/" + sub; };
        npu_cur_freq  = read_sysfs(p("cur_freq").c_str());
        npu_min_freq  = read_sysfs(p("min_freq").c_str());
        npu_max_freq  = read_sysfs(p("max_freq").c_str());
        npu_governor  = read_sysfs(p("governor").c_str());
        // load format: "100@900000000Hz" → chỉ lấy phần trước "@"
        std::string load_raw = read_sysfs(p("load").c_str());
        size_t at_pos = load_raw.find('@');
        npu_load      = (at_pos != std::string::npos) ? load_raw.substr(0, at_pos) : load_raw;
        npu_available = read_sysfs(p("available_frequencies").c_str());
        if (npu_load == "100")
            LOGI("[devfreq] load=100%% — OEM Hard-coded Performance Mode");
        LOGI("[devfreq] cur=%s Hz | load=%s%%", npu_cur_freq.c_str(), npu_load.c_str());
    } else {
        // Fallback /proc/driver/rknpu (firmware cũ hơn)
        FILE *fp = fopen("/proc/driver/rknpu", "r");
        if (fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp)) npu_load += std::string(line);
            fclose(fp);
            driver_alive = true;
            LOGI("[proc] /proc/driver/rknpu found");
        } else {
            LOGE("[devfreq] Không tìm thấy NPU interface (SELinux?)");
        }
    }

    // ==========================================================
    // PHẦN 6: SYSTEM PROPERTIES
    // ==========================================================
    char hw_platform[PROP_VALUE_MAX] = {0};
    char hw_hardware[PROP_VALUE_MAX] = {0};
    char soc_model[PROP_VALUE_MAX]   = {0};
    char build_ver[PROP_VALUE_MAX]   = {0};
    __system_property_get("ro.board.platform",        hw_platform);
    __system_property_get("ro.hardware",              hw_hardware);
    __system_property_get("ro.product.model",         soc_model);
    __system_property_get("ro.build.version.release", build_ver);

    // ==========================================================
    // PHẦN 7: ĐÓNG GÓI KẾT QUẢ
    // ==========================================================
    info += "========== NPU INFO ==========\n";
    info += "NPU Status    : ";
    info += driver_alive ? "Online\n" : "Offline\n";

    info += "------------------------------\n";
    info += "[SDK / Driver]\n";
    info += "  API Version : " + sdk_api_version + "\n";
    info += "  Drv Version : " + sdk_drv_version + "\n";
    info += "  rknn_init   : ret=" + std::to_string(ret_init);
    if      (ret_init == RKNN_SUCC)                       info += " (SUCC)\n";
    else if (ret_init == RKNN_ERR_MODEL_INVALID)           info += " (MODEL_INVALID — OEM service chiếm NPU)\n";
    else if (ret_init == RKNN_ERR_DEVICE_UNAVAILABLE)      info += " (DEVICE_UNAVAILABLE)\n";
    else if (ret_init == RKNN_ERR_TARGET_PLATFORM_UNMATCH) info += " (PLATFORM_UNMATCH)\n";
    else if (ret_init == RKNN_ERR_DEVICE_UNMATCH)          info += " (DEVICE_UNMATCH — cần update SDK)\n";
    else                                                   info += " (code=" + std::to_string(ret_init) + ")\n";

    info += "------------------------------\n";
    info += "[Model: RetinaFace_mobile320.rknn]\n";
    info += tensor_info;

    info += "------------------------------\n";
    info += "[Memory]\n";
    info += mem_info;

    info += "------------------------------\n";
    info += "[Devfreq: " + (devfreq_found ? std::string(devfreq_base) : "Not found") + "]\n";
    if (devfreq_found) {
        info += "  Cur Freq    : " + hz_to_mhz(npu_cur_freq) + "\n";
        info += "  Min Freq    : " + hz_to_mhz(npu_min_freq) + "\n";
        info += "  Max Freq    : " + hz_to_mhz(npu_max_freq) + "\n";
        info += "  Governor    : " + npu_governor + "\n";
        info += "  Load        : " + npu_load + "%";
        if (npu_load == "100") info += " (OEM Performance Mode)";
        info += "\n";
        info += "  Avail Freq  : " + npu_available + "\n";
    } else {
        info += "  Note: SELinux chặn hoặc OEM khác đường dẫn\n";
    }

    info += "------------------------------\n";
    info += "[Device]\n";
    info += "  Platform    : " + std::string(hw_platform) + "\n";
    info += "  Hardware    : " + std::string(hw_hardware)  + "\n";
    info += "  Model       : " + std::string(soc_model)   + "\n";
    info += "  Android     : " + std::string(build_ver)   + "\n";
    info += "==============================";

    return env->NewStringUTF(info.c_str());
}

