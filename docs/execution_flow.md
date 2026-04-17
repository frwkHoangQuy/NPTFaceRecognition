# Execution Flow

## Execution Pipeline

The runtime execution pipeline processes a single camera frame through the following discrete stages:

1. **Frame Acquisition** -- `CameraManager` configures CameraX `ImageAnalysis` with `STRATEGY_KEEP_ONLY_LATEST` backpressure. The analyzer callback executes on a dedicated single-thread `ExecutorService` (background thread). Raw YUV_420_888 plane buffers (`ByteBuffer`) are extracted from the `ImageProxy` without copy.

2. **JNI Boundary Crossing** -- `FaceEngine.detect()` forwards the three direct `ByteBuffer` references (Y, U, V) along with stride metadata to `nativeRunRetinaFace()`. The JNI layer accesses the buffers via `GetDirectBufferAddress`, achieving zero-copy transfer from Java heap to native address space.

3. **Native Preprocessing** -- Within `native-lib.cpp`, the YUV planes are converted to BGR using stride-aware row iteration. The BGR frame is letterbox-resized to the model input dimensions (320x320) and packed into NCHW float layout for RKNN consumption.

4. **NPU Inference** -- The preprocessed tensor is submitted to the RK3588 NPU via the RKNN C API (`rknn_run`). Inference executes on dedicated NPU cores with hardware-level parallelism, returning raw output tensors (bounding box regressions, classification scores, landmark offsets).

5. **Post-processing (Decode + NMS)** -- Anchor-based decoding transforms regression deltas into absolute coordinates. Softmax scoring filters candidates below the confidence threshold. Non-Maximum Suppression (NMS) eliminates redundant overlapping detections. The surviving detections are packed into a flat `float[]` array (15 floats per face: x1, y1, x2, y2, score, 10 landmark values) and returned across the JNI boundary.

6. **Rotation and Mirror Transform** -- `FaceEngine.decodeFaces()` applies coordinate rotation (0, 90, or 270 degrees) to align detections with the display orientation, followed by horizontal mirroring to correct for front-camera reflection.

7. **UI Thread Dispatch** -- `MainActivity.onCameraFrame()` invokes `runOnUiThread()` to post the decoded `List<FaceData>` and computed frame dimensions to `FaceOverlayView.setFaces()`, which triggers `invalidate()` and a subsequent `onDraw()` pass on the main thread.

## Sequence Diagram

```mermaid
sequenceDiagram
    autonumber
    participant CX as CameraX<br/>(Background Thread)
    participant CM as CameraManager<br/>ImageAnalysis.Analyzer
    participant FE as FaceEngine<br/>(Java JNI Bridge)
    participant NL as native-lib.cpp<br/>(C++ / RKNN)
    participant NPU as RK3588 NPU<br/>(Hardware)
    participant MA as MainActivity<br/>(Background Thread)
    participant UI as FaceOverlayView<br/>(UI Thread)

    CX->>CM: onImageAvailable(ImageProxy)
    Note over CM: Executes on cameraExecutor<br/>(single background thread)
    CM->>CM: Extract Y/U/V ByteBuffer planes<br/>+ rowStride, pixelStride, rotation
    CM->>MA: callback.onFrame(yBuf, uBuf, vBuf, w, h, strides, rotation)
    MA->>FE: detect(yBuf, uBuf, vBuf, w, h, strides, rotation)
    FE->>NL: nativeRunRetinaFace(yBuf, uBuf, vBuf, w, h, strides)
    Note over NL: GetDirectBufferAddress<br/>(zero-copy access)
    NL->>NL: YUV420 to BGR conversion<br/>(stride-aware)
    NL->>NL: Letterbox resize to 320x320<br/>+ NCHW float packing
    NL->>NPU: rknn_run(input_tensor)
    NPU-->>NL: Raw output tensors<br/>(bbox, cls, lm)
    NL->>NL: Anchor decode + softmax<br/>+ NMS filtering
    NL-->>FE: float[] (15 per face)
    FE->>FE: decodeFaces(): rotation transform<br/>+ horizontal mirror
    FE-->>MA: List of FaceData
    MA->>UI: runOnUiThread -> setFaces(faces, frameW, frameH)
    Note over UI: invalidate() triggers onDraw()<br/>on Android UI thread
    UI->>UI: Scale bboxes to View dimensions<br/>+ render on Canvas
    CM->>CM: imageProxy.close()
```

