#include <jni.h>
#include <string>
#include <vector>
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <sys/system_properties.h>
#include <cstdio>
#include <cstring>
#include "include/rknn_api.h"

#define LOG_TAG "NPUFaceRecognition"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// =============================================================
// GLOBAL: giữ model context thường trú sau khi init
// =============================================================
static rknn_context g_ctx    = 0;
static bool         g_loaded = false;

// =============================================================
// HELPER: đọc 1 dòng từ sysfs, trim newline
// =============================================================
static std::string read_sysfs(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return "N/A";
    char buf[256] = {0};
    fgets(buf, sizeof(buf), f);
    fclose(f);
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
    return {buf};
}

// HELPER: Hz → MHz string
static std::string hz_to_mhz(const std::string &hz) {
    try { return std::to_string(std::stol(hz) / 1000000) + " MHz"; }
    catch (...) { return hz; }
}

// =============================================================
// queryNPUInfo: Kiểm tra trạng thái phần cứng NPU
//   - Devfreq: tần số, load, governor (OEM path)
//   - System properties: platform, hardware, model, android ver
//   Thêm thông tin NPU mới vào đây
// =============================================================
extern "C" JNIEXPORT void JNICALL
Java_com_example_npufacerecognition_MainActivity_queryNPUInfo(JNIEnv*, jobject) {

    // --- Devfreq: tìm đường dẫn NPU đúng theo từng chip ---
    const char *candidates[] = {
        "/sys/class/devfreq/fde40000.npu",  // RK3588
        "/sys/class/devfreq/fdab0000.npu",  // RK3588s
        "/sys/class/devfreq/ff400000.npu",  // RK3566/3568
        nullptr
    };
    const char *devfreq_base = nullptr;
    for (int i = 0; candidates[i] != nullptr; i++) {
        std::string probe = std::string(candidates[i]) + "/cur_freq";
        FILE *t = fopen(probe.c_str(), "r");
        if (t) { fclose(t); devfreq_base = candidates[i]; break; }
    }

    // --- System properties ---
    char platform[PROP_VALUE_MAX] = {0};
    char hardware[PROP_VALUE_MAX] = {0};
    char model[PROP_VALUE_MAX]    = {0};
    char android[PROP_VALUE_MAX]  = {0};
    __system_property_get("ro.board.platform",        platform);
    __system_property_get("ro.hardware",              hardware);
    __system_property_get("ro.product.model",         model);
    __system_property_get("ro.build.version.release", android);

    LOGI("===== NPU Hardware Info =====");
    LOGI("  Platform : %s", platform);
    LOGI("  Hardware : %s", hardware);
    LOGI("  Model    : %s", model);
    LOGI("  Android  : %s", android);

    if (devfreq_base != nullptr) {
        auto p = [&](const char *sub) { return std::string(devfreq_base) + "/" + sub; };
        std::string cur  = read_sysfs(p("cur_freq").c_str());
        std::string mn   = read_sysfs(p("min_freq").c_str());
        std::string mx   = read_sysfs(p("max_freq").c_str());
        std::string gov  = read_sysfs(p("governor").c_str());
        std::string avail= read_sysfs(p("available_frequencies").c_str());
        std::string load_raw = read_sysfs(p("load").c_str());
        size_t at = load_raw.find('@');
        std::string load = (at != std::string::npos) ? load_raw.substr(0, at) : load_raw;

        LOGI("  Devfreq  : %s", devfreq_base);
        LOGI("  Cur Freq : %s", hz_to_mhz(cur).c_str());
        LOGI("  Min Freq : %s", hz_to_mhz(mn).c_str());
        LOGI("  Max Freq : %s", hz_to_mhz(mx).c_str());
        LOGI("  Governor : %s", gov.c_str());
        LOGI("  Load     : %s%%", load.c_str());
        LOGI("  Avail    : %s", avail.c_str());
    } else {
        LOGE("  Devfreq  : not found");
    }
    LOGI("=============================");
}

// =============================================================
// queryModelInfo: Kiểm tra thông tin model RKNN đã load vào NPU
//   - SDK version, Driver version
//   - Input/Output tensor (tên, dims, format, type)
//   - Memory usage (weight, internal)
//   Thêm thông tin model mới vào đây
// =============================================================
extern "C" JNIEXPORT void JNICALL
Java_com_example_npufacerecognition_MainActivity_queryModelInfo(JNIEnv *env, jobject,
                                                                 jobject assetManager,
                                                                 jstring modelFileName) {
    // --- Load model ---
    const char *filename = env->GetStringUTFChars(modelFileName, nullptr);
    AAssetManager *mgr   = AAssetManager_fromJava(env, assetManager);
    AAsset        *asset = AAssetManager_open(mgr, filename, AASSET_MODE_BUFFER);
    env->ReleaseStringUTFChars(modelFileName, filename);

    if (!asset) {
        LOGE("[queryModelInfo] File not found: %s", filename);
        return;
    }

    off_t size = AAsset_getLength(asset);
    std::vector<uint8_t> buf(size);
    AAsset_read(asset, buf.data(), size);
    AAsset_close(asset);

    rknn_context ctx = 0;
    int ret = rknn_init(&ctx, buf.data(), (uint32_t) size, 0, nullptr);
    if (ret != RKNN_SUCC) {
        LOGE("[queryModelInfo] rknn_init failed: %d", ret);
        return;
    }

    LOGI("===== Model Info =====");

    // --- SDK version ---
    rknn_sdk_version sdk;
    memset(&sdk, 0, sizeof(sdk));
    if (rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &sdk, sizeof(sdk)) == RKNN_SUCC) {
        LOGI("  SDK API  : %s", sdk.api_version);
        LOGI("  SDK Drv  : %s", sdk.drv_version);
    }

    // --- Input/Output tensor ---
    rknn_input_output_num io_num;
    memset(&io_num, 0, sizeof(io_num));
    if (rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num)) == RKNN_SUCC) {
        LOGI("  Inputs   : %d", io_num.n_input);
        for (uint32_t i = 0; i < io_num.n_input; i++) {
            rknn_tensor_attr attr;
            memset(&attr, 0, sizeof(attr));
            attr.index = i;
            if (rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &attr, sizeof(attr)) == RKNN_SUCC) {
                std::string dims;
                for (uint32_t d = 0; d < attr.n_dims; d++) {
                    if (d) dims += "x";
                    dims += std::to_string(attr.dims[d]);
                }
                LOGI("    [%d] %-20s [%s] fmt=%d type=%d",
                     i, attr.name, dims.c_str(), attr.fmt, attr.type);
            }
        }
        LOGI("  Outputs  : %d", io_num.n_output);
        for (uint32_t i = 0; i < io_num.n_output; i++) {
            rknn_tensor_attr attr;
            memset(&attr, 0, sizeof(attr));
            attr.index = i;
            if (rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &attr, sizeof(attr)) == RKNN_SUCC) {
                std::string dims;
                for (uint32_t d = 0; d < attr.n_dims; d++) {
                    if (d) dims += "x";
                    dims += std::to_string(attr.dims[d]);
                }
                LOGI("    [%d] %-20s [%s] type=%d",
                     i, attr.name, dims.c_str(), attr.type);
            }
        }
    }

    // --- Memory ---
    rknn_mem_size mem;
    memset(&mem, 0, sizeof(mem));
    if (rknn_query(ctx, RKNN_QUERY_MEM_SIZE, &mem, sizeof(mem)) == RKNN_SUCC) {
        LOGI("  Weight   : %u KB", mem.total_weight_size   / 1024);
        LOGI("  Internal : %u KB", mem.total_internal_size / 1024);
    }

    LOGI("======================");
    rknn_destroy(ctx);
}

// =============================================================
// runRetinaFace: nhận YUV planes trực tiếp từ CameraX (zero-copy)
// =============================================================
extern "C" JNIEXPORT void JNICALL
Java_com_example_npufacerecognition_MainActivity_runRetinaFace(
        JNIEnv *env, jobject,
        jobject y_buffer, jobject u_buffer, jobject v_buffer,
        jint width, jint height,
        jint y_row_stride, jint uv_row_stride,
        jint uv_pixel_stride)
{
    auto* y_ptr = (uint8_t*) env->GetDirectBufferAddress(y_buffer);
    auto* u_ptr = (uint8_t*) env->GetDirectBufferAddress(u_buffer);
    auto* v_ptr = (uint8_t*) env->GetDirectBufferAddress(v_buffer);

    if (!y_ptr || !u_ptr || !v_ptr) {
        LOGE("[runRetinaFace] GetDirectBufferAddress failed");
        return;
    }

    static int frame_count = 0;
    if (frame_count % 30 == 0) {
        LOGI("[runRetinaFace] frame=%d | %dx%d | y_stride=%d uv_stride=%d px_stride=%d",
             frame_count, width, height, y_row_stride, uv_row_stride, uv_pixel_stride);
    }
    frame_count++;

    // TODO Bước 5: yuv420_to_bgr888() + rknn_run
}
