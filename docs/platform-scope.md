# Platform scope: macOS only

FOMOsnap targets one platform: **macOS 14 or newer.** This is a hard fork of
[tobi/omasnap](https://github.com/tobi/omasnap), which is Wayland/Hyprland.
The editor is shared heritage; everything that touches the system was
replaced. There is no Linux build here, and no abstraction layer kept
around for a platform we do not ship.

## What "macOS only" means concretely

- Display and window discovery goes through ScreenCaptureKit
  (`SCShareableContent`) in `src/mac/mac-screen.mm`. "Focused" means the
  display under the pointer.
- Pixel capture is a ScreenCaptureKit still. The display is grabbed before
  any window of ours exists, so the overlay cannot appear in its own
  screenshot.
- Overlay placement is a borderless `NSWindow` at `CGShieldingWindowLevel`
  (`src/mac/mac-window.mm`), not layer-shell. Pins are floating windows on
  every Space.
- OCR is Vision, not tesseract. Clipboard is `NSPasteboard` through
  `QClipboard`. Notifications are `UNUserNotificationCenter`.
- Auto-scroll injects wheel events with `CGEvent` behind the Accessibility
  permission (`src/mac/mac-scroll-inject.mm`). The event carries its own
  location, so the pointer is not warped.
- Apple headers stay in `src/mac/*.mm`. The rest of the codebase is plain
  C++23 against Qt.

## Contribution policy

A pull request that adds Wayland, X11, Windows, or a second platform
backend is out of scope. Upstream is where to go for Hyprland. If a
shared-editor change is useful here, port the behavior — do not reintroduce
the Linux path it arrived on.

If in doubt: **would a macOS user notice any difference in binary size,
startup time, or dependencies if this PR were reverted?** If yes, and the
change is not for this platform, it does not belong here.

See [dependencies.md](dependencies.md) before adding a library or a
framework.
