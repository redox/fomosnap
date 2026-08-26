# Threading: the main thread never blocks

FOMOsnap is a full-screen overlay. The instant it stops painting — even for
one dropped frame — it looks broken, because there is nothing else on
screen to explain the freeze. So the rule is absolute: **the UI thread does
capture, paint, and input handling, and nothing else.** Anything that can
take more than a frame (disk I/O for a full-resolution image, PNG encoding,
Vision OCR, ScreenCaptureKit round trips that wait on a frame) runs off it.

## The pattern

Every background operation in the codebase follows the same shape:

1. Copy the small amount of state the worker needs by value into a lambda
   (`CaptureData`, an image, a path). Qt's implicit sharing makes this
   cheap; it also means the worker never touches `this` while the UI thread
   might be mutating it.
2. `QtConcurrent::run(...)` the lambda on Qt's global thread pool.
3. A `QFutureWatcher` connected on the UI thread picks up the result via a
   queued `finished` signal and applies it — never the other way around.
4. A `busy_`-style flag (or a more specific one) blocks reentrancy while the
   watcher is in flight, and the status pill says what is happening.

`src/editor.hpp` currently has six watchers, each the entry point for
reading its corresponding worker:

| Watcher | Worker does |
|---|---|
| `captureWatcher_` | Reads window/monitor pixels via `captureMonitorPixels` |
| `ocrWatcher_` | Renders the OCR crop and runs Vision |
| `finishWatcher_` | Renders the export, encodes PNG, writes the clipboard, moves the file |
| `snapshotWatcher_` | Writes the crash-recovery working snapshot + operation log |
| `pinWatcher_` | Renders the image for a pinned window |
| `recentsWatcher_` | Lists and decodes thumbnails for the recents shelf |

`src/scroll-capture.cpp` follows the same rule with a plain `QFuture<void>`:
the capture loop (grab → crop → classify → accumulate) runs on a worker
thread so the overlay keeps painting the live page and the mode pills while
frames come in.

## What this buys, concretely

- **OCR**: whole-image or drag-region text recognition runs Vision and
  renders a full-resolution crop, both off the UI thread, with a
  scanning animation over the region so the wait reads as progress rather
  than a hang.
- **Export**: a stitched scroll capture can be 25,000 pixels tall. PNG
  encoding that image is seconds of work — all in `finishWatcher_`'s
  worker. See `CaptureEditor::finish()`.
- **Scroll capture**: reading the screen back after every wheel tick, at
  whatever cadence the page's animation settles at, never stalls painting
  the overlay's own chrome.

## The one documented exception

Before any window exists — single-instance handover in `src/instance-lock.cpp`,
and the instant `--fullscreen --copy`-style quick output path in `main()`
(`quickOutput()`, called before `QGuiApplication::exec()` even runs) — there
is no live, painted surface to keep responsive, so a bounded synchronous
wait is fine. The rule is about not freezing something the user is looking
at; a CLI-style path that exits before showing anything doesn't have that
problem. Don't extend this exception to anything that runs after a window
is visible.

## Self-violations found and fixed

Two places broke this rule despite being written after the pattern was
established, which is worth remembering: the pattern has to be followed on
purpose every time, since nothing enforces it automatically.

- **`CaptureEditor::reopenRecent()`** loaded a shelved capture's full-resolution
  source with a synchronous `QImage::load()` directly in the shelf's click
  handler — for a stitched scroll capture, tens of megapixels, on the UI
  thread. Fixed to decode on the worker pool (`reopenWatcher_`), the same
  shape as every other watcher above.
- **`CaptureEditor::pinSnapshot()`** rendered the pin image on a worker but
  then PNG-encoded and wrote it to disk in the `pinWatcher_::finished` slot —
  back on the UI thread, after the watcher had already proven the async
  shape was easy to reach. Fixed to do the encode+write inside the same
  worker lambda as the render, so the slot only launches the pin process.

## A known violation, not yet fixed

`spawnScrollInjector()` (`src/mac/mac-scroll-inject.mm`) is called
synchronously from the UI thread when auto-scroll starts or Continues
(`ScrollCapturePanel::startCapture`/`continueCapture` in
`src/scroll-capture.cpp`). On macOS that call only checks Accessibility
permission and detaches a thread — it does not spawn `hyprctl` — but it
still returns only after that check. It hasn't been moved to a worker
because the auto-scroll injector is delicate and needs a live
re-verification pass, not just a green `make check`. Fix it the same way
as the two cases above (`QtConcurrent::run` wrapping the whole call, a
small watcher applying the result) when you can test it live.

## Adding new work

If you're adding an operation that touches disk, spawns a process, or does
anything non-trivial with an image, it does not go on the UI thread. Follow
the six existing watchers as templates — the shape (copy in, run, watch,
apply) is the same every time, on purpose, so new code doesn't invent a
seventh way to do it. If a signal needs to fire when the worker is truly
finished, remember `QFutureWatcher::finished` is a queued connection: it
does not race obtaining a "the watcher is running" check made moments
earlier in the same call.

See also [editing-model.md](editing-model.md) for what state a background
render is allowed to read, and [dependencies.md](dependencies.md) for the
frameworks those workers call.
