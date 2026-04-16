package com.example.npufacerecognition;

import androidx.appcompat.app.AppCompatActivity;
import androidx.camera.core.CameraSelector;
import androidx.camera.core.ImageAnalysis;
import androidx.camera.core.ImageProxy;
import androidx.camera.core.Preview;
import androidx.camera.lifecycle.ProcessCameraProvider;
import androidx.camera.view.PreviewView;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import android.Manifest;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.graphics.RectF;
import android.os.Bundle;
import android.util.Log;

import com.example.npufacerecognition.databinding.ActivityMainBinding;
import com.google.common.util.concurrent.ListenableFuture;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MainActivity extends AppCompatActivity {

    static {
        System.loadLibrary("npufacerecognition");
    }

    private static final String TAG = "MainActivity";
    private static final int CAMERA_PERMISSION_CODE = 100;

    private PreviewView previewView;
    private FaceOverlayView faceOverlayView;
    private ExecutorService cameraExecutor;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        ActivityMainBinding binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        previewView = binding.previewView;
        faceOverlayView = binding.faceOverlayView;

        // Kiểm tra thông tin NPU và model khi khởi động
        queryNPUInfo();
        queryModelInfo(getAssets(), "RetinaFace_mobile320.rknn");
        initRetinaFace(getAssets(), "RetinaFace_mobile320.rknn");

        cameraExecutor = Executors.newSingleThreadExecutor();
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
                == PackageManager.PERMISSION_GRANTED) {
            startCamera();
        } else {
            ActivityCompat.requestPermissions(
                    this,
                    new String[]{Manifest.permission.CAMERA},
                    CAMERA_PERMISSION_CODE
            );
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == CAMERA_PERMISSION_CODE) {
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                startCamera();
            } else {
                Log.e(TAG, "Camera permission denied");
            }
        }
    }

    private void startCamera() {
        ListenableFuture<ProcessCameraProvider> future =
                ProcessCameraProvider.getInstance(this);
        future.addListener(() -> {
            try {
                ProcessCameraProvider provider = future.get();
                Log.d(TAG, "Camera provider obtained: " + provider);

                /// Usecase Preview

                Preview preview = new Preview.Builder().build();
                preview.setSurfaceProvider(previewView.getSurfaceProvider());

                /// Usecase ImageAnalysis
                ImageAnalysis imageAnalysis = new ImageAnalysis.Builder()
                        .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST).build();

                imageAnalysis.setAnalyzer(cameraExecutor, imageProxy -> {
                    ImageProxy.PlaneProxy planeY = imageProxy.getPlanes()[0];
                    ImageProxy.PlaneProxy planeU = imageProxy.getPlanes()[1];
                    ImageProxy.PlaneProxy planeV = imageProxy.getPlanes()[2];

                    // --- LOG ROTATION ---
                    int rotation = imageProxy.getImageInfo().getRotationDegrees();
                    Log.d(TAG, "[analyzer] rotation=" + rotation
                            + " imgW=" + imageProxy.getWidth()
                            + " imgH=" + imageProxy.getHeight());

                    float[] result = runRetinaFace(
                            planeY.getBuffer(),
                            planeU.getBuffer(),
                            planeV.getBuffer(),
                            imageProxy.getWidth(),
                            imageProxy.getHeight(),
                            planeY.getRowStride(),
                            planeU.getRowStride(),
                            planeU.getPixelStride()
                    );
                    int imgW = imageProxy.getWidth();
                    int imgH = imageProxy.getHeight();
                    handleFaceResult(result, imgW, imgH, rotation);
                    imageProxy.close();
                });
                /// Thiết lập lifecycle
                CameraSelector cameraSelector = CameraSelector.DEFAULT_FRONT_CAMERA;
                provider.unbindAll();
                provider.bindToLifecycle(this, cameraSelector, preview, imageAnalysis);

            } catch (Exception e) {
                Log.e(TAG, "Camera start failed: " + e.getMessage());
            }
        }, ContextCompat.getMainExecutor(this));
    }

    public int Ret(int a) {
        return a;
    }

    private RectF findBestBox(List<RectF> boxes) {
        if (boxes == null || boxes.isEmpty()) return null;
        RectF bestBox = null;
        float maxArea = 0;
        for (RectF box : boxes) {
            float area = box.width() * box.height();
            if (area > maxArea) {
                maxArea = area;
                bestBox = box;
            }
        }
        return bestBox;
    }

    private static final int FACE_FIELDS = 15;

    private void handleFaceResult(float[] data, int imgW, int imgH, int rotation) {
        // Swap W/H nếu ảnh bị xoay 90° hoặc 270°
        int frameW = (rotation == 90 || rotation == 270) ? imgH : imgW;
        int frameH = (rotation == 90 || rotation == 270) ? imgW : imgH;

        if (data == null || data.length == 0) {
            runOnUiThread(() -> faceOverlayView.setFaces(null, frameW, frameH));
            return;
        }
        int count = data.length / FACE_FIELDS;
        List<FaceData> faceList = new ArrayList<>();
        for (int i = 0; i < count; i++) {
            int base = i * FACE_FIELDS;
            FaceData fd = new FaceData();
            float x1 = data[base];
            float y1 = data[base + 1];
            float x2 = data[base + 2];
            float y2 = data[base + 3];
            fd.score = data[base + 4];
            System.arraycopy(data, base + 5, fd.lm, 0, 10);
            if (rotation == 270) {
                fd.x1 = y1;
                fd.y1 = imgW - x2;
                fd.x2 = y2;
                fd.y2 = imgW - x1;
                // Flip landmarks
                for (int k = 0; k < 5; k++) {
                    float lx = fd.lm[k * 2];
                    float ly = fd.lm[k * 2 + 1];
                    fd.lm[k * 2] = ly;
                    fd.lm[k * 2 + 1] = imgW - lx;
                }
            } else if (rotation == 90) {
                fd.x1 = imgH - y2;
                fd.y1 = x1;
                fd.x2 = imgH - y1;
                fd.y2 = x2;
                for (int k = 0; k < 5; k++) {
                    float lx = fd.lm[k * 2];
                    float ly = fd.lm[k * 2 + 1];
                    fd.lm[k * 2] = imgH - ly;
                    fd.lm[k * 2 + 1] = lx;
                }
            } else {
                fd.x1 = x1;
                fd.y1 = y1;
                fd.x2 = x2;
                fd.y2 = y2;
            }
            float tmpX1 = frameW - fd.x2;
            float tmpX2 = frameW - fd.x1;
            fd.x1 = tmpX1;
            fd.x2 = tmpX2;
            for (int k = 0; k < 5; k++) {
                fd.lm[k * 2] = frameW - fd.lm[k * 2];
            }
            faceList.add(fd);
        }
        runOnUiThread(() -> faceOverlayView.setFaces(faceList, frameW, frameH));
    }

    // --- Native methods ---
    public native void queryNPUInfo();

    public native void queryModelInfo(AssetManager assetManager, String modelFileName);

    public native float[] runRetinaFace(
            java.nio.ByteBuffer yBuffer,
            java.nio.ByteBuffer uBuffer,
            java.nio.ByteBuffer vBuffer,
            int width, int height,
            int yRowStride, int uvRowStride,
            int uvPixelStride
    );


    private native void initRetinaFace(android.content.res.AssetManager assetManager,
                                       String modelFileName);
}