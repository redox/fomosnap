/** @fileoverview Display discovery, window discovery, and pixel capture
 *  through ScreenCaptureKit. Replaces the Wayland `ext-image-copy-capture`
 *  path: a screenshot is one `SCScreenshotManager` call, and the first still
 *  is taken before the overlay window is ever created. Live grabs during
 *  scroll capture omit this process's windows, so the overlay cannot freeze
 *  every crop as its own backing store. */

#include "mac-platform.hpp"

#include "capture.hpp"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <QImage>

#include <algorithm>
#include <chrono>
#include <mutex>

namespace {

/// Seconds to wait on ScreenCaptureKit's completion handlers. They run on an
/// internal queue, so blocking the calling thread is safe, but a wedged
/// capture daemon must not hang the whole capture.
constexpr int64_t kCaptureTimeoutNs = 5ll * NSEC_PER_SEC;

[[nodiscard]] bool waitFor(dispatch_semaphore_t semaphore) {
  return dispatch_semaphore_wait(
             semaphore, dispatch_time(DISPATCH_TIME_NOW, kCaptureTimeoutNs)) ==
         0;
}

[[nodiscard]] QString describe(NSError *error) {
  return error ? QString::fromNSString(error.localizedDescription)
               : QStringLiteral("no detail reported");
}

/// One capture asks for the shareable content three times: to find the
/// focused display, to build the capture filter, and to list windows. Each
/// ask is a ~15 ms round trip to the capture daemon, so a capture that should
/// feel instant would spend ~45 ms re-learning what it already knew.
///
/// The window is deliberately shorter than a human double-press: within one
/// capture every lookup hits the cache, while a second press always sees the
/// screen as it is now. The captured pixels are never cached.
constexpr auto kContentCacheWindow = std::chrono::milliseconds(250);

std::mutex g_contentMutex;
SCShareableContent *g_cachedContent = nil;
std::chrono::steady_clock::time_point g_cachedAt;

[[nodiscard]] SCShareableContent *fetchShareableContent(QString &error) {
  __block SCShareableContent *result = nil;
  __block NSError *failure = nil;
  dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
  [SCShareableContent
      getShareableContentExcludingDesktopWindows:YES
                             onScreenWindowsOnly:YES
                               completionHandler:^(SCShareableContent *content,
                                                   NSError *problem) {
                                 result = content;
                                 failure = problem;
                                 dispatch_semaphore_signal(semaphore);
                               }];
  if (!waitFor(semaphore)) {
    error = QStringLiteral("Timed out asking the system for shareable content");
    return nil;
  }
  if (!result) {
    error = QStringLiteral("Could not list displays: %1").arg(describe(failure));
    return nil;
  }
  return result;
}

/// The shareable content set: displays, windows, and applications in one round
/// trip, reused for the rest of the capture it belongs to.
[[nodiscard]] SCShareableContent *shareableContent(QString &error) {
  const std::lock_guard<std::mutex> guard(g_contentMutex);
  const auto now = std::chrono::steady_clock::now();
  if (g_cachedContent && now - g_cachedAt < kContentCacheWindow)
    return g_cachedContent;

  SCShareableContent *fresh = fetchShareableContent(error);
  if (!fresh)
    return nil;
  g_cachedContent = fresh;
  g_cachedAt = now;
  return fresh;
}

/// Backing scale for a display, read from the NSScreen that owns it so it is
/// by construction the same number Qt reports as the device pixel ratio.
[[nodiscard]] qreal displayScale(CGDirectDisplayID displayId) {
  for (NSScreen *screen in [NSScreen screens]) {
    NSNumber *number = screen.deviceDescription[@"NSScreenNumber"];
    if (number && static_cast<CGDirectDisplayID>(number.unsignedIntValue) ==
                      displayId)
      return static_cast<qreal>(screen.backingScaleFactor);
  }
  return 1.0;
}

/// Global pointer position in points, top-left origin: the same space
/// CGDisplayBounds and SCDisplay.frame use, and the space Qt lays screens out
/// in. NSEvent.mouseLocation is bottom-left, so it is deliberately not used.
[[nodiscard]] CGPoint pointerLocation() {
  CGEventRef event = CGEventCreate(nullptr);
  const CGPoint location = event ? CGEventGetLocation(event) : CGPointZero;
  if (event)
    CFRelease(event);
  return location;
}

void fillMonitor(MonitorInfo &monitor, SCDisplay *display) {
  const CGRect frame = display.frame;
  const qreal scale = displayScale(display.displayID);
  monitor.displayId = display.displayID;
  monitor.name = QString::fromNSString(
      [NSString stringWithFormat:@"Display %u", display.displayID]);
  for (NSScreen *screen in [NSScreen screens]) {
    NSNumber *number = screen.deviceDescription[@"NSScreenNumber"];
    if (number &&
        static_cast<CGDirectDisplayID>(number.unsignedIntValue) ==
            display.displayID) {
      monitor.name = QString::fromNSString(screen.localizedName);
      break;
    }
  }
  monitor.geometry = QRect(qRound(frame.origin.x), qRound(frame.origin.y),
                           qRound(frame.size.width), qRound(frame.size.height));
  monitor.scale = scale;
  monitor.pixelSize = QSize(qRound(frame.size.width * scale),
                            qRound(frame.size.height * scale));
  // macOS has no enumerable Space id, and window discovery already returns
  // only the current Space, so nothing needs one.
  monitor.workspaceId = 0;
}

[[nodiscard]] SCDisplay *findDisplay(SCShareableContent *content,
                                     CGDirectDisplayID displayId) {
  for (SCDisplay *display in content.displays) {
    if (display.displayID == displayId)
      return display;
  }
  return nil;
}

[[nodiscard]] NSArray<SCWindow *> *windowsOwnedByThisApp(
    SCShareableContent *content) {
  NSString *bundleId = [[NSBundle mainBundle] bundleIdentifier];
  const pid_t selfPid = [[NSProcessInfo processInfo] processIdentifier];
  NSMutableArray<SCWindow *> *own = [NSMutableArray array];
  for (SCWindow *window in content.windows) {
    SCRunningApplication *owner = window.owningApplication;
    if (!owner)
      continue;
    if (owner.processID == selfPid ||
        (bundleId && [owner.bundleIdentifier isEqualToString:bundleId]))
      [own addObject:window];
  }
  return own;
}

/// Draws a CGImage into a QImage's own buffer. Format_RGB32 is 0xffRRGGBB in
/// native byte order, which on little-endian is BGRX in memory: exactly what
/// `kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little` writes, so this
/// is a straight blit with no per-pixel swizzle.
[[nodiscard]] QImage toQImage(CGImageRef image) {
  const size_t width = CGImageGetWidth(image);
  const size_t height = CGImageGetHeight(image);
  if (width == 0 || height == 0)
    return {};

  QImage result(static_cast<int>(width), static_cast<int>(height),
                QImage::Format_RGB32);
  if (result.isNull())
    return {};
  CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef context = CGBitmapContextCreate(
      result.bits(), width, height, 8,
      static_cast<size_t>(result.bytesPerLine()), colorSpace,
      kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
  CGColorSpaceRelease(colorSpace);
  if (!context)
    return {};
  CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
  CGContextRelease(context);
  return result;
}

[[nodiscard]] SCStreamConfiguration *configurationFor(const MonitorInfo &monitor) {
  SCStreamConfiguration *configuration = [[SCStreamConfiguration alloc] init];
  configuration.width = static_cast<NSInteger>(monitor.pixelSize.width());
  configuration.height = static_cast<NSInteger>(monitor.pixelSize.height());
  configuration.showsCursor = NO;
  configuration.capturesAudio = NO;
  configuration.pixelFormat = kCVPixelFormatType_32BGRA;
  configuration.colorSpaceName = kCGColorSpaceSRGB;
  if (@available(macOS 14.0, *))
    configuration.captureResolution = SCCaptureResolutionBest;
  return configuration;
}

/// One still from a prepared filter. Blocks; safe on any thread.
[[nodiscard]] bool captureStill(SCContentFilter *filter,
                                SCStreamConfiguration *configuration,
                                QImage &image, QString &error) {
  __block CGImageRef captured = nullptr;
  __block NSError *failure = nil;
  dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
  [SCScreenshotManager
      captureImageWithFilter:filter
               configuration:configuration
           completionHandler:^(CGImageRef result, NSError *problem) {
             // CGImageRef is not ARC-managed, and the handler's reference dies
             // with the block, so it has to be retained across the handoff.
             captured = result ? CGImageRetain(result) : nullptr;
             failure = problem;
             dispatch_semaphore_signal(semaphore);
           }];
  if (!waitFor(semaphore)) {
    error = QStringLiteral("Timed out waiting for the screen capture");
    return false;
  }
  if (!captured) {
    error = QStringLiteral("Screen capture returned no image: %1")
                .arg(describe(failure));
    return false;
  }
  image = toQImage(captured);
  CGImageRelease(captured);
  if (image.isNull()) {
    error = QStringLiteral("Could not convert the captured frame");
    return false;
  }
  return true;
}

} // namespace

namespace mac {

bool ensureScreenRecordingAccess(QString &error) {
  // Headless tests describe the display and supply pixels; they must not
  // raise the system prompt or depend on a TCC grant.
  if (!qEnvironmentVariable("FOMOSNAP_TEST_MONITOR").isEmpty())
    return true;
  if (CGPreflightScreenCaptureAccess())
    return true;
  // This raises the system prompt and returns immediately; the permission
  // only takes effect for a fresh launch, so the caller must not retry.
  CGRequestScreenCaptureAccess();
  error = QStringLiteral(
      "FOMOsnap needs Screen Recording permission. Grant it in System Settings "
      "> Privacy & Security > Screen Recording, then start FOMOsnap again.");
  return false;
}

void openScreenRecordingSettings() {
  NSURL *url = [NSURL
      URLWithString:@"x-apple.systempreferences:com.apple.preference.security?"
                    @"Privacy_ScreenCapture"];
  [[NSWorkspace sharedWorkspace] openURL:url];
}

bool focusedDisplay(MonitorInfo &monitor, QString &error) {
  SCShareableContent *content = shareableContent(error);
  if (!content)
    return false;
  if (content.displays.count == 0) {
    error = QStringLiteral("No displays are available to capture");
    return false;
  }

  // "Focused" means the display under the pointer, which is the same rule
  // the system screenshot tool uses and the only one available without a
  // compositor to ask.
  const CGPoint pointer = pointerLocation();
  SCDisplay *chosen = content.displays.firstObject;
  for (SCDisplay *display in content.displays) {
    if (CGRectContainsPoint(display.frame, pointer)) {
      chosen = display;
      break;
    }
  }
  fillMonitor(monitor, chosen);
  return !monitor.geometry.size().isEmpty();
}

bool captureDisplay(const MonitorInfo &monitor, QImage &image, QString &error,
                    bool excludeOwnWindows) {
  // A live grab needs the window list as it is now: the 250 ms content cache
  // can still hold the pre-overlay snapshot, which would exclude nothing and
  // capture the overlay's frozen backing store as the page.
  SCShareableContent *content = excludeOwnWindows ? fetchShareableContent(error)
                                                  : shareableContent(error);
  if (!content)
    return false;
  SCDisplay *display = findDisplay(content, monitor.displayId);
  if (!display) {
    error = QStringLiteral("Display %1 is no longer connected")
                .arg(monitor.displayId);
    return false;
  }
  NSArray<SCWindow *> *excluded =
      excludeOwnWindows ? windowsOwnedByThisApp(content) : @[];
  SCContentFilter *filter =
      [[SCContentFilter alloc] initWithDisplay:display
                             excludingWindows:excluded];
  return captureStill(filter, configurationFor(monitor), image, error);
}

QVector<WindowTarget> windowTargets(const MonitorInfo &monitor) {
  QString error;
  SCShareableContent *content = shareableContent(error);
  if (!content)
    return {};

  NSString *ownBundleId = [[NSBundle mainBundle] bundleIdentifier];
  const QRect monitorRect(QPoint(), monitor.geometry.size());
  QVector<WindowTarget> targets;
  for (SCWindow *window in content.windows) {
    // Layer 0 is the normal window layer: everything above it is menus,
    // panels, and system chrome that nobody wants to screenshot as a window.
    if (window.windowLayer != 0 || !window.isOnScreen)
      continue;
    SCRunningApplication *owner = window.owningApplication;
    if (ownBundleId && owner &&
        [owner.bundleIdentifier isEqualToString:ownBundleId])
      continue;

    const CGRect frame = window.frame;
    QRect rect(qRound(frame.origin.x) - monitor.geometry.x(),
               qRound(frame.origin.y) - monitor.geometry.y(),
               qRound(frame.size.width), qRound(frame.size.height));
    rect = rect.intersected(monitorRect);
    if (rect.isEmpty())
      continue;

    QString appClass =
        owner ? QString::fromNSString(owner.applicationName) : QString();
    QString title = window.title ? QString::fromNSString(window.title) : QString();
    if (title.isEmpty())
      title = appClass.isEmpty() ? QStringLiteral("window") : appClass;
    targets.push_back({rect, QString::number(window.windowID), std::move(title),
                       std::move(appClass)});
  }
  return targets;
}

} // namespace mac

// --- capture.hpp entry points ------------------------------------------------

bool captureOutputSurface(const MonitorInfo &monitor, QImage &image,
                          QString &error) {
  return mac::captureDisplay(monitor, image, error);
}

/// A repeatable capture session for one display. ScreenCaptureKit keeps the
/// capture daemon warm between stills, so re-filtering per grab is cheap and
/// there is no session handshake to amortise the way Wayland needed.
struct OutputCapture::State {
  MonitorInfo monitor;
  bool open = false;
};

OutputCapture::OutputCapture() = default;
OutputCapture::~OutputCapture() = default;

bool OutputCapture::open(const QString &outputName, QString &error) {
  auto state = std::make_unique<State>();
  QString probeError;
  SCShareableContent *content = shareableContent(probeError);
  if (!content) {
    error = probeError;
    return false;
  }
  for (SCDisplay *display in content.displays) {
    MonitorInfo candidate;
    fillMonitor(candidate, display);
    if (candidate.name == outputName) {
      state->monitor = candidate;
      state->open = true;
      break;
    }
  }
  if (!state->open) {
    error = QStringLiteral("No display named %1").arg(outputName);
    return false;
  }
  state_ = std::move(state);
  return true;
}

bool OutputCapture::grab(QImage &image, QString &error, int timeoutMs) {
  Q_UNUSED(timeoutMs);
  if (!state_ || !state_->open) {
    error = QStringLiteral("Capture session is not open");
    return false;
  }
  return mac::captureDisplay(state_->monitor, image, error, true);
}

bool OutputCapture::isOpen() const { return state_ && state_->open; }

bool OutputCapture::sessionStopped() const { return state_ && !state_->open; }

QSize OutputCapture::bufferSize() const {
  return state_ ? state_->monitor.pixelSize : QSize();
}

void OutputCapture::close() { state_.reset(); }
