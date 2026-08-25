package com.ast.breathlessamiga;

import android.app.Activity;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.graphics.Color;
import android.os.Build;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.FrameLayout;

final class PlatformUi {
    private PlatformUi() { }

    static void configureWindow(Activity activity) {
        if (isAutomotiveDevice(activity)) {
            activity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
        } else {
            activity.getWindow().setFlags(
                    WindowManager.LayoutParams.FLAG_FULLSCREEN |
                            WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON,
                    WindowManager.LayoutParams.FLAG_FULLSCREEN |
                            WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        }
    }

    static View wrapContent(Activity activity, View content) {
        if (!isAutomotiveDevice(activity)) return content;

        final FrameLayout safeContainer = new FrameLayout(activity);
        safeContainer.setBackgroundColor(Color.BLACK);
        safeContainer.setClipToPadding(true);
        safeContainer.addView(content, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));
        safeContainer.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override
            public WindowInsets onApplyWindowInsets(View view, WindowInsets insets) {
                view.setPadding(insets.getSystemWindowInsetLeft(),
                        insets.getSystemWindowInsetTop(),
                        insets.getSystemWindowInsetRight(),
                        insets.getSystemWindowInsetBottom());
                return insets;
            }
        });
        return safeContainer;
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
        if (isAutomotiveDevice(activity)) return;

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
        if (isAutomotiveDevice(activity)) {
            activity.getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_VISIBLE | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        } else {
            activity.getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                            View.SYSTEM_UI_FLAG_FULLSCREEN |
                            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                            View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                            View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                            View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    private static boolean isAutomotiveDevice(Activity activity) {
        if (activity.getPackageManager().hasSystemFeature(PackageManager.FEATURE_AUTOMOTIVE))
            return true;
        final int type = activity.getResources().getConfiguration().uiMode &
                Configuration.UI_MODE_TYPE_MASK;
        return type == Configuration.UI_MODE_TYPE_CAR;
    }
}
