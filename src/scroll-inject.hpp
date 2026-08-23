/** @fileoverview The auto-scroll injection worker: drives the application
 *  under the capture region with real wheel input, one tick per acknowledged
 *  capture cycle.
 *
 *  macOS delivers a scroll event to whatever is under the event's own
 *  location, so unlike the Wayland original this never warps the pointer: the
 *  wheel events are addressed at the region and the user's cursor stays where
 *  they left it. Injection needs the Accessibility permission. */
#pragma once

#include "auto-capture.hpp"
#include "stitch.hpp"

#include <QPoint>
#include <QString>

#include <atomic>
#include <memory>

/// Whether synthetic input is allowed. Does not prompt.
[[nodiscard]] bool scrollInjectionPermitted();

/// Raises the system's Accessibility prompt and opens the settings pane.
/// The permission only takes effect for a fresh launch.
void requestScrollInjectionPermission();

/**
 * Spawns the injection worker thread. `scrollAt` is the global point, in
 * points, that wheel events are addressed to -- the region's bottom-right
 * corner, inset so it stays on the page rather than a neighbouring window.
 *
 * The worker publishes ready cycles on `handshake` (cycle 1 = the unscrolled
 * first frame), scrolls the acknowledged notch count per cycle, and sets
 * `stop` itself on any exit so its death is always observable. Returns false
 * with `error` set when injection is not permitted.
 */
[[nodiscard]] bool spawnScrollInjector(
    std::shared_ptr<std::atomic<bool>> stop,
    std::shared_ptr<stitch::CaptureHandshake> handshake, QPoint scrollAt,
    stitch::Axis axis, QString &error);
