package com.ast.breathlessamiga;

import android.os.Build;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

import java.util.Locale;

/**
 * Tells real game controllers and keyboards apart from built-in pseudo input
 * devices. Many phones expose their fingerprint sensor (and similar sensors)
 * as a uinput device that claims every key code, so Android classifies it as
 * a gamepad and an alphabetic keyboard. Trusting those flags hid the touch
 * controls on such phones.
 */
final class InputDevices {
    private InputDevices() {}

    private static final String[] PSEUDO_DEVICE_NAMES = {
            "uinput", "fingerprint", "fpc", "goodix", "gf_input", "gf3", "gf5", "gf9",
            "silead", "egis", "etxxx", "elan_fp", "elanfp", "chipone", "sunwave",
            "synaptics_fp", "qbt", "fp_input", "fp-keys", "fp_keys", "focal_fp",
            "novatek_fp", "betterlife", "microarray", "cdfinger", "jiiov",
    };

    static boolean isPseudoDevice(InputDevice device) {
        if (device == null) return true;
        if (device.isVirtual()) return true;
        final String name = device.getName() == null ? "" : device.getName().toLowerCase(Locale.ROOT);
        for (String pattern : PSEUDO_DEVICE_NAMES) {
            if (name.contains(pattern)) return true;
        }
        return false;
    }

    static boolean isControllerSource(int source) {
        return (source & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
                (source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    static boolean isController(InputDevice device) {
        return device != null && isControllerSource(device.getSources()) && !isPseudoDevice(device);
    }

    static boolean isControllerConnected() {
        for (int id : InputDevice.getDeviceIds()) {
            if (isController(InputDevice.getDevice(id))) return true;
        }
        return false;
    }

    static boolean isAlphabeticKeyboard(InputDevice device) {
        return device != null &&
                (device.getSources() & InputDevice.SOURCE_KEYBOARD) == InputDevice.SOURCE_KEYBOARD &&
                device.getKeyboardType() == InputDevice.KEYBOARD_TYPE_ALPHABETIC &&
                !isPseudoDevice(device);
    }

    static boolean isKeyboardConnected() {
        for (int id : InputDevice.getDeviceIds()) {
            if (isAlphabeticKeyboard(InputDevice.getDevice(id))) return true;
        }
        return false;
    }

    /** True when the event comes from a fingerprint or other pseudo device. */
    static boolean isFromPseudoDevice(KeyEvent event) {
        // Key events injected by the system or adb have no physical device.
        if (event.getDeviceId() < 0) return false;
        final InputDevice device = event.getDevice();
        return device != null && !device.isVirtual() && isPseudoDevice(device);
    }

    static boolean isFromPseudoDevice(MotionEvent event) {
        final InputDevice device = event.getDevice();
        return device != null && !device.isVirtual() && isPseudoDevice(device);
    }

    static boolean isFromController(KeyEvent event) {
        return isControllerSource(event.getSource()) && !isFromPseudoDevice(event);
    }

    static boolean isFromController(MotionEvent event) {
        return isControllerSource(event.getSource()) && !isFromPseudoDevice(event);
    }

    static boolean isFromDesktopKeyboard(KeyEvent event) {
        if ((event.getSource() & InputDevice.SOURCE_KEYBOARD) != InputDevice.SOURCE_KEYBOARD) return false;
        if (isControllerSource(event.getSource())) return false;
        return isAlphabeticKeyboard(event.getDevice());
    }

    static String describeDevices() {
        final StringBuilder text = new StringBuilder();
        for (int id : InputDevice.getDeviceIds()) {
            final InputDevice device = InputDevice.getDevice(id);
            if (device == null) continue;
            text.append(String.format(Locale.ROOT, "\n  #%d '%s' sources=0x%08x keyboard=%d virtual=%s controller=%s alphaKeyboard=%s",
                    id, device.getName(), device.getSources(), device.getKeyboardType(),
                    device.isVirtual(), isController(device), isAlphabeticKeyboard(device)));
            if (Build.VERSION.SDK_INT >= 19)
                text.append(String.format(Locale.ROOT, " vid=%04x pid=%04x", device.getVendorId(), device.getProductId()));
        }
        return text.toString();
    }
}
