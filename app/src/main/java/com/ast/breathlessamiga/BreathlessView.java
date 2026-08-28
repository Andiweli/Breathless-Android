package com.ast.breathlessamiga;

import android.content.Context;
import android.opengl.GLSurfaceView;
import android.view.MotionEvent;

final class BreathlessView extends GLSurfaceView {
    interface CapturedPointerListener {
        boolean onCapturedPointerEvent(MotionEvent event);
        void onPointerCaptureChanged(boolean hasCapture);
    }

    private CapturedPointerListener capturedPointerListener;

    BreathlessView(Context context) {
        super(context);
        setEGLContextClientVersion(2);
        setPreserveEGLContextOnPause(true);
        setRenderer(new BreathlessRenderer());
        setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
        setFocusable(true);
        setFocusableInTouchMode(true);
        requestFocus();
    }

    void setCapturedPointerListener(CapturedPointerListener listener) {
        capturedPointerListener = listener;
    }

    @Override
    public boolean onCapturedPointerEvent(MotionEvent event) {
        if (capturedPointerListener != null &&
                capturedPointerListener.onCapturedPointerEvent(event)) return true;
        return false;
    }

    @Override
    public void onPointerCaptureChange(boolean hasCapture) {
        if (capturedPointerListener != null)
            capturedPointerListener.onPointerCaptureChanged(hasCapture);
    }
}
