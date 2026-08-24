package com.ast.breathlessamiga;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Window;
import android.view.View;
import android.view.WindowManager;
import android.content.pm.ActivityInfo;
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
        getWindow().setFlags(
                WindowManager.LayoutParams.FLAG_FULLSCREEN | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON,
                WindowManager.LayoutParams.FLAG_FULLSCREEN | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        applyGameUi();

        File dataDir = new File(getExternalFilesDir(null), "Breathless");
        ensureGameData(dataDir);
        nativeSetDataPath(dataDir.getAbsolutePath());

        glView = new BreathlessView(this);
        setContentView(glView);
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
        saveProgressOnRenderThread();
        stopAudio();
        stopMusic();
        nativeAnalog(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
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
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                View.SYSTEM_UI_FLAG_FULLSCREEN |
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
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
        nativeTouch(event.getX(), event.getY(), event.getActionMasked());
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
            if (active && now - lastAnalogLogMs > 1200) {
                Log.i(TAG, "analog java v26 source=" + event.getSource() +
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

    private void ensureGameData(File dataDir) {
        File marker = new File(dataDir, ".breathless_data_ready_v2");
        if (marker.exists()) return;
        if (!dataDir.exists() && !dataDir.mkdirs()) {
            Log.e(TAG, "Could not create data dir: " + dataDir);
            return;
        }
        try {
            unzipAsset("breathless_data.zip", dataDir);
            FileOutputStream out = new FileOutputStream(marker);
            out.write('1');
            out.close();
        } catch (IOException e) {
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
