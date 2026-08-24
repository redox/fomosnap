#pragma once

#include <optional>

#include <QImage>
#include <QPointF>
#include <QSizeF>
#include <QtCore/qtypes.h>

/** A half-open horizontal text band in native source-image pixels. */
class TextBand {
public:
  // Half-open edges are deliberately passed together and named at every call.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  TextBand(qreal top, qreal bottom) : top_(top), bottom_(bottom) {}

  [[nodiscard]] qreal top() const { return top_; }
  [[nodiscard]] qreal bottom() const { return bottom_; }
  [[nodiscard]] qreal center() const { return (top_ + bottom_) / 2.0; }
  [[nodiscard]] qreal height() const { return bottom_ - top_; }

  bool operator==(const TextBand &) const = default;

private:
  qreal top_ = 0.0;
  qreal bottom_ = 0.0;
};

/**
 * Detects the text row nearest `sourcePoint` in a small source-image window.
 *
 * `sourcePixelsPerLogicalPixel` keeps the physical scan size and accepted text
 * heights stable for native, fractional-scale, and HiDPI captures. Returns no
 * band for flat or non-text-like content so callers can retain freehand input.
 */
[[nodiscard]] std::optional<TextBand>
detectTextBand(const QImage &source, const QPointF &sourcePoint,
               const QSizeF &sourcePixelsPerLogicalPixel = QSizeF(1.0, 1.0));
