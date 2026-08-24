/** @fileoverview Fast live-input and adjustable post-stroke smoothing. */
#pragma once

#include <array>

#include <QElapsedTimer>
#include <QPointF>
#include <QVector>
#include <QtTypes>

namespace stroke {

inline constexpr int minimumSmoothingLevel = 0;
inline constexpr int maximumSmoothingLevel = 6;
inline constexpr int defaultSmoothingLevel = 3;

/**
 * Small adaptive live-input filter. The five-point history removes pointer
 * chatter while the speed-dependent blend lets quick gestures keep up with
 * the hand. reset() starts a new, independent stroke.
 */
class InputSmoother {
public:
  void reset();
  [[nodiscard]] QPointF update(const QPointF &raw);
  /** Deterministic form used by the smoke suite. */
  [[nodiscard]] QPointF update(const QPointF &raw, qreal elapsedSeconds);

private:
  static constexpr int historyCapacity = 5;
  std::array<QPointF, historyCapacity> history_{};
  QPointF historySum_;
  QPointF smoothed_;
  int historyCount_ = 0;
  int historyNext_ = 0;
  bool hasSmoothed_ = false;
  QElapsedTimer clock_;
};

/**
 * Release-time pen levels: 0 leaves the baseline untouched, 1--2 apply one or
 * two anchored Chaikin passes, and 3--6 progressively simplify with
 * Ramer-Douglas-Peucker before two or three Chaikin passes. The first and last
 * points are always preserved, and strokes shorter than three points
 * (including stored dots) pass through untouched.
 */
[[nodiscard]] QVector<QPointF>
smoothFreehand(const QVector<QPointF> &points,
               int level = defaultSmoothingLevel);

} // namespace stroke
