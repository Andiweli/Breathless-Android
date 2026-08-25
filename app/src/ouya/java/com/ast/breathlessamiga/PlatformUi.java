package com.ast.breathlessamiga;

import android.app.Activity;
import android.content.pm.ActivityInfo;
import android.os.Build;
import android.view.View;
import android.view.WindowManager;

final class PlatformUi {
    private PlatformUi() { }

    static void configureWindow(Activity activity) {
        activity.getWindow().setFlags(
                WindowManager.LayoutParams.FLAG_FULLSCREEN |
                        WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON,
                WindowManager.LayoutParams.FLAG_FULLSCREEN |
                        WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
    }

    static View wrapContent(Activity activity, View content) {
        return content;
    }

    static boolean isOuyaDevice() {
        return containsOuya(Build.MANUFACTURER) || containsOuya(Build.BRAND) ||
                containsOuya(Build.MODEL) || containsOuya(Build.DEVICE) ||
                containsOuya(Build.PRODUCT);
    }

    private static boolean containsOuya(String value) {
        return value != null && value.toLowerCase(java.util.Locale.US).contains("ouya");
    }

    static void installSystemUiRestorer(final Activity activity) {
        activity.getWindow().getDecorView().setOnSystemUiVisibilityChangeListener(
                new View.OnSystemUiVisibilityChangeListener() {
                    @Override
                    public void onSystemUiVisibilityChange(int visibility) {
                        final int hidden = View.SYSTEM_UI_FLAG_FULLSCREEN |
                                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION;
                        if ((visibility & hidden) != hidden) {
                            activity.getWindow().getDecorView().postDelayed(new Runnable() {
                                @Override public void run() {
                                    applySystemUi(activity);
                                }
                            }, 250L);
                        }
                    }
                });
    }

    static void applySystemUi(Activity activity) {
        activity.getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                        View.SYSTEM_UI_FLAG_FULLSCREEN |
                        View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                        View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                        View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                        View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }
}
