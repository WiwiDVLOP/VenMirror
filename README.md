# VenMirror

![Windows](https://github.com/WiwiDVLOP/VenMirror/workflows/Windows/badge.svg)
![license](https://img.shields.io/badge/license-Apache2.0-blue.svg)
![release](https://img.shields.io/github/v/release/WiwiDVLOP/VenMirror.svg)

[中文用户？点我查看中文介绍](README_zh.md)

**VenMirror** is a fork of [QtScrcpy](https://github.com/barry-ran/QtScrcpy) (by barry-ran / RanKun, Apache-2.0) with a **visual keymap editor**: map keyboard and mouse to phone touches by dragging controls over the game, pressing a key to bind it, and saving — no more hand-editing JSON files. It is made for playing Android games on PC from a mirrored phone, e.g. Monster Hunter Outlanders.

VenMirror supports displaying and controlling Android devices via USB or over network. It does NOT require root privileges.

It supports three major platforms: GNU/Linux, Windows and macOS.

It focuses on:

 - **lightness** (displays only the device screen)
 - **performance** (30~60 fps)
 - **quality** (1920×1080 or above)
 - **low latency** ([35~70ms][lowlatency])
 - **low startup time** (only about 1 second to display the first frame)
 - **non-intrusiveness** (nothing will be installed on the device)

[lowlatency]: https://github.com/Genymobile/scrcpy/pull/646

![win](screenshot/win-en.png)

![mac](screenshot/mac-en.png)

![linux](screenshot/linux-en.png)

## Visual keymap editor (new in VenMirror)
Instead of writing a JSON script by hand, use the visual editor:

1. Connect to your phone and start the service.
2. Click **Editar mapa** (Edit keymap) in the main window.
3. The current keymap's controls appear drawn over the video.
4. **Drag** a control to move it, **click** one to select it, then **press a key** to bind it.
5. Use the palette to add a touch button, a joystick (steer wheel), a hold-to-aim button or a drag control. Delete or duplicate controls as needed.
6. Click **Guardar** (Save): the map is written to your user keymap folder and applied live, without restarting the mirror.

While editing, the device is in normal mode, so clicks on the video do not fire game actions.

The editor writes the same JSON format QtScrcpy reads, so maps made by hand keep working, and maps made in the editor work in QtScrcpy too.

## Mapping Keys
You can write your script to map keyboard and mouse actions to touches and clicks of the mobile phone according to your needs. [Here](docs/KeyMapDes.md) are the script writing rules.

A keymap for Monster Hunter Outlanders is bundled as an example (`keymap/monster-hunter-outlanders.json`): WASD joystick, mouse to attack and move the camera, Space to dodge, Q potion, T/E/V skills, C aim, X net, G gather, Z cold/heat, H rapid shot, 1-4 portraits.

Instruction for adding new customized mapping files.

- Write a customized script and put it in the `keymap` directory (or save it from the visual editor)
- Click `refresh script` to show it
- Select your script
- Connect to your phone, start service and click `apply`
- Press the SwitchKey of the map (F in the bundled Monster Hunter Outlanders map) to switch to custom mapping mode
- Press the SwitchKey again to switch back to normal mode
- (For games such as PUBG Mobile) If you want to move vehicles with the STEER_WHEEL keys, you need to set the move mode to `single rocker mode`.

## Group control
You can control all your phones at the same time.

![group-control-demo](docs/image/group-control.gif)

## Thanks

VenMirror is a fork of [QtScrcpy](https://github.com/barry-ran/QtScrcpy) by [barry-ran](https://github.com/barry-ran) (RanKun), which is based on [Genymobile](https://github.com/Genymobile)'s [scrcpy](https://github.com/Genymobile/scrcpy) project. Thanks a lot!

The difference between QtScrcpy and the original scrcpy is as follows:

key points|scrcpy|QtScrcpy
--|:--:|:--:
ui|sdl|qt
video encode|ffmpeg|ffmpeg
video render|sdl|opengl
cross-platform|self implemented|provided by Qt
language|C|C++
style|sync|async
keymap|no custom keymap|support custom keymap
build|meson+gradle|qmake or CMake

- It's very easy to customize your GUI with Qt
- Asynchronous programming of Qt-based signal slot mechanism improves performance
- Easy to learn
- Add support for multi-touch

## Requirements
Android API >= 21 (Android 5.0).

Make sure you have enabled [ADB debugging][enable-adb] on your device(s).

[enable-adb]: https://developer.android.com/studio/command-line/adb.html#Enabling


## Download

[github-download]: https://github.com/WiwiDVLOP/VenMirror/releases

### Windows
On Windows, for simplicity, prebuilt archives with all the dependencies (including ADB) are available at Releases:

 - [`VenMirror`][github-download]

or you can [build it yourself](#Build)

### Mac OS
On Mac OS, for simplicity, prebuilt archives with all the dependencies (including ADB) are available at Releases:

- [`VenMirror`][github-download]

or you can [build it yourself](#Build)

### Linux
For users in other distros, you can use the prebuilt archives from Releases:

- [`VenMirror`][github-download]

or you can [build it yourself](#Build) (not recommended, get it in Actions if you can)

## Run
Connect to your Android device on your computer, then run the program and click `USB connect` or `WiFi connect`

### Wireless connection steps (ensure that the mobile phone and PC are on the same LAN):
1. Enable USB debugging in developer options on the Android device
2. Connect the Android device to the computer via USB
3. Click update device, and you will see that the device number is updated
4. Click get device IP
5. Click start adbd
6. Click wireless connect
7. Click update device again, and another device with an IP address will be found. Select this device.
8. Click start service


Note: it is not necessary to keep your Android device connected via USB after you start adbd.

## Interface button introduction：

- Start config: function parameter settings before starting the service    

    You can set the bit rate, resolution, recording format, and video save path of the locally recorded video.

    - Background record: the Android device screen is not displayed after starting the service. It is recorded in the background.
    - Always on top: the video window for Android devices will be kept on the top
    - Close screen: automatically turn off the Android device screen to save power after starting the service
    - Reverse connection: service startup mode. You can uncheck it if you experience connection failure with a message `more than one device`
    
- Refresh devices: Refresh the currently connected device
- Start service: connect to the Android device
- Stop service: disconnect from the Android device
- Stop all services: disconnect all connected Android devices
- Get device IP: Get the IP address of the Android device and update it to the "Wireless" area for the ease of wireless connection setting.
- Start adbd: Start the adbd service of the Android device. You must start it before the wireless connection.
- Wireless connect: Connect to Android devices wirelessly
- Wireless disconnect: Disconnect wirelessly connected Android devices
- adb command: execute customized ADB commands (blocking commands are not supported now, such as a shell)


## The main function
- Display Android device screens in real-time
- Real-time mouse and keyboard control of Android devices
- Screen recording
- Screenshot to png
- Wireless connection
- Supports multiple device connections
- Full-screen display
- Display on the top
- Install apk: drag and drop apk to the video window to install
- Transfer files: Drag files to the video window to send files to Android devices
- Background recording: record only, no display interface
- Copy-paste

    It is possible to synchronize clipboards between the computer and the device, in
    both directions:

    - `Ctrl + c` copies the device clipboard to the computer clipboard;
    - `Ctrl + Shift + v` copies the computer clipboard to the device clipboard;
    - `Ctrl + v` _pastes_ the computer clipboard as a sequence of text events (non-ASCII characters does not yet work).
- Group control
- Sync device speaker sound to the computer (based on [sndcpy](https://github.com/rom1v/sndcpy), Android 10+ only)

## Shortcuts

 | Action                                 |   Shortcut (Windows)          |   Shortcut (macOS)
 | -------------------------------------- |:----------------------------- |:-----------------------------
 | Switch fullscreen mode                 | `Ctrl`+`f`                    | `Cmd`+`f`
 | Resize window to 1:1 (pixel-perfect)   | `Ctrl`+`g`                    | `Cmd`+`g`
 | Resize window to remove black borders  | `Ctrl`+`w` \| _Double-click¹_ | `Cmd`+`w`  \| _Double-click¹_
 | Click on `HOME`                        | `Ctrl`+`h` \| _Middle-click_  | `Ctrl`+`h` \| _Middle-click_
 | Click on `BACK`                        | `Ctrl`+`b` \| _Right-click²_  | `Cmd`+`b`  \| _Right-click²_
 | Click on `APP_SWITCH`                  | `Ctrl`+`s`                    | `Cmd`+`s`
 | Click on `MENU`                        | `Ctrl`+`m`                    | `Ctrl`+`m`
 | Click on `VOLUME_UP`                   | `Ctrl`+`↑` _(up)_             | `Cmd`+`↑` _(up)_
 | Click on `VOLUME_DOWN`                 | `Ctrl`+`↓` _(down)_           | `Cmd`+`↓` _(down)_
 | Click on `POWER`                       | `Ctrl`+`p`                    | `Cmd`+`p`
 | Power on                               | _Right-click²_                | _Right-click²_
 | Turn device screen off (keep mirroring)| `Ctrl`+`o`                    | `Cmd`+`o`
 | Expand notification panel              | `Ctrl`+`n`                    | `Cmd`+`n`
 | Collapse notification panel            | `Ctrl`+`Shift`+`n`            | `Cmd`+`Shift`+`n`
 | Copy to clipboard³                     | `Ctrl`+`c`                    | `Cmd`+`c`
 | Cut to clipboard³                      | `Ctrl`+`x`                    | `Cmd`+`x`
 | Synchronize clipboards and paste³      | `Ctrl`+`v`                    | `Cmd`+`v`
 | Inject computer clipboard text         | `Ctrl`+`Shift`+`v`            | `Cmd`+`Shift`+`v`

_¹Double-click on black borders to remove them._  

_²Right-click turns the screen on if it was off, presses BACK otherwise._

_³Only on Android >= 7._

## TODO
[TODO](docs/TODO.md)

## FAQ
[FAQ](docs/FAQ.md)

## DEVELOP
[DEVELOP](docs/DEVELOP.md)

Everyone is welcome to maintain this project and contribute your own code, but please follow these requirements:
1. Please open PRs to the dev branch instead of the master branch
2. Please rebase the original project before opening PRs
3. Please submit PRs on the principle of "small amounts, many times" (one PR for a change is recommended)
4. Please keep the code style consistent with the existing style.

## Build
All the dependencies are provided and it is easy to compile.

### VenMirror
#### Non-Arch Linux Users
1. Set up the Qt development environment with the official Qt installer or third-party tools such as [aqt](https://github.com/miurahr/aqtinstall) on the target platform.
   Qt version bigger than 5.12 is required. (use MSVC 2019 on Windows)
2. Clone the project with `git clone --recurse-submodules https://github.com/WiwiDVLOP/VenMirror.git`
3. For Windows, open CMakeLists.txt with QtCreator and compile Release
4. For Linux, directly run `./ci/linux/build_for_linux.sh "Release"`
Note: compiled artifacts are located at `output/x64/Release`

#### Arch Linux Users
1. Install packages: `base-devel cmake qt5-base qt5-multimedia qt5-x11extras` (`qtcreator` is recommended)
2. Clone the project with `git clone --recurse-submodules https://github.com/WiwiDVLOP/VenMirror.git`
3. Run `./ci/linux/build_for_linux.sh "Release"`

### Scrcpy-Server
1. Set up Android development environment on the target platform
2. Open server project in project root with Android Studio
3. The first time you open it, if you do not have the corresponding version of Gradle, you will be prompted to find Gradle, whether to upgrade Gradle or create it. Select Cancel. After cancelling, you will be prompted to select the location of existing Gradle. Cancel it too and it will download automatically.
4. After compiling the apk, rename it to scrcpy-server and replace QtScrcpy/QtScrcpyCore/src/third_party/scrcpy-server.

## Licence
Since it is based on scrcpy, it uses the same license as scrcpy

    Copyright (C) 2025 Rankun
    
    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at
    
        http://www.apache.org/licenses/LICENSE-2.0
    
    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
