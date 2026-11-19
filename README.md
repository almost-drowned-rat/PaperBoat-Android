# PaperBoat — Android

The Android version of [PaperBoat](https://github.com/HarbourMasters/PaperBoat), Harbour Masters' Paper Mario 64
port: the game running natively on Android with touch buttons, full controller support and RetroArch shaders.
Made for Android handhelds with a built-in controller and for phones. Tested on the **AYN Odin 2 Portal**
(Snapdragon 8 Gen 2, 1920×1080, Android 13).

This repository is a fork of the main project: branch `develop` follows upstream, branch `android` (the default) is
the Android port, on top of upstream's own Android build. The main project's README is kept as
[README-UPSTREAM.md](README-UPSTREAM.md).

> ⚠️ **No game files.** The app contains only this project's code. You need your own legally obtained ROM of
> **Paper Mario (USA)**, sha1 `3837f44cda784b466c9a2d99df70d77c322b97a0`.

## Install

1. Download the `.apk` from the [Releases](../../releases) page and open it on the device (allow installing apps
   from your browser or file manager when Android asks).
2. Start **PaperBoat** and choose **Choose Paper Mario ROM** in Android's file picker. The game files (`pm64.o2r`)
   are made from it once — that takes a moment — then the game starts.

Updates install over the previous version and keep your saves. To get them automatically, add this repository to
[Obtainium](https://github.com/ImranR98/Obtainium) (`https://github.com/almost-drowned-rat/PaperBoat-Android`).

Needs Android 7.0 or newer and a 64-bit (arm64) device; RetroArch shaders need Android 10 or newer and Vulkan.
App ID `io.github.almostdrownedrat.paperboat` — it installs next to other PaperBoat builds. Release 0.1.0 is built on PaperBoat
1.0.3.

## Controls

- **Controller**: works out of the box (built-in handheld controls or a Bluetooth / USB pad); the engine's menu is
  navigated with the controller on Android.
- **Touch buttons** (N64 layout: stick, A, B, Z, L, R, START, C buttons; D-pad hidden by default), sent to the
  game as a controller. **Automatic** hides them while a controller is used and a touch brings them back; **Always**
  and **Off** are the other modes. The engine's own touch controls start off on Android.
- **Back** (the back gesture, the system bar's back button or a handheld's own Back key) opens the menu over the
  game, fully usable with the controller (D-pad / stick, **A** choose, **B** back, holding the D-pad keeps moving):
  - **Back to the game**
  - **Touch controls** — mode, move and resize, size, single buttons, opacity, stick sensitivity, reset
  - **Settings** — the engine's menu (with its Mods tab)
  - **Shaders** — RetroArch shaders (below)
  - **FPS** — a small readout of the frame rate and frame time in the corner
  - **Exit game**

  With the engine's menu open, Back closes it. In the menu Back opens, **Y** sets the selected setting back to its
  default.

## Shaders

Back > **Shaders** has everything in one place:

- **Shader** — On / Off (On takes the preset last used).
- **Shader selection** — the folders and presets of the imported packs. None are shipped: **Import shader pack
  (.zip)** takes a slang shader pack such as [libretro/slang-shaders](https://github.com/libretro/slang-shaders)'
  "Download ZIP"; it is unpacked into the app's folder (`Android/data/io.github.almostdrownedrat.paperboat/files/shaders`).
- **Shader resolution** — the picture the shader works on: **Original** (240 lines, as in RetroArch: the game draws
  in its low resolution mode and the shader gets that picture - the look the shaders were made for, and the
  lightest), **2x** (480 lines) or **Full** (the screen's size). While the game's own menu is open, the shader works
  on the full picture so the menu stays readable.
- **Shader parameters** — the active preset's settings (kept per preset).
- **Import shader pack (.zip)**

While a shader is in use the game draws into a buffer that goes through the shader onto the screen; switching
between shaders applies at once, from Off to a shader at the next start.

## Fixes for Android

- Native heap pointer tagging is off: Android 11+ tags arm64 heap pointers, which garbled textures and text.

## Known issues

Report problems on this repository's [Issues](../../issues) page, with the device and Android version. Problems
of the game itself (also on the PC) belong to the [main project](https://github.com/HarbourMasters/PaperBoat/issues).

## Building

As upstream's CI (`.github/workflows/build.yml`, job `build-android`): install the Android SDK, NDK and JDK 21,
then run `android/gradlew -p android assembleRelease`. No ROM is needed to build: the app makes the game files from
the player's ROM on the device. Android sources: `android/` and `src/port/android/`.

## AI assistance

The changes in this Android version were made with AI assistance ([Claude Code](https://claude.com/claude-code)).
Planning, testing on the device and the decision what gets released are by the repository owner. Use at your own
risk. This applies only to this version's own changes, not to the code of the original projects.

## Credits and license

- [HarbourMasters/PaperBoat](https://github.com/HarbourMasters/PaperBoat) — PaperBoat by Bass3l, JeodC, KiritoDv and
  Caladius (see [README-UPSTREAM.md](README-UPSTREAM.md)), built on [libultraship](https://github.com/Kenix3/libultraship)
  and the [Paper Mario DX decompilation](https://github.com/bates64/papermario-dx)
- [librashader](https://github.com/SnowflakePowered/librashader) by SnowflakePowered (MIT / MPL-2.0 / GPL-3.0) —
  RetroArch shaders, loaded at run time

License: see [LICENSE](LICENSE) (CC0 1.0, as the main project).

*Fan project, not affiliated with Nintendo or Intelligent Systems. Paper Mario is © Nintendo.*
