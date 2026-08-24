package com.ast.breathlessamiga;

import android.opengl.GLSurfaceView;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

final class BreathlessRenderer implements GLSurfaceView.Renderer {
    private static native void nativeSurfaceCreated();
    private static native void nativeInit(int width, int height);
    private static native void nativeResize(int width, int height);
    private static native void nativeRender();

    public void onSurfaceCreated(GL10 gl, EGLConfig config) { nativeSurfaceCreated(); }
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        nativeInit(width, height);
        nativeResize(width, height);
    }
    public void onDrawFrame(GL10 gl) { nativeRender(); }
}
