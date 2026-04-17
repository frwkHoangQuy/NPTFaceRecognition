package com.airport.facepod.hardware;

import android.content.Context;
import android.util.Log;

import androidx.camera.core.CameraSelector;
import androidx.camera.core.ImageAnalysis;
import androidx.camera.core.ImageProxy;
import androidx.camera.core.Preview;
import androidx.camera.lifecycle.ProcessCameraProvider;
import androidx.camera.view.PreviewView;
import androidx.core.content.ContextCompat;
import androidx.lifecycle.LifecycleOwner;

import com.google.common.util.concurrent.ListenableFuture;

import java.nio.ByteBuffer;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class CameraManager {

    private static final String TAG = "CameraManager";

    public interface OnFrameCallback {
        void onFrame(
                ByteBuffer yBuffer, ByteBuffer uBuffer, ByteBuffer vBuffer,
                int width, int height,
                int yRowStride, int uvRowStride, int uvPixelStride,
                int rotationDegrees
        );
    }

    private final Context context;
    private final LifecycleOwner lifecycleOwner;
    private final PreviewView previewView;
    private final OnFrameCallback callback;
    private final ExecutorService cameraExecutor;

    public CameraManager(Context context, LifecycleOwner lifecycleOwner,
                         PreviewView previewView, OnFrameCallback callback) {
        this.context = context;
        this.lifecycleOwner = lifecycleOwner;
        this.previewView = previewView;
        this.callback = callback;
        this.cameraExecutor = Executors.newSingleThreadExecutor();
    }

    public void startCamera() {
        ListenableFuture<ProcessCameraProvider> future =
                ProcessCameraProvider.getInstance(context);

        future.addListener(() -> {
            try {
                ProcessCameraProvider provider = future.get();
                Log.d(TAG, "Camera provider obtained: " + provider);

                // Use-case 1: Preview
                Preview preview = new Preview.Builder().build();
                preview.setSurfaceProvider(previewView.getSurfaceProvider());

                // Use-case 2: ImageAnalysis
                ImageAnalysis imageAnalysis = new ImageAnalysis.Builder()
                        .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                        .build();

                imageAnalysis.setAnalyzer(cameraExecutor, imageProxy -> {
                    // ⚠️ This runs on cameraExecutor (background thread).
                    // Do NOT touch any View here.
                    try {
                        ImageProxy.PlaneProxy planeY = imageProxy.getPlanes()[0];
                        ImageProxy.PlaneProxy planeU = imageProxy.getPlanes()[1];
                        ImageProxy.PlaneProxy planeV = imageProxy.getPlanes()[2];

                        int rotation = imageProxy.getImageInfo().getRotationDegrees();
                        Log.d(TAG, "[analyzer] rotation=" + rotation
                                + " imgW=" + imageProxy.getWidth()
                                + " imgH=" + imageProxy.getHeight());

                        callback.onFrame(
                                planeY.getBuffer(),
                                planeU.getBuffer(),
                                planeV.getBuffer(),
                                imageProxy.getWidth(),
                                imageProxy.getHeight(),
                                planeY.getRowStride(),
                                planeU.getRowStride(),
                                planeU.getPixelStride(),
                                rotation
                        );
                    } finally {
                        imageProxy.close();
                    }
                });

                // Bind to lifecycle
                CameraSelector cameraSelector = CameraSelector.DEFAULT_FRONT_CAMERA;
                provider.unbindAll();
                provider.bindToLifecycle(lifecycleOwner, cameraSelector, preview, imageAnalysis);

            } catch (Exception e) {
                Log.e(TAG, "Camera start failed: " + e.getMessage());
            }
        }, ContextCompat.getMainExecutor(context));
    }

    public void shutdown() {
        cameraExecutor.shutdown();
    }
}