package com.example.npufacerecognition;

import androidx.appcompat.app.AppCompatActivity;
import androidx.camera.core.CameraSelector;
import androidx.camera.core.Preview;
import androidx.camera.lifecycle.ProcessCameraProvider;
import androidx.camera.view.PreviewView;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.core.content.PackageManagerCompat;

import android.Manifest;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.graphics.RectF;
import android.os.Bundle;
import android.util.Log;
import android.widget.TextView;

import java.util.concurrent.Executors;

import com.example.npufacerecognition.databinding.ActivityMainBinding;
import com.google.common.util.concurrent.ListenableFuture;

import java.util.List;
import java.util.concurrent.ExecutorService;

public class MainActivity extends AppCompatActivity {

    // Used to load the 'npufacerecognition' library on application startup.
    static {
        System.loadLibrary("npufacerecognition");
    }

    private final String TAG = "MainActivity";

    private ActivityMainBinding binding;

    private PreviewView previewView;
    private FaceOverlayView faceOverlayView;
    private ExecutorService cameraExecutor;

    private static final int CAMERA_PERMISSION_CODE = 100;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        int ketqua = sumFromCPP(100, 200);
        Log.d(TAG, "Ket qua: " + ketqua);

        Log.d(TAG, process());

        Log.d(TAG, getNPUInfo(getAssets()));

        previewView = binding.previewView;
        faceOverlayView = binding.faceOverlayView;

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
                //Liên kết tầng preview của camera với lifecycle của activity
                Preview preview = new Preview.Builder().build();
                preview.setSurfaceProvider(previewView.getSurfaceProvider());
                // Chọn camera sau làm nguồn video
                CameraSelector cameraSelector = CameraSelector.DEFAULT_FRONT_CAMERA;
                // Ngắt liên kết tất cả các use case trước khi liên kết mới
                provider.unbindAll();
                // Liên kết use case preview với lifecycle của activity
                provider.bindToLifecycle(this, cameraSelector, preview);
            } catch (Exception e) {
                Log.e(TAG, "Camera start failed: " + e.getMessage());
            }
        }, ContextCompat.getMainExecutor(this));
    }


    public String processInJava() {
        return "Processed in Java";
    }

    public int Ret(int a) {
        return a;
    }

    private RectF findBestBox(List<RectF> boxes) {
        if (boxes == null || boxes.isEmpty()) {
            return null;
        }
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


    public native String stringFromJNI();

    public native int sumFromCPP(int a, int b);

    public native String process();

    public native String getNPUInfo(AssetManager assetManager);
}