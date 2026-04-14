package com.example.npufacerecognition;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.view.View;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

public class FaceOverlayView extends View {

    private final Paint boxPaint = new Paint();

    private RectF focusBox = null;

    private int frameWidth = 1;
    private int frameHeight = 1;

    public FaceOverlayView(Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);
        boxPaint.setColor(Color.GREEN);
        boxPaint.setStyle(Paint.Style.STROKE);
        boxPaint.setStrokeWidth(4f);
        boxPaint.setAntiAlias(true); // làm mịn đường viền
    }

    public void setFocusBox(RectF focusBox, int fw, int fh) {
        this.focusBox = focusBox;
        this.frameWidth = fw;
        this.frameHeight = fh;
        invalidate(); // Yêu cầu vẽ lại view
    }

    public void clearFocusBox() {
        this.focusBox = null;
        invalidate(); // Yêu cầu vẽ lại view
    }

    @Override
    protected void onDraw(@NonNull Canvas canvas) {
        super.onDraw(canvas);

        if (focusBox == null) return;
        float scaleX = (float) getWidth() / frameWidth;
        float scaleY = (float) getHeight() / frameHeight;
        float left = focusBox.left * scaleX;
        float top = focusBox.top * scaleY;
        float right = focusBox.right * scaleX;
        float bottom = focusBox.bottom * scaleY;
        canvas.drawRect(left, top, right, bottom, boxPaint);

    }
}
