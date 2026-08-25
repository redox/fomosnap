/** @fileoverview NSWindow placement for overlay surfaces. See mac-window.hpp. */

#include "mac-window.hpp"
#include "mac-platform.hpp"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include <QGuiApplication>
#include <QWindow>

// AppKit only wires "become key without activating the app" at NSPanel init.
// Qt creates the panel first, so adding the style bit later needs this to
// update the WindowServer tag or makeKey: still will not deliver Esc.
@interface NSWindow (FOMOsnapKeyFocus)
- (void)_setPreventsActivation:(BOOL)preventsActivation;
@end

namespace {

/// Qt's native handle for a QWindow on macOS is the content NSView, not the
/// window, so every caller has to take this one hop.
[[nodiscard]] NSWindow *nativeWindow(QWindow *window) {
  if (!window)
    return nil;
  // Only the cocoa platform hands back an NSView. Under offscreen or minimal
  // -- which the headless tests use -- a WId is an unrelated integer, and
  // bridging it to an object pointer dereferences garbage.
  if (QGuiApplication::platformName() != QLatin1String("cocoa"))
    return nil;
  const WId handle = window->winId();
  if (!handle)
    return nil;
  // ARC will not take an integer for an object, and Qt hands the view back as
  // an opaque WId, so the bridge cast is the only way across.
  NSView *view = (__bridge NSView *)reinterpret_cast<void *>(handle);
  return view.window;
}

void enableAlwaysMouseTracking(NSView *view) {
  if (!view)
    return;
  // Qt's own area is ActiveInKeyWindow. After Cmd+Tab another app is key, so
  // hover moves stop until we ask for them even when inactive.
  static NSString *const kAlwaysTrack = @"fomosnap.alwaysTrack";
  for (NSTrackingArea *area in [view.trackingAreas copy]) {
    if (area.userInfo[kAlwaysTrack])
      [view removeTrackingArea:area];
  }
  NSTrackingArea *area = [[NSTrackingArea alloc]
      initWithRect:view.bounds
           options:(NSTrackingMouseMoved | NSTrackingActiveAlways |
                    NSTrackingInVisibleRect | NSTrackingMouseEnteredAndExited)
             owner:view
          userInfo:@{kAlwaysTrack : @YES}];
  [view addTrackingArea:area];
}

void stealKeyboard(NSWindow *native) {
  if (!native)
    return;
  // A normal NSWindow can only be key while this process is the active app,
  // and cooperative activation will not grant that from a Carbon hotkey.
  // A nonactivating NSPanel steals key focus at the WindowServer instead,
  // which is how Spotlight-style overlays take Esc without a click.
  if ([native isKindOfClass:[NSPanel class]]) {
    native.hidesOnDeactivate = NO;
    [(NSPanel *)native setBecomesKeyOnlyIfNeeded:NO];
    native.styleMask |= NSWindowStyleMaskNonactivatingPanel;
    if ([native respondsToSelector:@selector(_setPreventsActivation:)])
      [native _setPreventsActivation:YES];
  }
  [native makeKeyAndOrderFront:nil];
  [native makeFirstResponder:native.contentView];
  enableAlwaysMouseTracking(native.contentView);
}

void releaseKeyboard(NSWindow *native) {
  if (!native)
    return;
  [native resignKeyWindow];
}

} // namespace

namespace macwindow {

void configure(QWindow *window, Level level, Keyboard keyboard,
               bool joinsAllSpaces, bool transparent) {
  NSWindow *native = nativeWindow(window);
  if (!native)
    return;

  // CGShieldingWindowLevel is the level the system's own screen-capture UI
  // uses: above the menu bar, the Dock, and full-screen apps. SharingNone
  // keeps ScreenCaptureKit from reading the overlay's backing store: a live
  // scroll grab would otherwise stitch the frozen first screenshot forever.
  native.level = level == Level::Shielding
                     ? static_cast<NSWindowLevel>(CGShieldingWindowLevel())
                     : NSFloatingWindowLevel;
  if (level == Level::Shielding)
    hideFromCapture(window);

  NSWindowCollectionBehavior behavior =
      NSWindowCollectionBehaviorFullScreenAuxiliary |
      NSWindowCollectionBehaviorIgnoresCycle;
  if (joinsAllSpaces)
    behavior |= NSWindowCollectionBehaviorCanJoinAllSpaces |
                NSWindowCollectionBehaviorStationary;
  else
    behavior |= NSWindowCollectionBehaviorMoveToActiveSpace;
  native.collectionBehavior = behavior;

  if (transparent) {
    native.opaque = NO;
    native.backgroundColor = NSColor.clearColor;
    native.hasShadow = NO;
  }

  // An overlay must never be dragged, resized, or minimised out from under
  // the selection it is showing.
  native.movableByWindowBackground = NO;
  native.ignoresMouseEvents = keyboard == Keyboard::None;

  if (keyboard == Keyboard::Exclusive)
    stealKeyboard(native);
}

double safeAreaTopInset(QWindow *window) {
  NSWindow *native = nativeWindow(window);
  NSScreen *screen = native.screen ?: [NSScreen mainScreen];
  if (!screen)
    return 0.0;
  // safeAreaInsets reports the notch on the built-in display and zero
  // everywhere else, which is exactly the distinction that matters here.
  return static_cast<double>(screen.safeAreaInsets.top);
}

void hideFromCapture(QWindow *window) {
  NSWindow *native = nativeWindow(window);
  if (!native)
    return;
  native.level = static_cast<NSWindowLevel>(CGShieldingWindowLevel());
  native.sharingType = NSWindowSharingNone;
}

void setKeyboardGrab(QWindow *window, bool grab) {
  NSWindow *native = nativeWindow(window);
  if (!native)
    return;
  if (grab) {
    stealKeyboard(native);
    native.ignoresMouseEvents = NO;
  } else {
    // Step out of the way entirely: the page under the overlay has to receive
    // both the keyboard and the wheel events aimed at it.
    releaseKeyboard(native);
    mac::becomeAccessoryApp();
    [NSApp deactivate];
  }
  hideFromCapture(window);
}

void activate(QWindow *window) {
  NSWindow *native = nativeWindow(window);
  stealKeyboard(native);
  if (!native)
    return;
  // Qt's show() can order the window in without making it key, because the
  // agent is not the active app. Re-steal on the next turn after that.
  dispatch_async(dispatch_get_main_queue(), ^{
    stealKeyboard(native);
  });
}

} // namespace macwindow
