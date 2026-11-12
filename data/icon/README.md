# The game's icon

A photo of the repository owner's own cat, made pixel art by the owner with ChatGPT (2026-10-24).
It is not from the game. It comes from the owner's `torchlight-xbox-ui-pc` repository
(`installer/data/icon/`), where the owner gave it under that repository's licence, the GNU GPL
version 3; this repository's licence is the same (`LICENSE`).

- `wizard-64.png`: the setup wizard's window icon, built into the executable
  (`src/wizard/CMakeLists.txt`).
- `torchlight-recomp-256.png`: the AppImage's icon (`packaging/linux/make_appimage.sh`) and the `.app`'s
  (`packaging/macos/make_app.sh`).
- `torchlight-recomp.ico`: the Windows executable's icon, 16 to 256 pixels (top `CMakeLists.txt`).
- `packaging/android/res/mipmap-*`: the Android launcher icon, named in the manifest
  (`android:icon`). `mipmap-anydpi-v26/ic_launcher.xml` is the adaptive icon: the cat
  (`ic_launcher_foreground.png`, 108 dp layer) on a plain colour (`values/colors.xml`), scaled so
  its farthest pixel stays inside the 66 dp safe circle that every launcher shape keeps.
  `ic_launcher.png` is the plain 48 dp icon. Both come in the five densities (mdpi 1x to xxxhdpi
  4x).
- `source-1254.png`: the owner's original, the form to edit; the others are made from it (cropped to
  the cat, centred on a transparent square, scaled with Lanczos).
