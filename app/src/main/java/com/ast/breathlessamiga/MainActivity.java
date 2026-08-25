package com.ast.breathlessamiga;

import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Window;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.media.MediaPlayer;
import android.content.res.AssetFileDescriptor;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public class MainActivity extends Activity {
    private static final String TAG = "Breathless";
    private static final float DEADZONE = 0.08f;
    private static final String DATA_MARKER = ".breathless_data_ready_v3";
    private static final String[] REQUIRED_GAME_FILES = {
            "BLES0001.GLD", "BLES0002.GLD", "BLES0003.GLD", "BLES0004.GLD",
            "BLES0006.GLD", "BLES0007.GLD", "BLES0008.GLD", "BLES0009.GLD",
            "BLES0010.GLD", "BLES0011.GLD", "BLES0012.GLD", "BLES0013.GLD",
            "BLES0014.GLD", "BLES0015.GLD", "BLES0016.GLD", "BLES0017.GLD",
            "BLES0018.GLD", "BLES0019.GLD", "BLES0020.GLD", "BLES0021.GLD",
            "BLES0022.GLD", "BLES0023.GLD", "BLES0024.GLD", "BLES0025.GLD",
            "Panel04.raw", "Mirino01.raw", "Background01.raw"
    };
    private static final long[] REQUIRED_GAME_FILE_SIZES = {
            623112L, 231483L, 872522L, 328386L,
            5509L, 6135L, 6565L, 7990L,
            7492L, 7539L, 6850L, 7646L,
            8656L, 8448L, 6760L, 6613L,
            6268L, 6437L, 7237L, 6517L,
            8044L, 7110L, 8469L, 7817L,
            12800L, 352L, 16384L
    };

    private BreathlessView glView;
    private long lastAnalogLogMs = 0;
    private volatile boolean audioRunning = false;
    private Thread audioThread;
    private MediaPlayer musicPlayer;
    private String playingMusicCode;

    static {
        System.loadLibrary("breathless_native");
    }

    private static native void nativeSetDataPath(String path);
    private static native void nativeSetOuyaDevice(boolean ouyaDevice);
    private static native void nativeSaveProgress();
    private static native void nativeKey(int keyCode, boolean pressed);
    private static native void nativeTouch(float x, float y, int action);
    private static native void nativeAnalog(float lx, float ly, float rx, float ry, float hatX, float hatY);
    private static native void nativeMixAudio(short[] output);
    private static native String nativeMusicCode();
    private static native int nativeMusicVolume();
    private static native boolean nativeConsumeQuitRequest();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        PlatformUi.configureWindow(this);
        applyGameUi();

        File dataDir = gameDataDirectory();
        ensureGameData(dataDir);
        migrateLegacyState(dataDir);
        nativeSetOuyaDevice(PlatformUi.isOuyaDevice());
        nativeSetDataPath(dataDir.getAbsolutePath());

        glView = new BreathlessView(this);
        setContentView(PlatformUi.wrapContent(this, glView));
        PlatformUi.installSystemUiRestorer(this);
        glView.post(new Runnable() {
            @Override public void run() {
                applyGameUi();
                glView.requestFocus();
            }
        });
    }

    @Override
    protected void onResume() {
        super.onResume();
        applyGameUi();
        if (glView != null) {
            glView.onResume();
            glView.requestFocus();
        }
        startAudio();
        updateMusic();
    }

    @Override
    protected void onPause() {
        stopAudio();
        stopMusic();
        nativeAnalog(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        saveProgressOnRenderThread();
        if (glView != null) glView.onPause();
        super.onPause();
    }

    private void saveProgressOnRenderThread() {
        if (glView == null) {
            nativeSaveProgress();
            return;
        }
        final CountDownLatch completed = new CountDownLatch(1);
        glView.queueEvent(new Runnable() {
            @Override public void run() {
                try { nativeSaveProgress(); }
                finally { completed.countDown(); }
            }
        });
        try {
            if (!completed.await(900, TimeUnit.MILLISECONDS))
                Log.w(TAG, "Timed out while waiting for progress snapshot");
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
        }
    }

    private void startAudio() {
        if (audioRunning) return;
        audioRunning = true;
        audioThread = new Thread(new Runnable() {
            @Override public void run() {
                final int rate = 44100;
                final int minimum = AudioTrack.getMinBufferSize(rate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT);
                final int bytes = Math.max(4096, minimum);
                final AudioTrack track = new AudioTrack(AudioManager.STREAM_MUSIC, rate,
                        AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT, bytes, AudioTrack.MODE_STREAM);
                final short[] buffer = new short[bytes / 2];
                try {
                    track.play();
                    while (audioRunning) {
                        nativeMixAudio(buffer);
                        track.write(buffer, 0, buffer.length);
                    }
                } finally {
                    try { track.stop(); } catch (IllegalStateException ignored) { }
                    track.release();
                }
            }
        }, "BreathlessAudio");
        audioThread.setDaemon(true);
        audioThread.start();
    }

    private void stopAudio() {
        audioRunning = false;
        Thread thread = audioThread;
        audioThread = null;
        if (thread != null) {
            try { thread.join(300); } catch (InterruptedException ignored) { Thread.currentThread().interrupt(); }
        }
    }

    private void updateMusic() {
        final String wanted = nativeMusicCode();
        if (wanted == null || wanted.length() == 0) { stopMusic(); return; }
        final int level = Math.max(1, Math.min(5, nativeMusicVolume()));
        final float volume = 0.65f * (float) level / 5.0f;
        if (wanted.equals(playingMusicCode) && musicPlayer != null) {
            musicPlayer.setVolume(volume, volume);
            return;
        }
        stopMusic();
        try {
            AssetFileDescriptor descriptor = getAssets().openFd("music/" + wanted + ".ogg");
            MediaPlayer player = new MediaPlayer();
            player.setAudioStreamType(AudioManager.STREAM_MUSIC);
            player.setDataSource(descriptor.getFileDescriptor(), descriptor.getStartOffset(), descriptor.getLength());
            descriptor.close();
            player.setLooping(true);
            player.setVolume(volume, volume);
            player.prepare();
            player.start();
            musicPlayer = player;
            playingMusicCode = wanted;
        } catch (IOException | IllegalStateException error) {
            Log.e(TAG, "Unable to start original music " + wanted, error);
            stopMusic();
        }
    }

    private void stopMusic() {
        MediaPlayer player = musicPlayer;
        musicPlayer = null;
        playingMusicCode = null;
        if (player != null) {
            try { player.stop(); } catch (IllegalStateException ignored) { }
            player.release();
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) applyGameUi();
    }

    private void applyGameUi() {
        PlatformUi.applySystemUi(this);
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        final int keyCode = event.getKeyCode();
        if (keyCode == KeyEvent.KEYCODE_VOLUME_UP || keyCode == KeyEvent.KEYCODE_VOLUME_DOWN ||
                keyCode == KeyEvent.KEYCODE_VOLUME_MUTE || keyCode == KeyEvent.KEYCODE_MUTE) {
            // Let Android change the media volume and display its normal overlay.
            return super.dispatchKeyEvent(event);
        }
        int action = event.getAction();
        if (action == KeyEvent.ACTION_DOWN || action == KeyEvent.ACTION_UP) {
            nativeKey(keyCode, action == KeyEvent.ACTION_DOWN);
            if (action == KeyEvent.ACTION_DOWN) {
                updateMusic();
                if (nativeConsumeQuitRequest()) finish();
            }
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        float localX = event.getX();
        float localY = event.getY();
        if (glView != null) {
            localX = Math.max(0.0f, Math.min(glView.getWidth(), localX - glView.getLeft()));
            localY = Math.max(0.0f, Math.min(glView.getHeight(), localY - glView.getTop()));
        }
        nativeTouch(localX, localY, event.getActionMasked());
        if (event.getActionMasked() == MotionEvent.ACTION_UP) {
            updateMusic();
            if (nativeConsumeQuitRequest()) finish();
        }
        return true;
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (event.getActionMasked() == MotionEvent.ACTION_MOVE) {
            float rawX = axis(event, MotionEvent.AXIS_X);
            float rawY = axis(event, MotionEvent.AXIS_Y);
            float rawZ = axis(event, MotionEvent.AXIS_Z);
            float rawRz = axis(event, MotionEvent.AXIS_RZ);
            float rawRx = axis(event, MotionEvent.AXIS_RX);
            float rawRy = axis(event, MotionEvent.AXIS_RY);
            float hatX = axis(event, MotionEvent.AXIS_HAT_X);
            float hatY = axis(event, MotionEvent.AXIS_HAT_Y);

            float lx = centered(rawX);
            float ly = centered(rawY);

            float rx = pickCentered(rawZ, rawRx);
            float ry = pickCentered(rawRz, rawRy);

            nativeAnalog(lx, ly, rx, ry, centered(hatX), centered(hatY));
            if (Math.abs(hatX) > 0.5f || Math.abs(hatY) > 0.5f) updateMusic();

            long now = System.currentTimeMillis();
            boolean active = Math.abs(rawX) > 0.03f || Math.abs(rawY) > 0.03f ||
                    Math.abs(rawZ) > 0.03f || Math.abs(rawRz) > 0.03f ||
                    Math.abs(rawRx) > 0.03f || Math.abs(rawRy) > 0.03f ||
                    Math.abs(hatX) > 0.03f || Math.abs(hatY) > 0.03f;
            if (BuildConfig.DEBUG && active && now - lastAnalogLogMs > 1200) {
                Log.i(TAG, "analog java v32 source=" + event.getSource() +
                        " dev=" + event.getDeviceId() +
                        " X=" + rawX + " Y=" + rawY +
                        " Z=" + rawZ + " RZ=" + rawRz +
                        " RX=" + rawRx + " RY=" + rawRy +
                        " HAT=" + hatX + "," + hatY +
                        " -> L=" + lx + "," + ly + " R=" + rx + "," + ry);
                lastAnalogLogMs = now;
            }
            return true;
        }
        return super.dispatchGenericMotionEvent(event);
    }

    private static float axis(MotionEvent event, int axis) {
        try {
            return event.getAxisValue(axis);
        } catch (Throwable ignored) {
            return 0.0f;
        }
    }

    private static float centered(float v) {
        return Math.abs(v) < DEADZONE ? 0.0f : v;
    }

    private static float pickCentered(float a, float b) {
        float ca = centered(a);
        float cb = centered(b);
        return Math.abs(ca) >= Math.abs(cb) ? ca : cb;
    }

    private File gameDataDirectory() {
        // Before KitKat, app-specific external files could still require the
        // legacy storage permission on vendor builds. OUYA runs Android 4.1,
        // so keep its required runtime data in private, always-writable storage.
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.KITKAT) {
            if (BuildConfig.DEBUG)
                Log.i(TAG, "Using internal game-data storage on pre-KitKat Android");
            return new File(getFilesDir(), "Breathless");
        }
        File storageRoot = getExternalFilesDir(null);
        try {
            if (storageRoot != null && !storageRoot.exists() && !storageRoot.mkdirs()) {
                storageRoot = null;
            }
            if (storageRoot != null && !storageRoot.canWrite()) {
                storageRoot = null;
            }
        } catch (SecurityException denied) {
            storageRoot = null;
        }
        if (storageRoot == null) {
            storageRoot = getFilesDir();
            Log.w(TAG, "External app storage unavailable or read-only; using internal storage");
        }
        return new File(storageRoot, "Breathless");
    }

    private void migrateLegacyState(File currentDataDir) {
        try {
            File externalRoot = getExternalFilesDir(null);
            if (externalRoot == null) return;
            File legacyDataDir = new File(externalRoot, "Breathless");
            if (legacyDataDir.getCanonicalPath().equals(currentDataDir.getCanonicalPath())) return;
            copyLegacyStateFile(legacyDataDir, currentDataDir, "breathless_save_v1.dat");
            copyLegacyStateFile(legacyDataDir, currentDataDir, "breathless_controls_v1.dat");
        } catch (IOException | SecurityException error) {
            Log.w(TAG, "Could not migrate legacy save data", error);
        }
    }

    private void copyLegacyStateFile(File sourceDir, File targetDir, String name) throws IOException {
        File source = new File(sourceDir, name);
        File target = new File(targetDir, name);
        if (!source.isFile() || target.exists()) return;
        InputStream in = new java.io.FileInputStream(source);
        FileOutputStream out = new FileOutputStream(target);
        byte[] buffer = new byte[16 * 1024];
        try {
            int read;
            while ((read = in.read(buffer)) > 0) out.write(buffer, 0, read);
        } finally {
            try { in.close(); } finally { out.close(); }
        }
        if (BuildConfig.DEBUG) Log.i(TAG, "Migrated legacy state: " + name);
    }

    private boolean hasCompleteGameData(File dataDir) {
        for (int i = 0; i < REQUIRED_GAME_FILES.length; ++i) {
            File file = new File(dataDir, REQUIRED_GAME_FILES[i]);
            if (!file.isFile() || file.length() != REQUIRED_GAME_FILE_SIZES[i]) {
                Log.w(TAG, "Game data missing or outdated: " + REQUIRED_GAME_FILES[i] +
                        " (found " + (file.isFile() ? file.length() : 0L) +
                        ", expected " + REQUIRED_GAME_FILE_SIZES[i] + ")");
                return false;
            }
        }
        return true;
    }

    private void ensureGameData(File dataDir) {
        File marker = new File(dataDir, DATA_MARKER);
        if (marker.isFile() && hasCompleteGameData(dataDir)) {
            if (BuildConfig.DEBUG) Log.i(TAG, "Bundled game data verified");
            return;
        }
        if (!dataDir.exists() && !dataDir.mkdirs()) {
            Log.e(TAG, "Could not create data dir: " + dataDir);
            return;
        }
        try {
            if (BuildConfig.DEBUG) Log.i(TAG, "Installing or repairing bundled game data");
            unzipAsset("breathless_data.zip", dataDir);
            if (!hasCompleteGameData(dataDir)) {
                throw new IOException("Bundled game data did not pass validation after extraction");
            }
            FileOutputStream out = new FileOutputStream(marker);
            try {
                out.write('1');
            } finally {
                out.close();
            }
            if (BuildConfig.DEBUG) Log.i(TAG, "Bundled game data ready: " + dataDir);
        } catch (IOException | SecurityException e) {
            Log.e(TAG, "Data extraction failed", e);
        }
    }

    private void unzipAsset(String assetName, File targetDir) throws IOException {
        byte[] buffer = new byte[64 * 1024];
        InputStream in = getAssets().open(assetName);
        ZipInputStream zip = new ZipInputStream(in);
        try {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                File outFile = new File(targetDir, entry.getName());
                String targetPath = targetDir.getCanonicalPath();
                String outPath = outFile.getCanonicalPath();
                if (!outPath.startsWith(targetPath + File.separator)) {
                    throw new IOException("Blocked zip path: " + entry.getName());
                }
                if (entry.isDirectory()) {
                    if (!outFile.exists()) outFile.mkdirs();
                } else {
                    File parent = outFile.getParentFile();
                    if (parent != null && !parent.exists()) parent.mkdirs();
                    FileOutputStream out = new FileOutputStream(outFile);
                    try {
                        int read;
                        while ((read = zip.read(buffer)) > 0) out.write(buffer, 0, read);
                    } finally {
                        out.close();
                    }
                }
                zip.closeEntry();
            }
        } finally {
            zip.close();
            in.close();
        }
    }
}
