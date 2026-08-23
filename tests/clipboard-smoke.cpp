/** @fileoverview Tests clipboard image loading and copying. The pasteboard is
 *  reached through QClipboard, so this runs headless: the offscreen platform
 *  keeps clipboard data in-process and round-trips it faithfully. */
#include "clipboard-smoke.hpp"

#include "capture.hpp"

#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>

namespace {

/// Publishes exactly the flavours a check wants to see, with no help from
/// QMimeData's own image conversion.
void offer(const QString &mimeType, const QByteArray &payload) {
  auto *data = new QMimeData;
  data->setData(mimeType, payload);
  QGuiApplication::clipboard()->setMimeData(data);
}

[[nodiscard]] QImage sampleImage() {
  QImage image(3, 2, QImage::Format_ARGB32);
  image.fill(Qt::transparent);
  image.setPixelColor(1, 0, QColor(18, 52, 86));
  image.setPixelColor(2, 1, QColor(171, 205, 239));
  return image;
}

[[nodiscard]] QByteArray encodePng(const QImage &image) {
  QByteArray png;
  QBuffer buffer(&png);
  return buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "PNG")
             ? png
             : QByteArray();
}

/** An offered PNG is decoded, pixels intact. */
bool runImageCheck(QString &error) {
  const QByteArray png = encodePng(sampleImage());
  if (png.isEmpty()) {
    error = QStringLiteral("Could not encode the clipboard test image");
    return false;
  }
  offer(QStringLiteral("image/png"), png);

  QImage image;
  if (!loadClipboardImage(image, error))
    return false;
  if (image.size() != QSize(3, 2) ||
      image.pixelColor(1, 0) != QColor(18, 52, 86) ||
      image.pixelColor(2, 1) != QColor(171, 205, 239)) {
    error = QStringLiteral("Clipboard image pixels were not preserved");
    return false;
  }
  return true;
}

/** What FOMOsnap copies is what FOMOsnap can reopen. */
bool runRoundTripCheck(QString &error) {
  const QImage expected = sampleImage();
  QGuiApplication::clipboard()->clear();
  if (!copyImageToClipboard(expected, error))
    return false;

  QImage image;
  if (!loadClipboardImage(image, error))
    return false;
  if (image.size() != expected.size() ||
      image.pixelColor(1, 0) != QColor(18, 52, 86) ||
      image.pixelColor(2, 1) != QColor(171, 205, 239)) {
    error = QStringLiteral("A copied image did not survive the round trip");
    return false;
  }
  return true;
}

/** A text-only clipboard reports a clear failure rather than an empty image. */
bool runTextOnlyCheck(QString &error) {
  offer(QStringLiteral("text/plain;charset=utf-8"),
        QByteArrayLiteral("not an image"));

  QImage image(1, 1, QImage::Format_ARGB32);
  QString clipboardError;
  if (loadClipboardImage(image, clipboardError) || !image.isNull() ||
      !clipboardError.contains(QStringLiteral("image"), Qt::CaseInsensitive)) {
    error = QStringLiteral("Text-only clipboard was not rejected clearly");
    return false;
  }
  return true;
}

/** An image flavour whose bytes are not an image says so, and does not
 *  silently hand back the previous capture. */
bool runUndecodableCheck(QString &error) {
  offer(QStringLiteral("image/png"), QByteArrayLiteral("\x89PNG truncated"));

  QImage image;
  QString clipboardError;
  if (loadClipboardImage(image, clipboardError) || !image.isNull() ||
      !clipboardError.contains(QStringLiteral("decoded"),
                               Qt::CaseInsensitive)) {
    error = QStringLiteral("An undecodable clipboard image was not rejected");
    return false;
  }
  return true;
}

} // namespace

bool runClipboardSmoke(QString &error) {
  const bool passed = runImageCheck(error) && runRoundTripCheck(error) &&
                      runTextOnlyCheck(error) && runUndecodableCheck(error);
  QGuiApplication::clipboard()->clear();
  return passed;
}
