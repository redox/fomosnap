/** @fileoverview The chrome every full-screen overlay wears: the mode badge at
 *  the top, the hotkey guide in the corner, and the status pill along the
 *  bottom. Capture and scroll capture are the same tool in two moods, so they
 *  are drawn by the same code rather than by two that drift apart. */
#pragma once

#include <QColor>
#include <QPair>
#include <QRect>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

class QFont;
class QPainter;

/// Overlay chrome text (toolbar labels, hotkey legend, tooltips): the system
/// UI font at a pixel size, never a Qt platform theme, `QStyle`, or
/// `palette()`.
[[nodiscard]] QFont chromeFont(int pixelSize, bool bold = false);
/// The system UI font at its native size. Callers that draw with a painter's
/// default font can pin this; `main()` leaves Qt/cocoa's application font
/// alone because that is already the system UI face.
[[nodiscard]] QFont chromeDefaultFont();
/// Monospace counterpart for numeric readouts: Menlo, the macOS system
/// fixed font. Qt's FixedFont lookup can resolve to the UI face under the
/// offscreen QPA, so this is pinned rather than taken from the theme.
[[nodiscard]] QFont chromeMonoFont(int pixelSize, bool bold = false);

/// The kinds of capture the tab strip across the top offers, on every
/// overlay. Region and Window are modes of the area overlay, Scroll is the
/// scroll overlay, and Fullscreen acts at once.
enum class CaptureKind { Region, Scroll, Window, Fullscreen };
struct CaptureTab {
  CaptureKind kind;
  QRectF rect;
};
/// Visible height of the tab strip's background, from the top edge (the
/// strip is flush against it) to its rounded bottom — fixed regardless of
/// window size, since only the horizontal layout changes with the surface.
/// Chrome stacked below the strip anchors to this, not a guessed constant.
constexpr qreal kCaptureTabBarBottom = 31.0;
[[nodiscard]] QString captureTabLabel(CaptureKind kind);
/// Tab positions for a surface of `bounds`, hanging off the top edge.
[[nodiscard]] QVector<CaptureTab> captureTabLayout(const QRect &bounds);
/// Index of the tab under `position`, or -1.
[[nodiscard]] int captureTabAt(const QVector<CaptureTab> &tabs,
                               const QPointF &position);
/// Draws the strip; `active` is lit, the tab under `cursor` is hinted.
/// `bounds` is the usable surface (below a notch): the bar may hang off the
/// top of that rect, but it is clipped so it cannot paint into the unsafe
/// area above it.
void drawCaptureTabs(QPainter &painter, const QVector<CaptureTab> &tabs,
                     CaptureKind active, const QPointF &cursor,
                     const QRect &bounds);

/// The badge naming what the overlay is doing, centered at the top, with the ×
/// that leaves it. Returns the whole badge; `closeRect` is the × alone, for
/// hit-testing the click that closes.
QRectF drawModeBadge(QPainter &painter, const QRect &bounds,
                     const QString &label, const QColor &accent,
                     QRectF *closeRect = nullptr);

/// The two-column key guide, pinned to the top-right corner, and moved to the
/// left when the pointer is over it, so it never hides what is underneath.
/// `keepVisible` are points (selected handles) the card must not cover.
void drawHotkeyLegend(QPainter &painter, const QRect &bounds,
                      const QPointF &cursor,
                      const QVector<QPair<QString, QString>> &entries,
                      const QVector<QPointF> &keepVisible = {});

/// The instruction line along the bottom.
void drawStatusPill(QPainter &painter, const QRect &bounds,
                    const QString &text);
