#include <jni.h>
#include <string>
#include <vector>
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <sys/system_properties.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include "include/rknn_api.h"

#define LOG_TAG "NPUFaceRecognition"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// =============================================================
// GLOBAL: giữ model context thường trú sau khi init
// =============================================================
static rknn_context g_ctx = 0;
static bool g_loaded = false;

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
Java_com_example_npufacerecognition_MainActivity_queryNPUInfo(JNIEnv *, jobject) {

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
        if (t) {
            fclose(t);
            devfreq_base = candidates[i];
            break;
        }
    }

    // --- System properties ---
    char platform[PROP_VALUE_MAX] = {0};
    char hardware[PROP_VALUE_MAX] = {0};
    char model[PROP_VALUE_MAX] = {0};
    char android[PROP_VALUE_MAX] = {0};
    __system_property_get("ro.board.platform", platform);
    __system_property_get("ro.hardware", hardware);
    __system_property_get("ro.product.model", model);
    __system_property_get("ro.build.version.release", android);

    LOGI("===== NPU Hardware Info =====");
    LOGI("  Platform : %s", platform);
    LOGI("  Hardware : %s", hardware);
    LOGI("  Model    : %s", model);
    LOGI("  Android  : %s", android);

    if (devfreq_base != nullptr) {
        auto p = [&](const char *sub) { return std::string(devfreq_base) + "/" + sub; };
        std::string cur = read_sysfs(p("cur_freq").c_str());
        std::string mn = read_sysfs(p("min_freq").c_str());
        std::string mx = read_sysfs(p("max_freq").c_str());
        std::string gov = read_sysfs(p("governor").c_str());
        std::string avail = read_sysfs(p("available_frequencies").c_str());
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
    AAssetManager *mgr = AAssetManager_fromJava(env, assetManager);
    AAsset *asset = AAssetManager_open(mgr, filename, AASSET_MODE_BUFFER);
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
        LOGI("  Weight   : %u KB", mem.total_weight_size / 1024);
        LOGI("  Internal : %u KB", mem.total_internal_size / 1024);
    }

    LOGI("======================");
    rknn_destroy(ctx);
}

// =============================================================
// HELPER: YUV420_888 → BGR float[320x320x3]
//   • Nearest-neighbor resize
//   • BT.601 YUV → RGB
//   • Ghi thẳng thứ tự B, G, R (không swap)
//   • Trừ mean: B-104, G-117, R-123
// =============================================================
static void yuv420_to_bgr_nchw(
        const uint8_t *y_ptr, const uint8_t *u_ptr, const uint8_t *v_ptr,
        int src_w, int src_h,
        int y_row_stride, int uv_row_stride, int uv_pixel_stride,
        uint8_t *out,
        int dst_w, int dst_h) {

    const int plane = dst_w * dst_h;

    uint8_t *B_plane = out;
    uint8_t *G_plane = out + plane;
    uint8_t *R_plane = out + 2 * plane;

    // --- Letterbox: tính scale giữ tỉ lệ ---
    float scale = std::min((float) dst_w / src_w, (float) dst_h / src_h);

    int scaled_w = (int) (src_w * scale);  // chiều rộng sau scale
    int scaled_h = (int) (src_h * scale);  // chiều cao sau scale

    // Padding để căn giữa
    int pad_x = (dst_w - scaled_w) / 2;  // lề trái
    int pad_y = (dst_h - scaled_h) / 2;  // lề trên

    // Pad color: BGR = (114, 114, 114) — chuẩn YOLOv5/RetinaFace
    const uint8_t PAD_B = 114, PAD_G = 114, PAD_R = 114;

    // Pre-compute LUT: dst pixel → src pixel (chỉ cho vùng ảnh thực)
    // -1 = vùng padding
    static int lut_col[320], lut_row[320];
    for (int d = 0; d < dst_w; d++) {
        int s = (int) ((d - pad_x) / scale);
        lut_col[d] = (d >= pad_x && d < pad_x + scaled_w) ? s : -1;
    }
    for (int d = 0; d < dst_h; d++) {
        int s = (int) ((d - pad_y) / scale);
        lut_row[d] = (d >= pad_y && d < pad_y + scaled_h) ? s : -1;
    }

    // Fixed-point Q8 BT.601 Full Range
    const int R_V = 351, G_U = 86, G_V = 179, B_U = 443;

    for (int dr = 0; dr < dst_h; dr++) {
        const int sr = lut_row[dr];
        const int row_offset = dr * dst_w;

        // Chuẩn bị row pointer (chỉ dùng khi sr != -1)
        const uint8_t *y_row = (sr >= 0) ? (y_ptr + sr * y_row_stride) : nullptr;
        const uint8_t *u_row = (sr >= 0) ? (u_ptr + (sr / 2) * uv_row_stride) : nullptr;
        const uint8_t *v_row = (sr >= 0) ? (v_ptr + (sr / 2) * uv_row_stride) : nullptr;

        for (int dc = 0; dc < dst_w; dc++) {
            const int sc = lut_col[dc];
            const int idx = row_offset + dc;

            // Vùng padding → fill màu constant
            if (sr < 0 || sc < 0) {
                B_plane[idx] = PAD_B;
                G_plane[idx] = PAD_G;
                R_plane[idx] = PAD_R;
                continue;
            }

            int Y = (int) y_row[sc];
            int U = (int) u_row[(sc / 2) * uv_pixel_stride] - 128;
            int V = (int) v_row[(sc / 2) * uv_pixel_stride] - 128;

            int R = Y + ((R_V * V) >> 8);
            int G = Y - ((G_U * U) >> 8) - ((G_V * V) >> 8);
            int B = Y + ((B_U * U) >> 8);

            R = (unsigned) R <= 255 ? R : (R < 0 ? 0 : 255);
            G = (unsigned) G <= 255 ? G : (G < 0 ? 0 : 255);
            B = (unsigned) B <= 255 ? B : (B < 0 ? 0 : 255);

            B_plane[idx] = (uint8_t) B;
            G_plane[idx] = (uint8_t) G;
            R_plane[idx] = (uint8_t) R;
        }
    }

    // Log letterbox params (debug)
    LOGI("[letterbox] src=%dx%d scale=%.4f scaled=%dx%d pad=(%d,%d)",
         src_w, src_h, scale, scaled_w, scaled_h, pad_x, pad_y);
}


// =============================================================
// ANCHOR: cấu trúc và bảng anchor cố định cho 320×320
// =============================================================
struct Anchor {
    float cx, cy, sx, sy; // tọa độ tâm + kích thước, normalized [0,1]
};

// =============================================================
// FaceBox: kết quả 1 khuôn mặt sau decode + NMS
//   x1, y1, x2, y2 : tọa độ bbox tính bằng pixel trong ảnh gốc
//   score           : confidence [0, 1]
//   lm[10]          : 5 landmarks (x0,y0, x1,y1, ..., x4,y4) pixel
// =============================================================
struct FaceBox {
    float x1, y1, x2, y2;  // top-left, bottom-right (pixel coords)
    float score;            // face confidence
    float lm[10];           // 5 landmarks × (x, y)
};

// =============================================================
//   SOFTMAX2: tính score face từ 2 logits (bg, fg)
//   Dùng max-shift để tránh overflow float
// =============================================================
static inline float softmax2(float bg, float fg) {
    float m  = std::max(bg, fg);
    float e0 = expf(bg - m);
    float e1 = expf(fg - m);
    return e1 / (e0 + e1);  // xác suất class "face"
}

// =============================================================
//   IOU: Intersection over Union của 2 FaceBox
//   Cả 2 box dùng định dạng (x1,y1,x2,y2) pixel
// =============================================================
static float iou(const FaceBox &a, const FaceBox &b) {
    float ix1 = std::max(a.x1, b.x1);
    float iy1 = std::max(a.y1, b.y1);
    float ix2 = std::min(a.x2, b.x2);
    float iy2 = std::min(a.y2, b.y2);

    float iw    = std::max(0.f, ix2 - ix1);
    float ih    = std::max(0.f, iy2 - iy1);
    float inter = iw * ih;

    float area_a = (a.x2 - a.x1) * (a.y2 - a.y1);
    float area_b = (b.x2 - b.x1) * (b.y2 - b.y1);

    return inter / (area_a + area_b - inter + 1e-6f);
}




static std::vector<Anchor> g_anchors;  // global, init 1 lần

static void generateAnchors320() {
    if (!g_anchors.empty()) return;

    const int IMAGE_SIZE = 320;
    const int steps[] = {8, 16, 32};
    const int min_sizes[][2] = {{16,  32},
                                {64,  128},
                                {256, 512}};
    // feature_maps: 320/8=40, 320/16=20, 320/32=10

    g_anchors.reserve(4200); // 40*40*2 + 20*20*2 + 10*10*2

    for (int k = 0; k < 3; k++) {
        int step = steps[k];
        int fmap = IMAGE_SIZE / step; // 40, 20, 10

        for (int i = 0; i < fmap; i++) {         // row
            for (int j = 0; j < fmap; j++) {     // col
                for (int m = 0; m < 2; m++) {    // 2 min_sizes mỗi tầng
                    float min_size = (float) min_sizes[k][m];

                    Anchor a;
                    a.sx = min_size / IMAGE_SIZE;
                    a.sy = min_size / IMAGE_SIZE;
                    a.cx = (j + 0.5f) * step / IMAGE_SIZE;
                    a.cy = (i + 0.5f) * step / IMAGE_SIZE;

                    g_anchors.push_back(a);
                }
            }
        }
    }

    LOGI("[generateAnchors320] total anchors = %zu (expected 4200)", g_anchors.size());

    // Sanity check: in 4 anchor đầu tiên
    for (int i = 0; i < 4 && i < (int) g_anchors.size(); i++) {
        LOGI("  anchor[%d] cx=%.4f cy=%.4f sx=%.4f sy=%.4f",
             i, g_anchors[i].cx, g_anchors[i].cy,
             g_anchors[i].sx, g_anchors[i].sy);
    }
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
        jint uv_pixel_stride) {
    auto *y_ptr = (uint8_t *) env->GetDirectBufferAddress(y_buffer);
    auto *u_ptr = (uint8_t *) env->GetDirectBufferAddress(u_buffer);
    auto *v_ptr = (uint8_t *) env->GetDirectBufferAddress(v_buffer);

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

    /// Chuẩn bị buffer đầu vào đã resize + convert sẵn cho model (320x320 BGR float)


    // Buffer uint8 NCHW: 3 planes × 320 × 320
    static uint8_t bgr_nchw[3 * 320 * 320];

    yuv420_to_bgr_nchw(
            y_ptr, u_ptr, v_ptr,
            width, height,
            y_row_stride, uv_row_stride, uv_pixel_stride,
            bgr_nchw,
            320, 320);

    // Log kiểm tra: pixel (0,0) của từng plane B/G/R
    if (frame_count % 30 == 1) {
        LOGI("[runRetinaFace] NCHW bgr[0] B=%d G=%d R=%d",
             bgr_nchw[0],               // B plane pixel 0
             bgr_nchw[320 * 320],       // G plane pixel 0
             bgr_nchw[2 * 320 * 320]);  // R plane pixel 0
    }

    if (!g_loaded) {
        LOGE("[runRetinaFace] Model not loaded, skip");
        return;
    }

    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));

    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;          // uint8, NPU tự dequantize + mean
    inputs[0].size = 3 * 320 * 320 * sizeof(uint8_t);
    inputs[0].fmt = RKNN_TENSOR_NCHW;           // Planar — khớp với PyTorch model
    inputs[0].buf = bgr_nchw;
    inputs[0].pass_through = 0;                          // NPU xử lý normalize/mean bằng HW

    int ret = rknn_inputs_set(g_ctx, 1, inputs);
    if (ret != RKNN_SUCC) {
        LOGE("[runRetinaFace] rknn_inputs_set failed: %d", ret);
        return;
    }

    /// Chạy inference

    ret = rknn_run(g_ctx, nullptr);
    if (ret != RKNN_SUCC) {
        LOGE("[runRetinaFace] rknn_run failed: %d", ret);
        return;
    }

    if (frame_count % 30 == 1) {
        LOGI("[runRetinaFace] rknn_run OK — frame=%d", frame_count);
    }

    /// Lấy output tensors

    const int NUM_OUTPUTS = 3;
    rknn_output outputs[NUM_OUTPUTS];
    memset(outputs, 0, sizeof(outputs));

    // want_float = 1: RKNN tự dequantize về float32 cho mình
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        outputs[i].want_float = 1;
        outputs[i].index = i;
    }

    ret = rknn_outputs_get(g_ctx, NUM_OUTPUTS, outputs, nullptr);
    if (ret != RKNN_SUCC) {
        LOGE("[runRetinaFace] rknn_outputs_get failed: %d", ret);
        return;
    }
    // Log kiểm tra 4 giá trị đầu tiên của mỗi output
    if (frame_count % 30 == 1) {
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            auto *ptr = (float *) outputs[i].buf;
            LOGI("[runRetinaFace] output[%d] size=%d  [0]=%.4f [1]=%.4f [2]=%.4f [3]=%.4f",
                 i, outputs[i].size,
                 ptr[0], ptr[1], ptr[2], ptr[3]);
        }
    }

    rknn_outputs_release(g_ctx, NUM_OUTPUTS, outputs);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_npufacerecognition_MainActivity_initRetinaFace(
        JNIEnv *env, jobject,
        jobject assetManager,
        jstring modelFileName) {
    // Guard: không load lại nếu đã load rồi
    if (g_loaded) {
        LOGI("[initRetinaFace] Already loaded, skip");
        return;
    }

    // Mở file .rknn từ assets
    AAssetManager *mgr = AAssetManager_fromJava(env, assetManager);
    const char *filename = env->GetStringUTFChars(modelFileName, nullptr);
    AAsset *asset = AAssetManager_open(mgr, filename, AASSET_MODE_BUFFER);
    env->ReleaseStringUTFChars(modelFileName, filename);

    if (!asset) {
        LOGE("[initRetinaFace] Cannot open: %s", filename);
        return;
    }

    // Đọc toàn bộ bytes vào buffer
    off_t size = AAsset_getLength(asset);
    std::vector<uint8_t> model_buf(size);
    AAsset_read(asset, model_buf.data(), size);
    AAsset_close(asset);
    LOGI("[initRetinaFace] Read %ld bytes from assets", (long) size);

    // Nạp model vào NPU
    int ret = rknn_init(&g_ctx, model_buf.data(), (uint32_t) size, 0, nullptr);
    if (ret != RKNN_SUCC) {
        LOGE("[initRetinaFace] rknn_init failed: %d", ret);
        return;
    }

    g_loaded = true;
    generateAnchors320(); // tạo anchors cố định cho model RetinaFace 320x320
    LOGI("[initRetinaFace] NPU ready — g_ctx=%p", (void *) (uintptr_t) g_ctx);
}
