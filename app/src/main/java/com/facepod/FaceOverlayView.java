package com.facepod;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.view.View;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import java.util.List;

public class FaceOverlayView extends View {

    private final Paint boxPaint = new Paint();
    private final Paint lmPaint = new Paint();

    private List<FaceData> faces = null;
    private int frameWidth = 1;
    private int frameHeight = 1;

    public FaceOverlayView(Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);
        boxPaint.setColor(Color.GREEN);
        boxPaint.setStyle(Paint.Style.STROKE);
        boxPaint.setStrokeWidth(4f);
        boxPaint.setAntiAlias(true);

        lmPaint.setColor(Color.RED);
        lmPaint.setStyle(Paint.Style.FILL);
        lmPaint.setAntiAlias(true);
    }

    public void setFaces(List<FaceData> faces, int fw, int fh) {
        this.faces = faces;
        this.frameWidth = fw;
        this.frameHeight = fh;
        invalidate();
    }

    @Override
    protected void onDraw(@NonNull Canvas canvas) {
        super.onDraw(canvas);
        if (faces == null || faces.isEmpty()) return;

        float scaleX = (float) getWidth() / frameWidth;
        float scaleY = (float) getHeight() / frameHeight;

        for (FaceData f : faces) {
            // Bounding box
            float l = f.x1 * scaleX;
            float t = f.y1 * scaleY;
            float r = f.x2 * scaleX;
            float b = f.y2 * scaleY;
            canvas.drawRect(l, t, r, b, boxPaint);

            // 5 Landmarks
//            for (int k = 0; k < 5; k++) {
//                float lx = f.lm[k * 2] * scaleX;
//                float ly = f.lm[k * 2 + 1] * scaleY;
//                canvas.drawCircle(lx, ly, 6f, lmPaint);
//            }
        }
    }
}
