# Dependencies: learn the current set before adding to it

The dependency list is small on purpose and should stay that way. Before
adding anything — a library, a build tool, an external process — read this
file, then ask whether the thing you need is genuinely absent from it.

## Link-time (build and runtime)

From `CMakeLists.txt`, this is the entire list:

| Dependency | What it's for |
|---|---|
| **Qt6** (Concurrent, Core, Gui, Test, Widgets) | Everything: windowing, painting, the editor UI, the worker-pool threading model ([threading.md](threading.md)), the test harness |
| **System frameworks** (linked only from `src/mac/*.mm`) | ScreenCaptureKit, Vision, UserNotifications, Carbon, ApplicationServices, ServiceManagement — see [platform-scope.md](platform-scope.md) |

That's it. No JSON library (Qt's `QJsonDocument` handles the operation log),
no image codec beyond what Qt's own PNG support provides, no HTTP, no
logging framework, no CLI-parsing library beyond `QCommandLineParser`, no
config-file parser beyond `QSettings` (used for the one optional INI file —
see below). No tesseract, no Wayland client, no LayerShellQt.

## Runtime: system frameworks, not subprocesses

The Linux original shells out to `hyprctl`, `wl-copy`, `tesseract`, and
`omarchy-notification-send`. This fork does not. Capture, OCR, clipboard,
notifications, hotkeys, and login-item registration go through the
quarantined `src/mac/` layer and the corresponding system frameworks.
`QProcess::startDetached` is used only to spawn another copy of the same
binary (`--pin`, `--agent` restart), never a third-party CLI.

## Not a dependency: Qt platform themes

The overlay is hand-painted: it opens no dialogs and reads no palette, so it
does not load an external Qt platform theme. Chrome text uses `chromeFont()`
and `chromeMonoFont()` (`src/overlay-chrome.cpp`), which wrap macOS's system
UI and fixed fonts. Do not derive chrome from `QStyle` or `palette()`, and do
not add icon-theme lookup: each is a startup cost with nothing in this
codebase to spend it on.

## The one config file

The optional output/palette INI, read with `QSettings`, exists for exactly
two things people legitimately need to override (screenshot
destination/filename pattern, and preset colors) — not as a general
settings mechanism. See the "minimally configurable" principle in
[AGENTS.md](../AGENTS.md) before adding a new key.

## Evaluating a new dependency

Ask, in order:

1. **Can Qt already do this?** Qt6's modules are broad (concurrency, text
   layout, image I/O). Check before reaching further.
2. **Is this an OS-integration concern?** Then it belongs in `src/mac/`
   against a system framework, not a bundled library or a subprocess.
3. **Does it exist only to support Linux or another OS?** Then it's out of
   scope — see [platform-scope.md](platform-scope.md).
4. **Is it justified anyway?** Then it needs to earn its place in the table
   above, and this file needs to be updated in the same PR.

## Binary size

Everything runs from the one executable inside `FOMOsnap.app`. Every
dependency added here is weight every user carries on every install and
every update. If a feature can be built with what's already linked, that's
the implementation to ship.
