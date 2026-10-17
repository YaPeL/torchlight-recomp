package io.github.yapel.tlrecomp;

import android.content.Context;
import android.view.Surface;
import android.view.SurfaceHolder;

import org.libsdl.app.SDLSurface;

/**
 * SDL's surface, telling the native side when it goes and comes back (docs/android-port.md, stage
 * A1.3). OGRE draws in it with its own EGL surface, and SDL waits for nobody before letting a
 * surface go when it does not render in it itself: the backend lets go of its EGL surface first,
 * on the thread it draws with, while this callback waits (platform_android.cpp,
 * ServeSurfaceChanges).
 */
public class TorchlightSurface extends SDLSurface {
    static native void nativeSurfaceDestroying();
    static native void nativeSurfaceReady(Surface surface);

    public TorchlightSurface(Context context) {
        super(context);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        nativeSurfaceDestroying();
        super.surfaceDestroyed(holder);
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        super.surfaceChanged(holder, format, width, height);
        nativeSurfaceReady(holder.getSurface());
    }
}
