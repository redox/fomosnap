/** @fileoverview Tiny uncompressed rasters for tool tests.
 *
 *  Each row (or column) is a solid, named color. A cut, crop or other
 *  destructive edit is then a statement you can check by eye in a BMP
 *  ("green is gone, yellow now sits on cyan") and by sampling a pixel
 *  ("G channel is the original row index").
 */
#pragma once

#include <QColor>
#include <QImage>
#include <QString>
#include <QVector>

#include <algorithm>

/// Ten high-contrast bands. Index lives in the blue channel (`b = index * 20`)
/// so a sampled pixel names its source band; the other channels stay free
/// for colours you can tell apart by eye in a BMP.
inline const QVector<QColor> kFixtureBands = {
    QColor(220, 50, 0),    // 0 red
    QColor(230, 140, 20),  // 1 orange
    QColor(230, 210, 40),  // 2 yellow
    QColor(40, 180, 60),   // 3 green
    QColor(40, 200, 80),   // 4 cyan
    QColor(50, 90, 100),   // 5 blue
    QColor(140, 70, 120),  // 6 purple
    QColor(200, 60, 140),  // 7 magenta
    QColor(140, 90, 160),  // 8 brown
    QColor(160, 160, 180), // 9 gray
};

inline QColor fixtureBandColor(int index) {
  const int last = static_cast<int>(kFixtureBands.size()) - 1;
  const int i = std::clamp(index, 0, last);
  return kFixtureBands.at(i);
}

inline int fixtureBandIndex(const QColor &color) {
  const int last = static_cast<int>(kFixtureBands.size()) - 1;
  return std::clamp(color.blue() / 20, 0, last);
}

/// `bandCount` horizontal strips, each `bandPx` tall, `width` wide.
inline QImage rowBandImage(int width, int bandPx, int bandCount) {
  QImage image(width, bandPx * bandCount, QImage::Format_RGB32);
  for (int band = 0; band < bandCount; ++band)
    for (int y = 0; y < bandPx; ++y)
      for (int x = 0; x < width; ++x)
        image.setPixelColor(x, band * bandPx + y, fixtureBandColor(band));
  return image;
}

/// `bandCount` vertical strips, each `bandPx` wide, `height` tall.
inline QImage columnBandImage(int height, int bandPx, int bandCount) {
  QImage image(bandPx * bandCount, height, QImage::Format_RGB32);
  for (int band = 0; band < bandCount; ++band)
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < bandPx; ++x)
        image.setPixelColor(band * bandPx + x, y, fixtureBandColor(band));
  return image;
}

/// Stack `keep` (band indices, in order) into a new row-band image.
inline QImage rowBandsKept(int width, int bandPx,
                           const QVector<int> &keep) {
  QImage image(width, bandPx * keep.size(), QImage::Format_RGB32);
  for (int out = 0; out < keep.size(); ++out)
    for (int y = 0; y < bandPx; ++y)
      for (int x = 0; x < width; ++x)
        image.setPixelColor(x, out * bandPx + y,
                            fixtureBandColor(keep.at(out)));
  return image;
}

inline QString describeBandRow(const QImage &image, int y) {
  if (y < 0 || y >= image.height())
    return QStringLiteral("y=%1 out of range").arg(y);
  const QColor c = image.pixelColor(0, y);
  return QStringLiteral("y=%1 band=%2 rgb(%3,%4,%5)")
      .arg(y)
      .arg(fixtureBandIndex(c))
      .arg(c.red())
      .arg(c.green())
      .arg(c.blue());
}

/// Pixel-identical compare with the first mismatch named by band.
inline bool sameRaster(const QImage &actual, const QImage &expected,
                       QString &error) {
  const QImage a = actual.convertToFormat(QImage::Format_RGB32);
  const QImage e = expected.convertToFormat(QImage::Format_RGB32);
  if (a.size() != e.size()) {
    error = QStringLiteral("size %1x%2, expected %3x%4")
                .arg(a.width())
                .arg(a.height())
                .arg(e.width())
                .arg(e.height());
    return false;
  }
  for (int y = 0; y < a.height(); ++y) {
    for (int x = 0; x < a.width(); ++x) {
      if (a.pixelColor(x, y) == e.pixelColor(x, y))
        continue;
      error = QStringLiteral("mismatch at %1,%2: got %3 expected %4")
                  .arg(x)
                  .arg(y)
                  .arg(describeBandRow(a, y))
                  .arg(describeBandRow(e, y));
      return false;
    }
  }
  return true;
}

inline bool saveBmp(const QImage &image, const QString &path, QString &error) {
  if (image.save(path, "BMP"))
    return true;
  error = QStringLiteral("could not write %1").arg(path);
  return false;
}
