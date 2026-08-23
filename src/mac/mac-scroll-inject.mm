/** @fileoverview Auto-scroll injection through CGEvent. See scroll-inject.hpp.
 *
 *  Replaces the Wayland original's two backends (a uinput kernel mouse and the
 *  wlr virtual-pointer protocol) with one: a scroll event posted to the HID
 *  tap. That also removes the pointer warping the Wayland version needed --
 *  there, wheel events went wherever the real pointer was, so the pointer had
 *  to be parked over the page. Here the event carries its own location. */

#include "scroll-inject.hpp"

#import <ApplicationServices/ApplicationServices.h>
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include <QDebug>

#include <chrono>
#include <optional>
#include <thread>

namespace {

using stitch::Axis;
using stitch::CaptureHandshake;

/// Between injecting a wheel group and announcing its frame as ready. The
/// overlay waits for the page to settle on its own; this only keeps the tick
/// from racing the screenshot that follows it.
constexpr int kScrollSettleMs = 30;

void sleepMs(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

/// Sleeps in slices so a stop request interrupts the settle.
[[nodiscard]] bool sleepUnlessStopped(int ms, const std::atomic<bool> &stop) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (stop.load(std::memory_order_acquire))
      return false;
    sleepMs(5);
  }
  return !stop.load(std::memory_order_acquire);
}

/// True when "natural" scrolling is on, which is the macOS default.
///
/// The window server applies this to synthetic wheel events as well as real
/// ones, so the sign has to follow it or auto-scroll runs backwards. Only the
/// natural-on case is verified by hand; the inversion is the documented
/// meaning of the setting.
[[nodiscard]] bool naturalScrolling() {
  NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
  if ([defaults objectForKey:@"com.apple.swipescrolldirection"] == nil)
    return true;
  return [defaults boolForKey:@"com.apple.swipescrolldirection"];
}

/// Posts one wheel group. `notches` is positive for "further into the page":
/// down for a vertical capture, right for a horizontal one.
[[nodiscard]] bool postScroll(QPoint at, Axis axis, int notches) {
  const int32_t magnitude = naturalScrolling() ? -notches : notches;
  const int32_t vertical = axis == Axis::Vertical ? magnitude : 0;
  const int32_t horizontal = axis == Axis::Vertical ? 0 : magnitude;

  CGEventRef event = CGEventCreateScrollWheelEvent2(
      nullptr, kCGScrollEventUnitLine, axis == Axis::Vertical ? 1 : 2, vertical,
      horizontal, 0);
  if (!event)
    return false;
  // The location is what decides which window receives this, which is why the
  // pointer never has to move.
  CGEventSetLocation(event, CGPointMake(at.x(), at.y()));
  CGEventPost(kCGHIDEventTap, event);
  CFRelease(event);
  return true;
}

void runInjector(const std::shared_ptr<std::atomic<bool>> &stop,
                 const std::shared_ptr<CaptureHandshake> &handshake,
                 QPoint scrollAt, Axis axis) {
  std::uint64_t cycle = handshake->publishReady(); // cycle 1: unscrolled frame
  while (!stop->load(std::memory_order_acquire)) {
    const std::optional<int> notches = handshake->waitForCapture(cycle, *stop);
    if (!notches)
      break;
    if (!postScroll(scrollAt, axis, *notches))
      break;
    if (!sleepUnlessStopped(kScrollSettleMs, *stop))
      break;
    cycle = handshake->publishReady();
  }
  // Always: the overlay watches this to know the worker is gone, and
  // waitForCapture returns empty once it is set.
  stop->store(true, std::memory_order_release);
}

} // namespace

bool scrollInjectionPermitted() { return AXIsProcessTrusted(); }

void requestScrollInjectionPermission() {
  // Prompts, and the grant only applies to a fresh launch.
  NSDictionary *options = @{(__bridge id)kAXTrustedCheckOptionPrompt : @YES};
  AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
  [[NSWorkspace sharedWorkspace]
      openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple."
                                   @"preference.security?Privacy_Accessibility"]];
}

bool spawnScrollInjector(std::shared_ptr<std::atomic<bool>> stop,
                         std::shared_ptr<stitch::CaptureHandshake> handshake,
                         QPoint scrollAt, stitch::Axis axis, QString &error) {
  if (!scrollInjectionPermitted()) {
    error = QStringLiteral(
        "Auto-scroll needs Accessibility permission. Grant FOMOsnap in System "
        "Settings > Privacy & Security > Accessibility, then start it again.");
    return false;
  }

  std::thread([stop, handshake, scrollAt, axis] {
    runInjector(stop, handshake, scrollAt, axis);
  }).detach();
  return true;
}
