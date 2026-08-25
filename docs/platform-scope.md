# Platform scope: macOS only, on purpose

FOMOsnap targets one platform: **macOS 14 or newer.** This is not a
starting point we intend to broaden into a portable screenshot tool. It is
a deliberate choice that keeps the code small and lets it use real platform
facilities instead of lowest-common-denominator abstractions.

This repository is a fork of [tobi/omasnap](https://github.com/tobi/omasnap),
which is Wayland and Hyprland only. Shared editor and stitcher code is
heritage; everything that touches the system was replaced. Do not
reintroduce Wayland, Hyprland, X11, Windows, or an abstraction layer kept
around for a platform we do not ship.

## What "macOS only" means concretely

- Display and window discovery, and pixel capture, go through
  ScreenCaptureKit in `src/mac/mac-screen.mm`. Headless tests never touch
  it: `FOMOSNAP_TEST_MONITOR` and `FOMOSNAP_TEST_CAPTURE` supply a fake
  display and its pixels.
- Overlay placement is an `NSWindow` in `src/mac/mac-window.mm`, not
  wlr-layer-shell. `macwindow::` also guards `winId()` so the offscreen QPA
  used by `make check` cannot be bridged as an `NSView`.
- Auto-scroll injects wheel events through `CGEvent` in
  `src/mac/mac-scroll-inject.mm`. The event carries the region's location,
  so the pointer never moves.
- OCR is Vision (`src/mac/mac-ocr.mm`), notifications and the login item
  are `src/mac/mac-app.mm`, and the system-wide hotkey is Carbon
  `RegisterEventHotKey` in `src/mac/mac-hotkey.mm`.
- The resident agent is a user LaunchAgent, not `SMAppService`. The plist
  must always name `--agent`. The agent must never gate startup on Screen
  Recording permission — that check belongs in the capture path.

## The platform layer is quarantined

Only `src/mac/*.mm` may include an Apple header. The rest of the codebase
is plain C++23 against Qt, talking to the platform through
`src/mac/mac-platform.hpp`. A pull request that leaks AppKit, Carbon, or
ScreenCaptureKit into a `.cpp` / `.hpp` is rejected.

## Contribution policy for other platforms

A pull request that adds support for Linux, X11, Windows, or a second
macOS backend is **rejected** if it adds any complexity to the shipping
path: no `#ifdef`, no backend interface, no compile-time flag, no extra
dependency. Shared editor fixes from upstream are welcome when they stay
inside that editor and do not bring Wayland or Hyprland code with them.

If in doubt, the test is: **would a macOS user notice any difference — in
binary size, startup time, code paths exercised, or dependencies — if this
PR were reverted?** If the answer is yes, it's probably out of scope.
