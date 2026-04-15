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

        previewView     = binding.previewView;
        faceOverlayView = binding.faceOverlayView;

        // Kiểm tra thông tin NPU và model khi khởi động
        queryNPUInfo();
        queryModelInfo(getAssets(), "RetinaFace_mobile320.rknn");

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

                    runRetinaFace(
                            planeY.getBuffer(),
                            planeU.getBuffer(),
                            planeV.getBuffer(),
                            imageProxy.getWidth(),
                            imageProxy.getHeight(),
                            planeY.getRowStride(),
                            planeU.getRowStride(),
                            planeU.getPixelStride()
                    );
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

    // --- Native methods ---
    public native void queryNPUInfo();
    public native void queryModelInfo(AssetManager assetManager, String modelFileName);
    public native void runRetinaFace(
            java.nio.ByteBuffer yBuffer,
            java.nio.ByteBuffer uBuffer,
            java.nio.ByteBuffer vBuffer,
            int width, int height,
            int yRowStride, int uvRowStride,
            int uvPixelStride
    );
}