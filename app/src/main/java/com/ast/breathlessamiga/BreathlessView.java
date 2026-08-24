package com.ast.breathlessamiga;

import android.content.Context;
import android.opengl.GLSurfaceView;

final class BreathlessView extends GLSurfaceView {
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
}
