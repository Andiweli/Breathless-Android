package com.ast.breathlessamiga;

import android.content.Context;
import android.os.SystemClock;

import com.ast.retrotouch.RetroTouchAdapter;
import com.ast.retrotouch.RetroTouchControl;
import com.ast.retrotouch.RetroTouchLayout;
import com.ast.retrotouch.RetroTouchMode;
import com.ast.retrotouch.RetroTouchView;

import java.util.ArrayList;
import java.util.List;

/**
 * Breathless-specific RetroTouch configuration and input mapping.
 *
 * This class deliberately contains no RetroTouch implementation. Keeping the game adapter on
 * the app side means a compatible RetroTouch update only requires replacing the AAR.
 */
final class BreathlessTouchBridge {
    static final int MODE_OFF = 0;
    static final int MODE_GAMEPLAY = 1;
    static final int MODE_NAVIGATION = 2;

    private static final float MOVE_DEADZONE = 0.08f;
    private static final long RUN_DOUBLE_TAP_MS = 350L;

    interface HostInput {
        void onAction(String actionId, boolean pressed);
        void onMove(float x, float y);
        void onLook(float deltaX, float deltaY);
    }

    private final RetroTouchView view;
    private final HostInput host;
    private long lastRunTapMs;
    private boolean runLatched;
    private boolean runPressed;
    private boolean moveActive;

    BreathlessTouchBridge(Context context, HostInput hostInput) {
        if (hostInput == null) throw new IllegalArgumentException("Host input must not be null");
        host = hostInput;
        view = new RetroTouchView(context);
        configureActions();
        configureLayouts();
        view.setAutoHideOnController(true);
        view.setLookSensitivity(1.15f);
        view.setLookWhileHoldingAction("fire", true);
        view.setListener(new RetroTouchAdapter() {
            @Override public void onAction(String actionId, boolean pressed) {
                if ("run".equals(actionId)) handleRun(pressed);
                else host.onAction(actionId, pressed);
            }

            @Override public void onMove(float x, float y) {
                boolean moving = Math.abs(x) > MOVE_DEADZONE || Math.abs(y) > MOVE_DEADZONE;
                if (moveActive != moving) {
                    moveActive = moving;
                    updateRunState();
                }
                host.onMove(x, y);
            }

            @Override public void onLook(float deltaX, float deltaY) {
                host.onLook(deltaX, deltaY);
            }
        });
        view.setMode(RetroTouchMode.OFF);
    }

    RetroTouchView getView() { return view; }

    void setMode(int nativeMode) {
        RetroTouchMode next = nativeMode == MODE_GAMEPLAY ? RetroTouchMode.GAMEPLAY :
                (nativeMode == MODE_NAVIGATION ? RetroTouchMode.NAVIGATION : RetroTouchMode.OFF);
        if (view.getMode() == RetroTouchMode.GAMEPLAY && next != RetroTouchMode.GAMEPLAY) resetRun();
        view.setMode(next);
    }

    void reset() {
        resetRun();
        // Beta 3 releases every active pointer/action explicitly. This also
        // prevents a held ChromeOS mouse button from surviving focus loss.
        view.resetInputState();
        view.setMode(RetroTouchMode.OFF);
    }

    private void configureActions() {
        view.registerAction("fire", "Fire");
        view.registerAction("use", "Use");
        view.registerAction("run", "Run");
        view.registerAction("weapon_next", "Next Weapon");
        view.registerAction("weapon_1", "Weapon 1");
        view.registerAction("weapon_2", "Weapon 2");
        view.registerAction("weapon_3", "Weapon 3");
        view.registerAction("weapon_4", "Weapon 4");
        view.registerAction("weapon_5", "Weapon 5");
        view.registerAction("weapon_6", "Weapon 6");
        view.registerAction("map", "Map");
        view.registerAction("menu", "Menu");
        view.registerAction("nav_ok", "OK");
        view.registerAction("nav_back", "Back");
    }

    private void configureLayouts() {
        List<RetroTouchControl> gameplay = new ArrayList<RetroTouchControl>();
        gameplay.add(RetroTouchControl.lookZone("look", 0.73f, 0.48f, 0.54f, 0.78f));
        gameplay.add(RetroTouchControl.moveStick("move", 0.15f, 0.76f, 0.25f));
        gameplay.add(RetroTouchControl.button("fire", "fire", "Fire", 0.90f, 0.72f, 0.15f));
        gameplay.add(RetroTouchControl.button("use", "use", "Use", 0.79f, 0.82f, 0.11f));
        gameplay.add(RetroTouchControl.button("run", "run", "Run", 0.29f, 0.67f, 0.11f));
        gameplay.add(RetroTouchControl.button("weapon", "weapon_next", "Next Weapon", 0.79f, 0.57f, 0.11f));
        gameplay.add(RetroTouchControl.button("map", "map", "Map", 0.66f, 0.11f, 0.09f));
        gameplay.add(RetroTouchControl.button("menu", "menu", "Menu", 0.54f, 0.11f, 0.09f));
        // Keep the original ID so existing saved Breathless gameplay layouts remain valid.
        view.setGameplayLayout(new RetroTouchLayout("breathless", gameplay));

        List<RetroTouchControl> navigation = new ArrayList<RetroTouchControl>();
        navigation.add(RetroTouchControl.dPad("navigation", 0.17f, 0.72f, 0.30f));
        navigation.add(RetroTouchControl.button("ok", "nav_ok", "OK", 0.87f, 0.68f, 0.14f));
        navigation.add(RetroTouchControl.button("back", "nav_back", "Back", 0.74f, 0.82f, 0.11f));
        view.setNavigationLayout(new RetroTouchLayout("breathless_navigation", navigation));
    }

    private void handleRun(boolean pressed) {
        if (pressed) {
            long now = SystemClock.uptimeMillis();
            runPressed = true;
            if (lastRunTapMs > 0L && now - lastRunTapMs <= RUN_DOUBLE_TAP_MS) {
                runLatched = !runLatched;
                lastRunTapMs = 0L;
                view.setActionLatched("run", runLatched);
            } else {
                lastRunTapMs = now;
            }
        } else {
            runPressed = false;
        }
        updateRunState();
    }

    private void updateRunState() {
        host.onAction("run", runPressed || (runLatched && moveActive));
    }

    private void resetRun() {
        lastRunTapMs = 0L;
        runLatched = false;
        runPressed = false;
        moveActive = false;
        host.onAction("run", false);
        view.setActionLatched("run", false);
    }
}
