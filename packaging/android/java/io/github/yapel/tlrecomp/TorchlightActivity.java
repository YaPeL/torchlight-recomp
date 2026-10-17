package io.github.yapel.tlrecomp;

import android.content.Context;
import android.content.pm.ApplicationInfo;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

/**
 * The app's activity: SDL's, which loads the native libraries below and calls SDL_main in the main
 * one (docs/android-port.md, stage A1). The main library is the game's (libtorchlight.so) or, in a
 * development build that replays captures, the replay's (libreplay.so): the APK carries one,
 * named in assets/main_library.txt by tools/android/build_apk.sh.
 *
 * Development flags: one per line in args.txt in the app's external files folder (adb push to
 * /sdcard/Android/data/<package>/files/args.txt); lines starting with # are skipped. Read only by
 * a debuggable (development) build: a release never takes flags from a file anyone can write.
 */
public class TorchlightActivity extends SDLActivity {
    private String mainLibrary() {
        try (BufferedReader reader = new BufferedReader(
                new java.io.InputStreamReader(getAssets().open("main_library.txt")))) {
            String name = reader.readLine();
            if (name != null && !name.trim().isEmpty()) return name.trim();
        } catch (IOException e) {
            // fall through to the game
        }
        return "torchlight";
    }

    @Override
    protected String[] getLibraries() {
        // The C++ runtime and the SDK's runtime (with SDL inside) before the main library, which SDL
        // takes as the last one (getMainSharedObject).
        return new String[] {"c++_shared", "rexruntime", mainLibrary()};
    }

    // The surface that tells the backend when it goes and comes back (TorchlightSurface).
    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new TorchlightSurface(context);
    }

    // Landscape only (docs/android-port.md, section 4): SDL otherwise turns the activity to the
    // shape of each window it makes (the first start's dialogs, a window sized from the display
    // while the device was held upright), and the game's window came out upright.
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        super.setOrientationBis(w, h, resizable, "LandscapeLeft LandscapeRight");
    }

    static native void nativeAppEnding();

    @Override
    protected void onDestroy() {
        // A backend waiting for its surface to come back stops waiting: nothing will be shown.
        if (!mBrokenLibraries) nativeAppEnding();
        super.onDestroy();
    }

    @Override
    protected String[] getArguments() {
        List<String> args = new ArrayList<>();
        if ((getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0) {
            return new String[0];
        }
        File file = new File(getExternalFilesDir(null), "args.txt");
        if (file.isFile()) {
            try (BufferedReader reader = new BufferedReader(new FileReader(file))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    line = line.trim();
                    if (!line.isEmpty() && !line.startsWith("#")) args.add(line);
                }
            } catch (IOException e) {
                // no flags
            }
        }
        return args.toArray(new String[0]);
    }
}
