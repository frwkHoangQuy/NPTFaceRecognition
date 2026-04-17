package com.facepod;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.util.Log;

import com.airport.facepod.hardware.CameraManager;
import com.airport.facepod.jni.FaceEngine;
import com.facepod.databinding.ActivityMainBinding;

import java.nio.ByteBuffer;
import java.util.List;

public class MainActivity extends AppCompatActivity {

    private static final String TAG = "MainActivity";
    private static final int CAMERA_PERMISSION_CODE = 100;
    private static final String MODEL_NAME = "RetinaFace_mobile320.rknn";

    private FaceOverlayView faceOverlayView;
    private CameraManager cameraManager;
    private FaceEngine faceEngine;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        ActivityMainBinding binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        faceOverlayView = binding.faceOverlayView;

        // Init FaceEngine (JNI + NPU)
        faceEngine = new FaceEngine();
        faceEngine.queryNPUInfo();
        faceEngine.queryModelInfo(getAssets(), MODEL_NAME);
        faceEngine.init(getAssets(), MODEL_NAME);

        // Wire CameraManager
        cameraManager = new CameraManager(
                this, this, binding.previewView, this::onCameraFrame);

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
                == PackageManager.PERMISSION_GRANTED) {
            cameraManager.startCamera();
        } else {
            ActivityCompat.requestPermissions(
                    this,
                    new String[]{Manifest.permission.CAMERA},
                    CAMERA_PERMISSION_CODE
            );
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode,
                                           String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == CAMERA_PERMISSION_CODE
                && grantResults.length > 0
                && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            cameraManager.startCamera();
        } else {
            Log.e(TAG, "Camera permission denied");
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (cameraManager != null) cameraManager.shutdown();
    }

    /**
     * Called on camera background thread.
     * ⚠️ UI updates via runOnUiThread().
     */
    private void onCameraFrame(
            ByteBuffer yBuffer, ByteBuffer uBuffer, ByteBuffer vBuffer,
            int width, int height,
            int yRowStride, int uvRowStride, int uvPixelStride,
            int rotationDegrees) {

        List<FaceData> faces = faceEngine.detect(
                yBuffer, uBuffer, vBuffer,
                width, height,
                yRowStride, uvRowStride, uvPixelStride,
                rotationDegrees
        );

        int[] frameSize = FaceEngine.getFrameSize(width, height, rotationDegrees);

        // ⚠️ runOnUiThread — callback fires on background thread
        runOnUiThread(() -> faceOverlayView.setFaces(
                faces.isEmpty() ? null : faces,
                frameSize[0], frameSize[1]
        ));
    }
}
