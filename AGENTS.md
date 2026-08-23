# Omasnap — Agent Guide

Omasnap is a super fast, native macOS screenshot and annotation overlay. It
captures region, window, or full display, then opens an annotation editor with
vector layers (arrows, lines, freehand, highlighter, rectangles, ellipses,
numbered markers, text, OCR). Finished captures go to the clipboard,
`~/Pictures/Screenshots`, or a pinned always-on-top window.

This is a macOS-only fork of [tobi/omasnap](https://github.com/tobi/omasnap),
which targets Wayland and Hyprland. The editor is shared heritage; everything
that touches the system was replaced.

## Project principles

- **Speed first.** The tool must feel instant: capture, annotate, copy. No
  startup bloat, no settings UI, no wizards. The resident agent exists so the
  overlay costs nothing at press time.
- **macOS only.** Requires macOS 14 or newer. No Wayland, no X11, no Windows,
  and no abstraction layer kept around for a platform we do not ship.
- **No backwards compatibility.** Break keybindings, CLI flags, file formats,
  or internals whenever it keeps the code simpler or the tool faster. Do not
  add compatibility shims, deprecation aliases, or migration code.
- **Native where it counts.** System frameworks over bundled dependencies:
  Vision for OCR rather than tesseract, `UNUserNotificationCenter` for
  notifications, `NSPasteboard` through `QClipboard` for the clipboard. The UI
  stays vector-drawn with the bundled Neucha font and no icon-theme dependency.
- **Single binary.** Everything (capture, editor, pin, agent) runs from the one
  executable inside `Omasnap.app`.
- **The platform layer is quarantined.** Only `src/mac/*.mm` may include an
  Apple header. The rest of the codebase is plain C++23 against Qt.

## Repository layout

| Path | Purpose |
|---|---|
| `src/main.cpp` | CLI parsing, capture sessions, single-instance lock, agent mode |
| `src/mac/mac-platform.hpp` | The whole platform surface: capture, OCR, notifications, hotkey, login item |
| `src/mac/mac-screen.mm` | Display/window discovery and pixel capture via ScreenCaptureKit |
| `src/mac/mac-window.hpp/.mm` | NSWindow placement for overlays, replacing wlr-layer-shell |
| `src/mac/mac-ocr.mm` | OCR through Vision |
| `src/mac/mac-app.mm` | Notifications, Dock policy, login item |
| `src/mac/mac-hotkey.mm` | The system-wide hotkey (Carbon `RegisterEventHotKey`) |
| `src/instance-lock.cpp/.hpp` | Single-instance handover: cancel a running overlay, or stop it and take over |
| `src/capture.cpp/.hpp` | Capture, rendering, output, and source+JSON operation-log persistence |
| `src/editor.cpp/.hpp` | Annotation editor: tools, vector layers, operation-log undo/redo, export |
| `src/pin.cpp/.hpp` | Pinned-capture windows (floating, on every Space) |
| `src/stitch.cpp/.hpp`, `src/auto-capture.cpp/.hpp` | Scroll-capture image assembly, kept for the scroll capture that is not built yet |
| `tools/icon-generator.cpp` | Draws the app icon; `make icon` regenerates `assets/Omasnap.icns` |
| `tests/*-smoke.cpp/.hpp` | Headless Qt Test coverage, including process-lifetime checks that run the real executable |
| `CMakeLists.txt` | Build definition; **the version lives here** (`project(omasnap VERSION ...)`) |

## Permissions

Screen Recording is a TCC permission granted to the app's **code signature**,
not its path. Two consequences worth knowing before debugging a "capture
returns nothing" report:

- The build ad-hoc signs `Omasnap.app` after linking. A rebuild changes the
  signature, so macOS may ask again.
- Never change `OMASNAP_BUNDLE_ID` casually: it is the TCC identity, and
  changing it makes every user re-approve.

Headless tests never touch either: `OMASNAP_TEST_MONITOR` describes a display
(`name:x,y,width,height@scale`) and `OMASNAP_TEST_CAPTURE` supplies its pixels.

## Build and verify

```bash
make check
```

`make check` configures and builds, runs the complete headless offscreen Qt
smoke suite (including simulated region clicks, asynchronous capture,
single-instance handover, and process-lifetime checks that drive the real
executable), then runs `clang-tidy` and `clazy-standalone` when available.
`make build` builds only, `make smoke` runs the behavioural suite, `make run`
opens the overlay once, `make agent` runs the resident agent in the
foreground, and `make install` installs the bundle to `/Applications`.

Always run `make check` after behavioural changes. CI
(`.github/workflows/build-macos.yml`) runs the same build and smoke on every
push and PR.

Dependencies: Xcode command-line tools and `brew install qt ninja`.

### Testing what cannot be faked

The editor is exercised in-process, but two classes of bug escape that and
have already bitten once each:

- **Teardown that deadlocks or crashes.** `tests/session-exit-smoke.cpp` runs
  the real executable and fails if it does not exit within a deadline. Any new
  session or shutdown path belongs there.
- **Platform assumptions under a non-cocoa QPA.** `winId()` is only an
  `NSView` under the `cocoa` platform; the headless suite runs `offscreen`,
  where bridging it segfaults. `macwindow::` guards this centrally — do not
  reintroduce a raw cast.

## Release process

1. Bump `project(omasnap VERSION ...)` in `CMakeLists.txt`.
2. Build and run `make check`.
3. Commit, tag `v<version>`, push main and the tag. The GitHub workflow
   attaches the bundle to the release automatically.

See `README.md` for user-facing features, keybindings, and install
instructions — keep it in sync when behavior changes.
