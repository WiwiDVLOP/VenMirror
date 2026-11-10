# VenMirror for macOS

Drag `VenMirror.app` to the Applications folder before opening it.

## "VenMirror is damaged and can't be opened"

VenMirror is an open-source application distributed without an Apple Developer
ID signature. macOS may attach a quarantine attribute to apps downloaded from
the Internet and then show this message. After moving the app to Applications,
open Terminal and run:

```sh
xattr -rd com.apple.quarantine /Applications/VenMirror.app
```

Then open VenMirror normally. This command removes the quarantine attribute
only from the VenMirror app you chose to install.

## Choose the matching package

Use the `mac-x64-Qt5` download on Intel Macs; use the `mac-arm64-Qt6` download
on Apple Silicon Macs.
