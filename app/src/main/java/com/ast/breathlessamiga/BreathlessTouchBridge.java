package com.ast.breathlessamiga;

import android.content.Context;
import android.graphics.Color;
import android.os.SystemClock;

import com.ast.retrotouch.RetroTouchAdapter;
import com.ast.retrotouch.RetroTouchButtonStyle;
import com.ast.retrotouch.RetroTouchColors;
import com.ast.retrotouch.RetroTouchControl;
import com.ast.retrotouch.RetroTouchIcon;
import com.ast.retrotouch.RetroTouchLayout;
import com.ast.retrotouch.RetroTouchLook;
import com.ast.retrotouch.RetroTouchSkin;
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

    // Blue/grey look shared by every button, the D-pad and the move stick (RetroTouch beta.13).
    private static final int SLATE = Color.rgb(46, 56, 74);
    private static final int SLATE_BORDER = Color.rgb(150, 168, 196);
    private static final int PALE_BLUE = Color.rgb(226, 234, 246);
    private static final int BLUE = Color.rgb(74, 122, 196);
    private static final int BLUE_LIGHT = Color.rgb(190, 212, 244);
    private static final int FIRE_BLUE = Color.rgb(44, 78, 132);

    // Icons not built into RetroTouch: Lucide "refresh-cw", "check" and "arrow-left" (ISC license,
    // 24 x 24 view box) in RetroTouch's stroke style.
    private static final RetroTouchIcon ICON_CYCLE = RetroTouchIcon.fromPaths(24f,
            "M3 12a9 9 0 0 1 9-9 9.75 9.75 0 0 1 6.74 2.74L21 8", "M21 3v5h-5",
            "M21 12a9 9 0 0 1-9 9 9.75 9.75 0 0 1-6.74-2.74L3 16", "M8 16H3v5");
    private static final RetroTouchIcon ICON_CHECK = RetroTouchIcon.fromPaths(24f, "M20 6 9 17l-5-5");
    private static final RetroTouchIcon ICON_BACK = RetroTouchIcon.fromPaths(24f, "m12 19-7-7 7-7", "M19 12H5");

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
        configureSkin();
    }

    private void configureSkin() {
        RetroTouchColors base = new RetroTouchColors(SLATE, BLUE, SLATE_BORDER, PALE_BLUE)
                .withPressedColors(BLUE, BLUE_LIGHT, Color.WHITE)
                .withLatchedColors(BLUE, BLUE_LIGHT, Color.WHITE); // always-run toggle
        RetroTouchColors fire = new RetroTouchColors(FIRE_BLUE, BLUE, BLUE_LIGHT, Color.WHITE)
                .withPressedColors(BLUE, Color.WHITE, Color.WHITE);
        // A single skin: RetroTouch then shows no style toggle in the editor.
        RetroTouchSkin skin = new RetroTouchSkin("breathless", "Breathless", RetroTouchButtonStyle.ICONS)
                .setDefaultColors(base)
                .setLook("fire", new RetroTouchLook().withIcon(RetroTouchIcon.CROSSHAIR).withColors(fire))
                .setLook("use", new RetroTouchLook().withIcon(RetroTouchIcon.HAND))
                .setLook("run", new RetroTouchLook().withIcon(RetroTouchIcon.FOOTPRINTS))
                .setLook("weapon_next", new RetroTouchLook().withIcon(ICON_CYCLE))
                .setLook("map", new RetroTouchLook().withIcon(RetroTouchIcon.MAP))
                .setLook("menu", new RetroTouchLook().withIcon(RetroTouchIcon.MENU))
                .setLook("nav_ok", new RetroTouchLook().withIcon(ICON_CHECK))
                .setLook("nav_back", new RetroTouchLook().withIcon(ICON_BACK))
                .setDpadIcons(RetroTouchIcon.ARROW_UP, RetroTouchIcon.ARROW_DOWN,
                        RetroTouchIcon.ARROW_LEFT, RetroTouchIcon.ARROW_RIGHT);
        for (int weapon = 1; weapon <= 6; ++weapon)
            skin.setLook("weapon_" + weapon, new RetroTouchLook().withLabel(Integer.toString(weapon)));
        view.registerSkin(skin);
        view.setDefaultSkin("breathless");
        view.setAccentColor(BLUE);
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
