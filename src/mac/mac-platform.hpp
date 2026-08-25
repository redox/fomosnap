/** @fileoverview The macOS platform layer: every call that leaves the process
 *  and touches the system lives behind this header. Implementations are
 *  Objective-C++ under `src/mac/`; the rest of FOMOsnap stays plain C++23 and
 *  never includes an Apple header. */
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class QImage;
class QWindow;
struct MonitorInfo;
struct WindowTarget;

namespace mac {

/**
 * Screen Recording is a TCC permission, so it cannot be granted in-process:
 * the first call raises the system prompt and returns false, and the user
 * must relaunch after granting. Returns true once the permission is held.
 * `error` carries a message worth showing when it is not.
 */
[[nodiscard]] bool ensureScreenRecordingAccess(QString &error);

/** Opens the Screen Recording pane of System Settings. */
void openScreenRecordingSettings();

/**
 * The display under the pointer, which is what "focused monitor" means on a
 * system with no compositor to ask. Fills geometry in points, `pixelSize` in
 * backing pixels, and `scale` from the matching NSScreen so Qt and FOMOsnap
 * always agree on the device pixel ratio.
 */
[[nodiscard]] bool focusedDisplay(MonitorInfo &monitor, QString &error);

/** Captures one display at full backing resolution, without the cursor.
 *  `excludeOwnWindows` drops this process's windows so a live grab (scroll
 *  capture or commit-time selection) sees the desktop under the overlay rather
 *  than the overlay's own backing store. Direct captures before the overlay
 *  exists leave it false, and a pin should still be capturable. */
[[nodiscard]] bool captureDisplay(const MonitorInfo &monitor, QImage &image,
                                  QString &error,
                                  bool excludeOwnWindows = false);

/**
 * On-screen windows of the current Space that intersect `monitor`, in
 * monitor-relative points, front to back. Titles require Screen Recording,
 * which capture already needs.
 */
[[nodiscard]] QVector<WindowTarget> windowTargets(const MonitorInfo &monitor);

/** Recognizes text with Vision. `languages` are BCP-47 tags. */
[[nodiscard]] QString recognizeTextWithVision(const QImage &image,
                                              const QStringList &languages,
                                              QString &error);

/** Posts a user notification, with `imagePath` attached as a thumbnail when
 *  given. Clicking it reopens that file in FOMOsnap. Silently does nothing
 *  when running unbundled, where the notification centre is unavailable. */
void postNotification(const QString &message, const QString &imagePath);

/** Installs the notification-click handler for the process. Safe to call more
 *  than once; only the first call does anything. */
void installNotificationHandler();

/** Hides the Dock icon and the menu bar: FOMOsnap is an overlay, not an app
 *  you switch to. */
void becomeAccessoryApp();

/** Invokes `handler` on the main thread when a different application becomes
 *  frontmost. FOMOsnap itself is ignored: the overlay is a nonactivating
 *  panel and must not count as a switch. A null handler removes the watch.
 *  Only one watch is held. */
void watchFrontmostApplication(std::function<void()> handler);

/** Registers/removes the login item that starts the resident agent. */
[[nodiscard]] bool setLaunchAtLogin(bool enabled, QString &error);
[[nodiscard]] bool launchesAtLogin();

/** Parses "ctrl+cmd+4" into the Carbon keycode/modifier pair the hotkey
 *  registration wants. Returns false on an unparseable spec. */
[[nodiscard]] bool parseHotkey(const QString &spec, quint32 &keyCode,
                               quint32 &modifiers, QString &error);

/** Installs a system-wide hotkey that invokes `handler` on the main thread.
 *  Only one is held at a time; registering again replaces it. */
[[nodiscard]] bool registerHotkey(quint32 keyCode, quint32 modifiers,
                                  void (*handler)(), QString &error);
void unregisterHotkey();

} // namespace mac
