/** @fileoverview Checks the monitor probe and the capture path around it.
 *  Headless: `FOMOSNAP_TEST_MONITOR` describes the display and
 *  `FOMOSNAP_TEST_CAPTURE` supplies its pixels, so nothing here needs a real
 *  screen or a Screen Recording grant. */
#include "capture-probe-smoke.hpp"

#include "capture.hpp"

#include <QDir>
#include <QImage>
#include <QTemporaryDir>

namespace {

/// Restores the environment whichever way a check exits.
class ScopedCaptureEnvironment {
public:
  ScopedCaptureEnvironment()
      : monitor_(qgetenv("FOMOSNAP_TEST_MONITOR")),
        capture_(qgetenv("FOMOSNAP_TEST_CAPTURE")) {}

  ~ScopedCaptureEnvironment() {
    restore("FOMOSNAP_TEST_MONITOR", monitor_);
    restore("FOMOSNAP_TEST_CAPTURE", capture_);
  }

  ScopedCaptureEnvironment(const ScopedCaptureEnvironment &) = delete;
  ScopedCaptureEnvironment &operator=(const ScopedCaptureEnvironment &) = delete;

private:
  static void restore(const char *name, const QByteArray &value) {
    if (value.isEmpty())
      qunsetenv(name);
    else
      qputenv(name, value);
  }

  QByteArray monitor_;
  QByteArray capture_;
};

} // namespace

bool runCaptureProbeSmoke(QString &error) {
  ScopedCaptureEnvironment environment;

  QTemporaryDir directory;
  if (!directory.isValid()) {
    error = QStringLiteral("Could not create capture-probe directory");
    return false;
  }

  QImage source(600, 400, QImage::Format_RGB32);
  source.fill(QColor(QStringLiteral("#28405c")));
  const QString sourcePath =
      QDir(directory.path()).filePath(QStringLiteral("source.png"));
  if (!source.save(sourcePath, "PNG")) {
    error = QStringLiteral("Could not write the capture-probe source");
    return false;
  }

  // A scaled monitor: the source is native pixels, the preview is points.
  qputenv("FOMOSNAP_TEST_MONITOR", QByteArrayLiteral("TEST:0,0,300,200@2"));
  qputenv("FOMOSNAP_TEST_CAPTURE", sourcePath.toUtf8());

  MonitorInfo monitor;
  if (!probeFocusedMonitor(monitor, error)) {
    if (error.isEmpty())
      error = QStringLiteral("Described monitor was not probed");
    return false;
  }
  if (monitor.name != QStringLiteral("TEST") ||
      monitor.geometry != QRect(0, 0, 300, 200) ||
      monitor.pixelSize != QSize(600, 400) || monitor.scale != 2.0) {
    error = QStringLiteral("Described monitor was parsed incorrectly");
    return false;
  }

  // Callers that never show the overlay skip window discovery entirely.
  CaptureData withoutWindows;
  if (!captureFocusedMonitor(withoutWindows, false, error) ||
      withoutWindows.source.size() != QSize(600, 400) ||
      withoutWindows.previewSize != QSize(300, 200) ||
      !withoutWindows.windows.isEmpty()) {
    if (error.isEmpty())
      error = QStringLiteral("Capture without window discovery was incorrect");
    return false;
  }

  // A source that cannot be read has to fail loudly rather than hand the
  // editor an empty frame.
  qputenv("FOMOSNAP_TEST_CAPTURE",
          QDir(directory.path())
              .filePath(QStringLiteral("missing.png"))
              .toUtf8());
  CaptureData missing;
  QString captureError;
  if (captureFocusedMonitor(missing, false, captureError) ||
      captureError.isEmpty()) {
    error =
        QStringLiteral("An unreadable capture source did not report an error");
    return false;
  }

  // A malformed description is a configuration mistake, not a fallback.
  qputenv("FOMOSNAP_TEST_MONITOR", QByteArrayLiteral("nonsense"));
  MonitorInfo malformed;
  QString malformedError;
  if (probeFocusedMonitor(malformed, malformedError) ||
      malformedError.isEmpty()) {
    error = QStringLiteral("A malformed monitor description was accepted");
    return false;
  }

  return true;
}
