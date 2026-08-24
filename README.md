# FOMOsnap

I had FOMO about [Omarchy](https://omarchy.org) and couldn't switch, so I
brought its screenshot tool over instead.

FOMOsnap is a native macOS screenshot and annotation overlay: a macOS-only port
of [tobi/omasnap](https://github.com/tobi/omasnap), which targets Wayland and
Hyprland. It captures the display under the pointer before showing any window of
its own, so the editor never appears in its own screenshot. Annotations stay
movable, resizable vector layers, and captures export at the display's native
backing resolution.

The annotation editor is upstream's, largely unchanged — which is why this port
was possible at all. Capture, windowing, OCR, clipboard, notifications, and the
global hotkey were rewritten against macOS frameworks.

[![Looping FOMOsnap demonstration](assets/omasnap.gif)](assets/omasnap.mp4)

## Features

- Freeform region, window, full-monitor, and scrolling-region capture modes.
  Scroll capture stitches a tall (or wide) page: you scroll it, or Auto-scroll
  drives the wheel for you (Accessibility permission).
- A pointer-side readout that turns any drag into a ruler: the pointer position
  while the crosshair is idle, then the frame size in native export pixels while a
  region, a hovered window, or a crop handle is being sized.
- Window capture is a crop of the display frame. Overlapping windows stay
  visible; there is no second clean-window recapture.
- Select/move/resize layers, mouse-wheel scaling, and eight external recropping handles.
- Arrows, straight lines, smoothed freehand strokes, translucent highlighter strokes,
  hollow or filled rectangles (optionally rounded) and ellipses, numbered markers,
  editable Neucha text (on a readability pill), and secure redaction with opaque or
  randomized non-spatial mosaic output.
- Per-layer preset or custom colors (including highlighter ink), undo/redo history,
  one-click whole-image or drag-region OCR (the recognized text is shown beside
  the image and copied to the clipboard),
  mesh-gradient backdrops, and rendered drop shadows.
- Cut tool: drag across a band of the image to remove it and collapse the gap, with a
  live preview and dashed seam marker while dragging; annotations shift to follow.
- Pin a finished capture as a bottom-right always-on-top window, launched
  from the same `fomosnap` executable and visible on every Space.
- Crash-resistant working documents under a private
  `~/Library/Application Support/fomosnap/`: the original source image plus a
  sidecar JSON operation log. Undo still works after a crash or `--file`
  reopen. Saving and copying write a normal flattened PNG to the clipboard or
  `~/Pictures/Screenshots`.
- PNG on the pasteboard alongside a native image flavour, so any app can paste
  it, plus timestamped files under `~/Pictures/Screenshots` by default.
- Top chrome clears the camera housing on notched MacBooks.
- Open an image already on the clipboard directly in the annotation editor.
- A recents shelf: the select overlay stacks small cards of the last five captures
  along the right edge; hover to fan them out, click one to reopen it in the editor
  with its layers still editable instead of taking a new screenshot.
- Correct native-pixel export on fractional or integer-scaled monitors.

## Platform scope

**macOS 14 (Sonoma) or newer**, Apple silicon or Intel. The floor is set by
`SCScreenshotManager` and `SMAppService`, both introduced in 14.

| Concern | How it works |
|---|---|
| Screen capture | `ScreenCaptureKit`, one still per capture, cursor excluded |
| Display and window discovery | `SCShareableContent`; "focused" means the display under the pointer |
| Overlay placement | A borderless `NSWindow` at `CGShieldingWindowLevel`, above the menu bar and full-screen apps |
| Pinned captures | A floating window that joins every Space |
| OCR | The `Vision` framework — no tesseract, no language data to install |
| Clipboard | `NSPasteboard`, via Qt |
| Notifications | `UNUserNotificationCenter`, with a thumbnail; click to reopen |
| Global hotkey | Carbon `RegisterEventHotKey`, which needs no Accessibility permission |
| Scroll capture | Live ScreenCaptureKit grabs of the page under the overlay; Auto-scroll posts `CGEvent` wheel events at the region (Accessibility permission) |

Everything Wayland-specific was deleted rather than abstracted, so there is no
Linux build in this fork. Use upstream for that.

### Permissions

FOMOsnap asks for **Screen Recording** the first time it captures. macOS cannot
grant this to a running process, so the first run explains, opens System
Settings, and exits — grant it there and start FOMOsnap again.

**Auto-scroll** also needs **Accessibility**, so FOMOsnap can post wheel events
into the page under the overlay. Manual scroll capture does not. The grant
cannot take effect in a running process either: allow it, then start FOMOsnap
again. The agent never asks for Accessibility at launch.

The permission is attached to the app's **code signature**, not its path. The
default signature is ad-hoc, which has no stable identity: its hash changes with
the binary, so every rebuild or `brew upgrade` looks like a different app and
macOS asks again. That is also why stale FOMOsnap entries pile up in
**System Settings > Privacy & Security > Screen Recording** — they are safe to
remove.

To stop the re-asking, build with a real signing identity:

```bash
security find-identity -v -p codesigning     # what you have
make build FOMOSNAP_CODESIGN_IDENTITY="Developer ID Application: ..."
```

A self-signed certificate made in Keychain Access works too; it only has to be
stable, not trusted by Apple.

### Release signing and notarization

The public Homebrew release must use a **Developer ID Application** certificate
and Apple notarization. Ad-hoc signatures are suitable for local development,
but Gatekeeper blocks them and Screen Recording grants do not survive rebuilds.
The release workflow signs only version tags, so pull requests do not need
Apple credentials.

Before the first signed release:

1. Enroll in the [Apple Developer Program](https://developer.apple.com/programs/)
   and create a **Developer ID Application** certificate under Certificates,
   Identifiers & Profiles. Export the certificate and private key from
   Keychain Access as a password-protected `.p12`.
2. In App Store Connect, create a **Team API key** under Users and Access >
   Integrations > App Store Connect API. Download its `.p8` file immediately;
   Apple provides the private key only once. Record its Key ID and the team's
   Issuer ID.
3. Add these GitHub Actions secrets to the repository:
   `APPLE_CERTIFICATE_P12_BASE64`, `APPLE_CERTIFICATE_PASSWORD`,
   `APPLE_API_KEY_P8_BASE64`, `APPLE_API_KEY_ID`, and
   `APPLE_API_ISSUER_ID`.
4. Add the exact certificate identity as the repository variable
   `FOMOSNAP_CODESIGN_IDENTITY`, for example
   `Developer ID Application: Example, Inc. (ABCDE12345)`. Verify it locally
   with `security find-identity -v -p codesigning`.

Encode the two files without putting either private key in the repository:

```bash
base64 -i FOMOsnap-Developer-ID.p12 | tr -d '\n' | pbcopy
base64 -i AuthKey_KEYID.p8 | tr -d '\n' | pbcopy
```

Paste each result into its matching GitHub secret. The tag workflow imports
the certificate into a temporary keychain, signs with hardened runtime,
submits the app to `notarytool`, staples the ticket, and verifies it with
Gatekeeper before publishing the archive. Create the next version only after
all five secrets and the identity variable are configured; older releases
remain ad-hoc signed and must be replaced by a new release.

The agent never asks for the permission at launch, only when a capture is
actually taken. Asking at launch meant prompting at login, exiting because the
permission was missing, and being restarted by launchd — prompting again,
indefinitely.

## Install

### Homebrew

```bash
brew tap redox/tap
brew install --cask fomosnap
```

The cask installs the latest tagged, prebuilt app. Upgrade it with:

```bash
brew upgrade --cask fomosnap
```

The first cask install installs and starts the resident agent. On later
upgrades, the cask reloads the agent if it is still installed, so the new
binary takes effect immediately. If you ran `fomosnap --uninstall-agent`,
upgrades preserve that choice; run `fomosnap --install-agent` to enable it again.

The cask installs `FOMOsnap.app` in `/Applications` and provides the `fomosnap`
command through a wrapper that still launches the executable inside the bundle.
Screen Recording is granted to the bundle identity, not to a copied binary.

The cask is defined in `packaging/homebrew/fomosnap.rb`; the tap holds a copy.

### From source

Requires the Xcode command-line tools.

```bash
brew install qt ninja
git clone https://github.com/redox/fomosnap.git
cd fomosnap
make install
```

That builds `FOMOsnap.app` and installs it to `/Applications`. Set `PREFIX` to
install elsewhere.

For a command-line entry point, write a wrapper that execs the executable
inside the bundle:

```bash
printf '#!/bin/bash\nexec "/Applications/FOMOsnap.app/Contents/MacOS/FOMOsnap" "$@"\n' \
  | sudo tee /usr/local/bin/fomosnap >/dev/null
sudo chmod +x /usr/local/bin/fomosnap
```

A wrapper rather than a symlink, deliberately. Launched through a symlink, dyld
reports the symlink as the executable path, so `+[NSBundle mainBundle]` finds no
bundle at all — which silently disables notifications and `--install-agent`.
`exec`ing the real path keeps the bundle identity, and with it the Screen
Recording grant.

### The resident agent and its hotkey

macOS has no compositor config to bind a key in, so FOMOsnap can hold one
itself. The agent stays resident with no Dock icon, registers a system-wide
shortcut, and shows the overlay with no launch cost, because Qt is already warm.

**The default shortcut is `Ctrl+Cmd+4`.** `Cmd+Shift+3/4/5` all belong to the
system screenshot tool, so the obvious keys were not available.

```bash
fomosnap --agent                          # foreground, to try it
fomosnap --agent --hotkey cmd+shift+2     # any modifier+key combination
fomosnap --install-agent                  # start it at login
fomosnap --uninstall-agent                # stop starting it at login
```

`FOMOSNAP_HOTKEY` sets the default. The key toggles: press once to open the
overlay, again to dismiss it.

`--install-agent` writes a user LaunchAgent to
`~/Library/LaunchAgents/com.fomosnap.FOMOsnap.agent.plist` and loads it, so the
agent starts at every login and is restored if macOS quits it during a
permission change. `--uninstall-agent` unloads and removes it.

Two details that are load-bearing rather than incidental:

- The plist names `--agent` explicitly. Registering the *app* to launch at login
  instead would start it with no arguments, which means an ordinary capture — a
  selection overlay across the screen the moment you log in.
- It is a plain LaunchAgent, not `SMAppService`. `SMAppService` checks a code
  requirement that an ad-hoc-signed app in a Homebrew keg does not satisfy:
  launchd accepts the registration and then refuses to spawn it, failing with
  `EX_CONFIG`. A plain LaunchAgent works from any location.

After a Homebrew upgrade the cask reloads the LaunchAgent with the new
`/Applications/FOMOsnap.app` executable, so the login item survives version
changes and the running agent is refreshed.

Prefer your own launcher? Skip the agent entirely and bind `fomosnap` in Raycast,
Shortcuts, or Karabiner. Every capture is a normal process launch.

## CLI capture modes

Running without arguments opens freeform region selection:

```bash
fomosnap
```

Explicit starting modes:

```bash
fomosnap --capture-region
fomosnap --capture-window
fomosnap --capture-fullscreen
```

Scroll capture stitches a region that is taller (or wider) than the screen:

```bash
fomosnap --scroll
```

Drag a region, then pick a direction: **Manual ↓ / →** scrolls the page
yourself while FOMOsnap captures each step, and **Auto ↓ / →** scrolls it for
you, one acknowledged notch at a time, stopping when the page stops moving.
The frames are aligned and stitched into one image and opened in the editor,
where `Ctrl`+wheel zooms and the wheel scrolls it.

Compatibility positional names are also accepted:

```bash
fomosnap region
fomosnap windows
fomosnap fullscreen
fomosnap smart       # maps to region selection
```

These options choose what is initially selected; the editor still controls whether the
result is copied, saved, or both.

Quick output skips the annotation editor. Add `--copy` to copy only, `--save` to save
only, or both flags to copy and save. Region and window captures output after selection;
fullscreen captures output immediately. Quick output cannot be combined with `--file`,
`--clipboard`, or `--pin`.

### One instance, toggled by the same hotkey

Only one capture overlay runs at a time, guarded by a lock file in the runtime snapshot
directory. Starting fomosnap while an overlay is open sends the running instance `SIGTERM`,
which it handles with a clean Qt shutdown; the new process then exits without capturing.
Pressing `PRINT` therefore opens the overlay and pressing it again dismisses it.

Every capture invocation dismisses this way, quick output included: `--copy`/`--save`
while an overlay is open closes the overlay and outputs nothing, rather than screenshotting
the overlay that is still on screen.

Editing an existing image is never cancelled this way: `--file`, `--clipboard`, or an
image path stops the running instance, waits up to two seconds for the lock, and opens the
editor on that image. That is how a pin's Edit button and a notification click always land
in the editor.

A lock left behind by a crashed instance is removed and reclaimed. A lock file that cannot
be read or written at all is reported on stderr instead of being mistaken for a running
instance.

Exit codes:

| Code | Meaning |
|---|---|
| `0` | Success, including dismissing a running overlay |
| `1` | Capture, image, or single-instance lock failure |
| `2` | Usage error |

### Edit an existing or clipboard image

Point fomosnap at any readable image and it opens straight into the annotation editor
with the whole image selected, skipping the screen-capture step:

```bash
fomosnap ~/Pictures/Screenshots/screenshot-2026-08-11_10-00-00.png
# or
fomosnap --file /path/to/capture.png
```

To open the image currently on the clipboard:

```bash
fomosnap --clipboard
```

The clipboard must offer readable image data. Text-only clipboard contents return an
error instead of opening an empty editor.

File URLs are accepted too. A saved capture notification's "Click to edit" action launches
`fomosnap` on the finished screenshot, so it can be reopened and re-annotated.

### Recent captures

Every capture finished from the editor (copied, saved, or both) keeps its working
document, source plus operation log, on a shelf of the five most recent under
`~/Library/Preferences/FOMOsnap/fomosnap/State/recent/` (`FOMOSNAP_RECENT_DIR` overrides). The select
overlay shows them as a small stack of cards on the right; hovering fans them out
and clicking one reopens that capture in the editor, undo history intact, in place
of a new screenshot. Finishing a reopened capture replaces its shelf entry.

### Configuration (optional)

FOMOsnap has no settings UI and runs fine with no config at all. If you want to
change where screenshots land or what they are called, create
`~/.config/fomosnap/fomosnap.conf` (INI format); every key is optional:

```ini
[output]
# Where saved screenshots go. Default: ~/Pictures/Screenshots
directory = ~/Pictures/Captures
# Filename pattern, without extension (.png is appended).
# Default: screenshot-{date}_{time}-{app}
filename = screenshot-{date}_{time}-{app}

[colors]
# Up to eight preset colors for the palette, and the initial custom color.
palette = #ff375f, #ff9f0a, #ffd60a, #30d158, #0a84ff, #bf5af2, #000000, #ffffff
custom = #ff375f
```

Filename tokens:

| Token | Expands to |
|---|---|
| `{date}` | `2026-08-23` (yyyy-MM-dd) |
| `{time}` | `14-05-09` (HH-mm-ss) |
| `{app}` | Slug of the app under the selection, e.g. `safari`, `ghostty`, `finder` (from the owning application's name). Empty for fullscreen captures, file edits, and when nothing is known — the separator before or after it is dropped too, so the default pattern gives `screenshot-2026-08-23_14-05-09.png`. |

The default keeps the date first so the folder always sorts chronologically:
`screenshot-2026-08-23_14-05-09-firefox.png`. Anything else in the pattern is
literal text (`screenshot-` is just a string). A name that already exists
gets `-2`, `-3`, … appended.

Environment overrides (`FOMOSNAP_SCREENSHOT_DIR` takes precedence over the config):

```bash
FOMOSNAP_SCREENSHOT_DIR="$HOME/Pictures/Captures" fomosnap
FOMOSNAP_OCR_LANGS="eng+deu" fomosnap
FOMOSNAP_OCR_LANGS="ja-JP+en-US" fomosnap
```

OCR runs on Vision, so there is no language data to install: every language the
system recognizes is already available. `FOMOSNAP_OCR_LANGS` takes BCP-47 tags
(`en-US`, `fr-FR`, `zh-Hans`) joined with `+`, and also accepts the old
tesseract codes (`eng`, `deu`, `jpn`) for the common languages. Defaults to
`eng`.

## Controls

### Capture selection

Tabs across the top of the overlay switch the capture kind: **Region**,
**Scrolling Region**, **Window**, **Fullscreen**. All four are modes of the
same overlay. Scrolling Region selects exactly like Region; once the region is
drawn, the page inside it goes live and the scroll controls appear in place.
The tabs stay up in the editor too: a tab there drops the edit and goes back to
capturing in that mode, and a small **Scroll capture** button under the image
turns the drawn region into a scrolling capture. The keys below do the same
without reaching for the pointer.

| Input | Action |
|---|---|
| Drag | Select a region, with its native pixel size shown at the pointer |
| `Space` | Step through the capture-kind tabs (Region, Scrolling Region, Window) |
| `S` | Toggle scrolling-region mode |
| `R` | Restore the last region drawn this session (same monitor) |
| `SUPER + Arrow` | Move among windows in window mode |
| `Enter` | Capture the highlighted window |
| `Ctrl+A` | Select the full focused monitor (the Fullscreen tab) |
| Hover the right-edge stack | Fan out the five most recent captures; click one to reopen it |
| `Esc` | Dismiss (while selecting; in the editor, `Esc` returns to Select and a second `Esc` closes) |

### Annotation editor

| Input | Action |
|---|---|
| `V` | Select/move/resize layers; drag empty canvas for a marquee; wheel scales the selected layer |
| `A` | Arrow |
| `S` | Spotlight/loupe; press again to cycle ellipse, rectangle, rounded |
| `L` | Straight line |
| `F` | Freehand stroke |
| `I` | Eyedropper in the color popover · sample the image as the custom color |
| `C` | Numbered marker |
| `R` | Rectangle; hover the shape button for rectangle, ellipse, and fill controls; `Alt`+wheel rounds corners |
| `E` | Ellipse; shares the shape submenu and filled/hollow toggle |
| `D` | Redact; press again to toggle randomized pixelation or solid redaction |
| `X` | Cut out a band; drag to preview the crossed-out strip, then release to remove and collapse it |
| `T` | Neucha text on a cream readability pill. Click for a one-line label, or drag a box to give it room for several lines: Enter moves to the next line while there is room and commits on the last one; `Shift+Enter` always adds a line; `Esc` commits too but keeps the label selected, so `Backspace` removes it; clicking away keeps the text; press T again to toggle the pill |
| `O` | Recognize and copy all text in the current image |
| `B` | Cycle backdrop |
| `1`–`8` | Set annotation color; `7` is black and `8` is white |
| Wheel | Scale selected layer, magnify the spotlight under the cursor, or change active tool size (`Alt`+wheel: rectangle corner radius or spotlight border); while just viewing a zoomed capture, scroll it like a document |
| `Shift`+wheel | Scroll a zoomed capture sideways (a wide stitch); never changes the zoom |
| `Ctrl`+wheel · middle-drag | Zoom about the cursor · pan by dragging |
| `+` / `-` / `0` (also with `Ctrl`) | Zoom in / out / fit |
| Hold `Shift` while dragging | Make rectangles, ellipses, and spotlights 1:1; snap lines and arrows to 45°; while dragging a selected layer's handle, keep a rectangle, redaction or spotlight's aspect ratio (lines and arrows: 45°) |
| Hold `Alt` while dragging | Center rectangles, ellipses, and spotlights on the press point; add `Shift` for a centered square/circle |
| `←` `↑` `→` `↓` | Nudge the selected layer 1 px; hold `Shift` for 10 px (a held key is one undo step). With nothing selected, pan a zoomed capture |
| Double-click text · `Enter` on a selected text | Reopen text editing |
| `Delete` | Delete selected layer |
| `Alt+D` | Duplicate selected layer (offset down-left, or away from a nearby edge); the copy becomes the selection |
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z`, `Ctrl+Y` | Redo |
| `Ctrl+C` | Copy PNG only |
| `Ctrl+S` | Save PNG only |
| `Enter` | Copy and save (with a text layer selected: edit it) |
| `P` | Pin the capture on screen and close the editor |
| `Esc` | Return to Select; press again to close |
| Right-click | Return to Select; cancel active drawing |

### Pinned captures

`P` renders the current capture, writes it to a `pin-<pid>-<n>-<random>.png` under
the runtime snapshot directory, and launches the same `fomosnap` executable in
detached pin mode. Active pins stack from the bottom-right and can be dragged
by the image background. The window stays visible on every Space without
compositor window rules. It preserves the image
aspect ratio, with a maximum width of one third of the screen and a maximum height of one
half.

Pinning neither touches the clipboard nor writes to the screenshot directory; it is a
fourth output alongside copy, save, and copy-and-save. `P` closes the editor and releases
the single-instance lock immediately. Pins from separate captures accumulate as independent
processes.

Hover the pin to reveal its controls:

| Input on a pin | Action |
|---|---|
| Edit button | Reopen the full-resolution PNG in FOMOsnap and replace the pin |
| Link button | Copy the source file path |
| Copy button, `Ctrl+C` | Copy the full-resolution PNG |
| Double-wide top-left drag handle | Drag the PNG into a file-capable drop target |
| Wheel | Resize within the screen caps, preserving aspect ratio |
| Close button, `Esc`, middle-click | Close |

Image and path copying go to `NSPasteboard`, which owns the data, so it stays
available after the pin is closed. No font-based symbol set is required; the
controls use the same vector icon renderer as the annotation toolbar.

Creation tools return to Select after one placement without selecting the new layer. In
Select mode, arrows and lines show only their two endpoint handles; other layers show a
selection boundary. The eight blue/white handles outside the image recrop its corners or
edges.

## Development and verification

```bash
make check
```

The smoke executable exercises region/window/fullscreen startup modes, capture selection,
working-document persistence (source plus op-log JSON), annotation tools, undo/redo
replay, vector movement and scaling, text editing, OCR, native-DPI output,
endpoint-only line selection, external crop handles, and the native-pixel
measurement readout on a scaled monitor.

It also runs process-lifetime checks that drive the real executable and fail if
it does not exit within a deadline, covering the teardown and signal paths that
in-process tests cannot reach.

`make icon` redraws `assets/FOMOsnap.icns` from `tools/icon-generator.cpp`; the
icon is vector-drawn at every size rather than downscaled from one bitmap.

`.github/workflows/build-macos.yml` runs the same `make check` build, smoke
suite, and available static-analysis checks on a macOS runner, stages the app
bundle, and uploads a versioned artifact. A `v*` tag also attaches that
artifact to the corresponding GitHub release.

## Acknowledgements

The capture and annotation workflow is inspired by three excellent screenshot tools:

- [Shottr](https://shottr.cc/) — fast region/window capture, OCR, and polished backdrops.
- [Satty](https://github.com/Satty-org/Satty) — a focused, native annotation workflow.
- [Flameshot](https://github.com/flameshot-org/flameshot) — selection-first capture and an
  approachable annotation toolbar.

Thanks to their authors and contributors for establishing the interaction patterns that made
this project possible. FOMOsnap is an independent implementation and is not
affiliated with those projects.

## Project history

FOMOsnap is a fork of [tobi/omasnap](https://github.com/tobi/omasnap), which was
itself extracted with `git filter-repo` from the original Omarchy
system-customization repository. This fork keeps that history and replaces the
Wayland platform layer with a macOS one; the annotation editor is substantially
unchanged, which is why the port was possible at all.

Upstream is where to go for Wayland and Hyprland — this repository will not
carry a Linux build.

The bundled Neucha font is distributed under the SIL Open Font License; its license is in
`assets/OFL.txt` and is installed with the application.
