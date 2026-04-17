package com.airport.facepod.jni;

import android.content.res.AssetManager;
import android.util.Log;

import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;

import com.facepod.FaceData;

public class FaceEngine {

    private static final String TAG = "FaceEngine";
    private static final int FACE_FIELDS = 15; // x1,y1,x2,y2,score,lm[10]

    static {
        System.loadLibrary("npufacerecognition");
    }

    // ── Native declarations (package-private, only used here) ──
    private static native void nativeQueryNPUInfo();
    private static native void nativeQueryModelInfo(AssetManager assetManager, String modelFileName);
    private static native void nativeInitRetinaFace(AssetManager assetManager, String modelFileName);
    private static native float[] nativeRunRetinaFace(
            ByteBuffer yBuffer, ByteBuffer uBuffer, ByteBuffer vBuffer,
            int width, int height,
            int yRowStride, int uvRowStride, int uvPixelStride
    );

    // ── Public API ──

    public void queryNPUInfo() {
        nativeQueryNPUInfo();
    }

    public void queryModelInfo(AssetManager assets, String modelFileName) {
        nativeQueryModelInfo(assets, modelFileName);
    }

    public void init(AssetManager assets, String modelFileName) {
        nativeInitRetinaFace(assets, modelFileName);
        Log.i(TAG, "FaceEngine initialized with model: " + modelFileName);
    }

    public List<FaceData> detect(
            ByteBuffer yBuffer, ByteBuffer uBuffer, ByteBuffer vBuffer,
            int width, int height,
            int yRowStride, int uvRowStride, int uvPixelStride,
            int rotationDegrees) {

        float[] raw = nativeRunRetinaFace(
                yBuffer, uBuffer, vBuffer,
                width, height,
                yRowStride, uvRowStride, uvPixelStride
        );

        return decodeFaces(raw, width, height, rotationDegrees);
    }

    public static int[] getFrameSize(int imgW, int imgH, int rotation) {
        if (rotation == 90 || rotation == 270) {
            return new int[]{imgH, imgW};
        }
        return new int[]{imgW, imgH};
    }

    // ── Internal: decode raw float[] → List<FaceData> with rotation + mirror ──

    private List<FaceData> decodeFaces(float[] data, int imgW, int imgH, int rotation) {
        int frameW = (rotation == 90 || rotation == 270) ? imgH : imgW;

        if (data == null || data.length == 0) {
            return new ArrayList<>();
        }

        int count = data.length / FACE_FIELDS;
        List<FaceData> faceList = new ArrayList<>(count);

        for (int i = 0; i < count; i++) {
            int base = i * FACE_FIELDS;
            FaceData fd = new FaceData();

            float x1 = data[base];
            float y1 = data[base + 1];
            float x2 = data[base + 2];
            float y2 = data[base + 3];
            fd.score = data[base + 4];
            System.arraycopy(data, base + 5, fd.lm, 0, 10);

            // Apply rotation transform
            if (rotation == 270) {
                fd.x1 = y1;
                fd.y1 = imgW - x2;
                fd.x2 = y2;
                fd.y2 = imgW - x1;
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

            // Mirror horizontally (front camera)
            float tmpX1 = frameW - fd.x2;
            float tmpX2 = frameW - fd.x1;
            fd.x1 = tmpX1;
            fd.x2 = tmpX2;
            for (int k = 0; k < 5; k++) {
                fd.lm[k * 2] = frameW - fd.lm[k * 2];
            }

            faceList.add(fd);
        }

        return faceList;
    }
}