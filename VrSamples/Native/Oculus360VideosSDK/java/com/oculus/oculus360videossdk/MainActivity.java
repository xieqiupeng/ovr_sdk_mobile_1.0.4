/************************************************************************************
 * Filename    :   MainActivity.java
 * Content     :
 * Created     :
 * Authors     :
 * <p/>
 * Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.
 * <p/>
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the Oculus360Videos/ directory. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *************************************************************************************/
package com.oculus.oculus360videossdk;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences.Editor;
import android.graphics.SurfaceTexture;
import android.media.AudioManager;
import android.media.MediaPlayer;
import android.os.Bundle;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.WindowManager;

import com.oculus.oculus360videossdk.client.HomeKeyLocker;
import com.oculus.vrappframework.VrActivity;

import java.io.IOException;

import static android.content.Context.MODE_PRIVATE;

public class MainActivity extends VrActivity implements android.graphics.SurfaceTexture.OnFrameAvailableListener,
        MediaPlayer.OnVideoSizeChangedListener,
        MediaPlayer.OnCompletionListener,
        MediaPlayer.OnErrorListener,
        AudioManager.OnAudioFocusChangeListener {

    public static final String TAG = "Oculus360Videos";

    /** Load jni .so on initialization */
    static {
        Log.d(TAG, "LoadLibrary");
        System.loadLibrary("oculus360videos");
    }

    public static native void nativeSetVideoSize(long appPtr, int width, int height);

    public static native SurfaceTexture nativePrepareNewVideo(long appPtr);

    public static native void nativeFrameAvailable(long appPtr);

    public static native void nativeVideoCompletion(long appPtr);

    public static native void nativeVideoStart(long appPtr, int name);

    public static native void nativeVideoStop(long appPtr);

    public static native void nativeVideo3D(long appPtr, int d);

    public static native long nativeSetAppInterface(VrActivity act, String fromPackageNameString, String commandString, String uriString);

    SurfaceTexture movieTexture = null;
    Surface movieSurface = null;
    MediaPlayer mediaPlayer = null;
    AudioManager audioManager = null;

    // ==================================================================================

    void requestAudioFocus() {
        // Request audio focus
        int result = audioManager.requestAudioFocus(this, AudioManager.STREAM_MUSIC,
                AudioManager.AUDIOFOCUS_GAIN);
        if (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
            Log.d(TAG, "startMovie(): GRANTED audio focus");
        } else if (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
            Log.d(TAG, "startMovie(): FAILED to gain audio focus");
        }
    }

    void releaseAudioFocus() {
        audioManager.abandonAudioFocus(this);
    }

    // ==================================================================================
    HomeKeyLocker mHomeKeyLocker = null;

    protected void onCreate(Bundle savedInstanceState) {
        Log.d(TAG, "onCreate enter");
//        this.getWindow().addPrivateFlags(WindowManager.LayoutParams.PRIVATE_FLAG_HOMEKEY_DISPATCHED);
        super.onCreate(savedInstanceState);
//        HomeKeyEventBroadCastReceiver receiver = new HomeKeyEventBroadCastReceiver();
//        registerReceiver(receiver, new IntentFilter(Intent.ACTION_CLOSE_SYSTEM_DIALOGS));
        //
        mHomeKeyLocker = new HomeKeyLocker();
//        mHomeKeyLocker.lock(this);
        //
        Intent intent = getIntent();
        String commandString = VrActivity.getCommandStringFromIntent(intent);
        String fromPackageNameString = VrActivity.getPackageStringFromIntent(intent);
        String uriString = VrActivity.getUriStringFromIntent(intent);

        setAppPtr(nativeSetAppInterface(this, fromPackageNameString, commandString, uriString));

        audioManager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
    }

    @Override
    protected void onDestroy() {
        Log.d(TAG, "onDestroy");
        // Abandon audio focus if we still hold it
        mHomeKeyLocker.unlock();
        mHomeKeyLocker = null;
        releaseAudioFocus();
        super.onDestroy();
    }

    // --
    // --> MediaPlayer.OnErrorListener START
    public boolean onError(MediaPlayer mp, int what, int extra) {
        Log.e(TAG, "MediaPlayer.OnErrorListener - what : " + what + ", extra : " + extra);
        return false;
    }

    // <-- MediaPlayer.OnErrorListener END

    // ==================================================================================

    public void onVideoSizeChanged(MediaPlayer mp, int width, int height) {


        Log.v(TAG, String.format("onVideoSizeChanged: %dx%d", width, height));
        if (width == 0 || height == 0) {
            Log.e(TAG, "The video size is 0. Could be because there was no video, no display surface was set, or the value was not determined yet.");
        } else {
            nativeSetVideoSize(getAppPtr(), width, height);
        }
    }

    public void onCompletion(MediaPlayer mp) {
        Log.v(TAG, String.format("onCompletion"));
        nativeVideoCompletion(getAppPtr());
    }

    public void onFrameAvailable(SurfaceTexture surfaceTexture) {
        nativeFrameAvailable(getAppPtr());
    }

    public void onAudioFocusChange(int focusChange) {
        switch (focusChange) {
            case AudioManager.AUDIOFOCUS_GAIN:
                // resume() if coming back from transient loss, raise stream volume if duck applied
                Log.d(TAG, "onAudioFocusChangedListener: AUDIOFOCUS_GAIN");
                break;
            case AudioManager.AUDIOFOCUS_LOSS:                // focus lost permanently
                // stop() if isPlaying
                Log.d(TAG, "onAudioFocusChangedListener: AUDIOFOCUS_LOSS");
                break;
            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT:    // focus lost temporarily
                // pause() if isPlaying
                Log.d(TAG, "onAudioFocusChangedListener: AUDIOFOCUS_LOSS_TRANSIENT");
                break;
            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK:    // focus lost temporarily
                // lower stream volume
                Log.d(TAG, "onAudioFocusChangedListener: AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK");
                break;
            default:
                break;
        }
    }

    // TODO
    public void stopMovie() {
        Log.d(TAG, "stopMovie()");

        if (mediaPlayer != null) {
            Log.d(TAG, "movie stopped");
            mediaPlayer.stop();
        }

        releaseAudioFocus();
    }

    public boolean isPlaying() {
        try {
            if (mediaPlayer != null) {
                boolean playing = mediaPlayer.isPlaying();
                if (playing) {
                    Log.d(TAG, "isPlaying() = true");
                } else {
                    Log.d(TAG, "isPlaying() = false");
                }
                return playing;
            }
            Log.d(TAG, "isPlaying() - NO MEDIA PLAYER");
        } catch (IllegalStateException ise) {
            Log.d(TAG, "isPlaying(): Caught illegalStateException: " + ise.toString());
        }
        return false;
    }

    //TODO
    public void pauseMovie() {
        Log.d(TAG, "pauseMovie()");
        try {
            if (mediaPlayer != null) {
                Log.d(TAG, "movie paused");
                mediaPlayer.pause();
            }
        } catch (IllegalStateException ise) {
            Log.d(TAG, "pauseMovie(): Caught illegalStateException: " + ise.toString());
        }
    }

    //TODO
    public void resumeMovie() {
        Log.d(TAG, "resumeMovie()");
        try {
            if (mediaPlayer != null) {
                Log.d(TAG, "movie started");
                mediaPlayer.start();
                mediaPlayer.setVolume(1.0f, 1.0f);
            }
        } catch (IllegalStateException ise) {
            Log.d(TAG, "resumeMovie(): Caught illegalStateException: " + ise.toString());
        }
    }

    // called from native code for starting movie
    public void seekToFromNative(final int seekPos) {
        Log.d(TAG, "seekToFromNative to " + seekPos);
        try {
            if (mediaPlayer != null) {
                mediaPlayer.seekTo(seekPos);
            }
        } catch (IllegalStateException ise) {
            Log.d(TAG, "seekToFromNative(): Caught illegalStateException: " + ise.toString());
        }
    }

    // called from native code for starting movie
    public void startMovieFromNative(final String pathName) {
        Log.d(TAG, "startMovieFromNative");
        //
        runOnUiThread(new Thread() {
            @Override
            public void run() {
                Log.d(TAG, "startMovieFromNative");
                startMovie(pathName);
            }
        });
    }

    public void start(String name) {
        if (name.endsWith("_TB.mp4")) {
            nativeVideoStart(getAppPtr(), 82);
        }
        nativeVideoStart(getAppPtr(), 0);
    }

    public void set3D(boolean is3D) {
        if (is3D) {
            nativeVideo3D(getAppPtr(), 3);
        } else {
            nativeVideo3D(getAppPtr(), 0);
        }
    }

    public void stop() {
        nativeVideoStop(getAppPtr());
    }

    //TODO
    public void startMovie(String pathName) {
        Log.v(TAG, "startMovie " + pathName);
        //
        synchronized (this) {
            // Request audio focus
            requestAudioFocus();

            // Have native code pause any playing movie,
            // allocate a new external texture,
            // and create a surfaceTexture with it.
            movieTexture = nativePrepareNewVideo(getAppPtr());
            movieTexture.setOnFrameAvailableListener(this);
            movieSurface = new Surface(movieTexture);

            if (mediaPlayer != null) {
                mediaPlayer.release();
            }

            Log.v(TAG, "MediaPlayer.create");

            synchronized (this) {
                mediaPlayer = new MediaPlayer();
            }
            mediaPlayer.setOnVideoSizeChangedListener(this);
            mediaPlayer.setOnCompletionListener(this);
            mediaPlayer.setSurface(movieSurface);

            try {
                Log.v(TAG, "mediaPlayer.setDataSource()");
                mediaPlayer.setDataSource(pathName);
                try {
                    Log.v(TAG, "mediaPlayer.prepare");
                    mediaPlayer.prepare();
                } catch (IOException t) {
                    Log.e(TAG, "mediaPlayer.prepare failed:" + t.getMessage());
                }
            } catch (IOException t) {
                Log.e(TAG, "mediaPlayer.setDataSource failed");
            }
            Log.v(TAG, "mediaPlayer.start");

            // If this movie has a saved position, seek there before starting
            // This seems to make movie switching crashier.
            final int seekPos = getPreferences(MODE_PRIVATE).getInt(pathName + "_pos", 0);
            if (seekPos > 0) {
                try {
                    mediaPlayer.seekTo(seekPos);
                } catch (IllegalStateException ise) {
                    Log.d(TAG, "mediaPlayer.seekTo(): Caught illegalStateException: " + ise.toString());
                }
            }

            mediaPlayer.setLooping(false);

            try {
                mediaPlayer.start();
            } catch (IllegalStateException ise) {
                Log.d(TAG, "mediaPlayer.start(): Caught illegalStateException: " + ise.toString());
            }

            mediaPlayer.setVolume(1.0f, 1.0f);

            // Save the current movie now that it was successfully started
            Editor edit = getPreferences(MODE_PRIVATE).edit();
            edit.putString("currentMovie", pathName);
            edit.commit();
        }

        Log.v(TAG, "returning");
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
//        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
//            Log.v(TAG, "dispatchKeyEvent back");
//            mHomeKeyLocker.lock(MainActivity.this);
//        }
        return true;
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        return true;
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent e) {
        return true;
    }
}
