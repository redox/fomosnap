/** @fileoverview Window-manager placement for overlay surfaces, replacing
 *  wlr-layer-shell. macOS has no layer-shell equivalent, so the two things
 *  FOMOsnap actually used it for -- "above everything, take the keyboard" for
 *  the capture overlay and "always visible on every Space" for a pin -- are
 *  expressed directly as NSWindow level and collection behaviour. */
#pragma once

class QWindow;

namespace macwindow {

enum class Level {
  /// Above the menu bar and full-screen apps: the capture overlay.
  Shielding,
  /// Above ordinary windows but below the overlay: a pinned capture.
  Floating,
};

enum class Keyboard {
  /// Never takes focus.
  None,
  /// Takes focus when clicked.
  OnDemand,
  /// Takes focus as soon as it is shown, and holds it: the capture overlay
  /// owns every keystroke while it is up.
  Exclusive,
};

/**
 * Applies overlay placement to an already-created native window. The QWindow
 * MUST have a native handle (call `winId()` first) or this is a no-op.
 * `transparent` gives the window a clear background so the editor's scrim can
 * show the live desktop through it.
 */
void configure(QWindow *window, Level level, Keyboard keyboard,
               bool joinsAllSpaces, bool transparent);

/** Raises the window and gives it the keyboard, bringing the process forward
 *  if another app was frontmost. */
void activate(QWindow *window);

/**
 * Height in points at the top of this window's screen that the display owns
 * rather than the window: the camera housing on a notched MacBook. A
 * full-screen overlay is placed under it, so anything drawn there is hidden
 * behind the notch. 0 on displays without one.
 */
[[nodiscard]] double safeAreaTopInset(QWindow *window);

} // namespace macwindow
