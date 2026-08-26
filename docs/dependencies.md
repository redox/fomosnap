# Dependencies: learn the current set before adding to it

The dependency list is small on purpose and should stay that way. Before
adding anything — a library, a framework, a build tool — read this file,
then ask whether the thing you need is genuinely absent from it.

## Link-time (build and runtime)

From `CMakeLists.txt`, this is the entire list:

| Dependency | What it's for |
|---|---|
| **Qt6** (Concurrent, Core, Gui, Test, Widgets) 6.8+ | Everything: windowing, painting, the editor UI, the worker-pool threading model ([threading.md](threading.md)), the test harness |
| **AppKit, Carbon, CoreGraphics, CoreMedia, CoreVideo, Foundation, ScreenCaptureKit, UserNotifications, Vision** | The quarantined platform layer in `src/mac/*.mm`: overlays, hotkey, capture, notifications, OCR |

That's it. No JSON library (Qt's `QJsonDocument` handles the operation log),
no image codec beyond Qt's PNG support, no HTTP, no logging framework, no
CLI-parsing library beyond `QCommandLineParser`, no config-file parser
beyond `QSettings` (used for the one optional INI file — see below).

System frameworks over bundled dependencies: Vision rather than tesseract,
`UNUserNotificationCenter` rather than a notification daemon,
`NSPasteboard` through `QClipboard` rather than a clipboard CLI.

## The one config file

`~/.config/fomosnap/fomosnap.conf` is optional INI, read with `QSettings`.
It exists for exactly two things people legitimately need to override
(screenshot destination/filename pattern, and preset colors) — not as a
general settings mechanism. See the "minimally configurable" principle in
[AGENTS.md](../AGENTS.md) before adding a new key.

## Evaluating a new dependency

Ask, in order:

1. **Can Qt already do this?** Qt6's modules are broad (concurrency, text
   layout, image I/O). Check before reaching further.
2. **Can a system framework do this?** Capture, OCR, notifications,
   clipboard, and hotkeys already go through Apple APIs in `src/mac/`.
   A new framework there is cheaper than a bundled library, and it must
   stay behind the platform header.
3. **Does it exist only to support another OS?** Then it's out of scope —
   see [platform-scope.md](platform-scope.md).
4. **Is it justified anyway?** Then it needs to earn its place in the table
   above, and this file needs to be updated in the same PR.

## Binary size

Single binary inside `FOMOsnap.app`. Homebrew's install can bundle Qt;
every other added dependency is weight every install carries. If a feature
can be built with what's already linked, that's the implementation to ship.
