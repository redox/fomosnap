/** @fileoverview Shared overlay chrome (see overlay-chrome.hpp). */
#include "overlay-chrome.hpp"

#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>

QString captureTabLabel(CaptureKind kind) {
  switch (kind) {
  case CaptureKind::Region:
    return QStringLiteral("REGION");
  case CaptureKind::Scroll:
    return QStringLiteral("SCROLLING REGION");
  case CaptureKind::Window:
    return QStringLiteral("WINDOW");
  case CaptureKind::Fullscreen:
    return QStringLiteral("FULLSCREEN");
  }
  return {};
}

namespace {
QFont captureTabFont() {
  QFont font(QStringLiteral("Noto Sans"));
  font.setBold(true);
  font.setPixelSize(11);
  return font;
}
QColor captureTabAccent(CaptureKind kind) {
  switch (kind) {
  case CaptureKind::Window:
    return QColor(QStringLiteral("#ffd60a"));
  case CaptureKind::Fullscreen:
    return QColor(QStringLiteral("#0a84ff"));
  case CaptureKind::Region:
  case CaptureKind::Scroll:
    break;
  }
  return QColor(QStringLiteral("#30d158"));
}
} // namespace

QVector<CaptureTab> captureTabLayout(const QRect &bounds) {
  static const CaptureKind order[] = {CaptureKind::Region, CaptureKind::Window,
                                      CaptureKind::Scroll,
                                      CaptureKind::Fullscreen};
  const QFontMetricsF metrics(captureTabFont());
  constexpr qreal kPad = 14.0;
  constexpr qreal kGap = 2.0;
  constexpr qreal kHeight = 26.0;
  // A few pixels under the usable top, so the pills sit below a notch
  // rather than against the camera housing.
  constexpr qreal kTop = 5.0;
  QVector<CaptureTab> tabs;
  qreal total = 0.0;
  for (const CaptureKind kind : order) {
    const qreal w = metrics.horizontalAdvance(captureTabLabel(kind)) + 2 * kPad;
    tabs.push_back({kind, QRectF(total, kTop, w, kHeight)});
    total += w + kGap;
  }
  total -= kGap;
  const qreal left = bounds.left() + (bounds.width() - total) / 2.0;
  // Both axes: `bounds` is the surface minus anything the display keeps for
  // itself, so on a notched MacBook the strip hangs off the top of the usable
  // area rather than off the top of the panel, where the camera housing would
  // cover it.
  for (CaptureTab &tab : tabs)
    tab.rect.translate(left, bounds.top());
  return tabs;
}

int captureTabAt(const QVector<CaptureTab> &tabs, const QPointF &position) {
  for (int index = 0; index < tabs.size(); ++index) {
    if (tabs.at(index).rect.adjusted(-2, -6, 2, 6).contains(position))
      return index;
  }
  return -1;
}

void drawCaptureTabs(QPainter &painter, const QVector<CaptureTab> &tabs,
                     CaptureKind active, const QPointF &cursor,
                     const QRect &bounds) {
  if (tabs.isEmpty())
    return;
  // Hangs off the usable top like a tab strip: square at that edge (drawn
  // past it so only the bottom corners round). Clipped to `bounds` so a
  // notched display does not get a slab behind the camera housing.
  QRectF bar = tabs.constFirst().rect.united(tabs.constLast().rect)
                   .adjusted(-5, -30, 5, 5);
  if (bar.top() < bounds.top())
    bar.setTop(bounds.top());
  painter.setPen(QPen(QColor(255, 255, 255, 32), 1));
  painter.setBrush(QColor(18, 18, 22, 235));
  painter.drawRoundedRect(bar, 12, 12);
  painter.setFont(captureTabFont());
  const int hovered = captureTabAt(tabs, cursor);
  for (int index = 0; index < tabs.size(); ++index) {
    const CaptureTab &tab = tabs.at(index);
    painter.setPen(Qt::NoPen);
    if (tab.kind == active) {
      painter.setBrush(captureTabAccent(tab.kind));
      painter.drawRoundedRect(tab.rect, 9, 9);
      painter.setPen(QColor(18, 18, 22));
    } else {
      if (index == hovered) {
        painter.setBrush(QColor(255, 255, 255, 28));
        painter.drawRoundedRect(tab.rect, 9, 9);
      }
      painter.setPen(QColor(255, 255, 255, index == hovered ? 255 : 190));
    }
    painter.drawText(tab.rect, Qt::AlignCenter, captureTabLabel(tab.kind));
  }
}

QRectF drawModeBadge(QPainter &painter, const QRect &bounds,
                     const QString &label, const QColor &accent,
                     QRectF *closeRect) {
  QFont badgeFont(QStringLiteral("Noto Sans"));
  badgeFont.setBold(true);
  badgeFont.setPixelSize(11);
  painter.setFont(badgeFont);
  const QString badge = label + QStringLiteral("  ×");
  const int badgeWidth = painter.fontMetrics().horizontalAdvance(badge) + 24;
  const QRectF badgeRect((bounds.width() - badgeWidth) / 2.0, 12, badgeWidth,
                         32);
  painter.setPen(QPen(QColor(255, 255, 255, 32), 1));
  painter.setBrush(QColor(18, 18, 22, 235));
  painter.drawRoundedRect(badgeRect, 10, 10);
  painter.setPen(accent);
  painter.drawText(badgeRect, Qt::AlignCenter, badge);
  if (closeRect) {
    // The × and a little around it, so the click that closes has a target
    // rather than a pixel.
    const qreal closeWidth =
        painter.fontMetrics().horizontalAdvance(QStringLiteral("×")) + 18;
    *closeRect = QRectF(badgeRect.right() - closeWidth, badgeRect.top(),
                        closeWidth, badgeRect.height());
  }
  return badgeRect;
}

void drawHotkeyLegend(QPainter &painter, const QRect &bounds,
                      const QPointF &cursor,
                      const QVector<QPair<QString, QString>> &entries,
                      const QVector<QPointF> &keepVisible) {
  if (entries.isEmpty())
    return;
  constexpr int columns = 2;
  const int rows = (entries.size() + columns - 1) / columns;
  QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
  font.setPixelSize(11);
  const QFontMetricsF metrics(font);
  constexpr qreal keyGap = 12;    // between a key and what it does
  constexpr qreal columnGap = 24; // between the two columns
  constexpr qreal padding = 12;   // card edge to text
  qreal keyWidth[columns] = {};
  qreal textWidth[columns] = {};
  for (int index = 0; index < entries.size(); ++index) {
    const int column = std::min(index / rows, columns - 1);
    keyWidth[column] = std::max(
        keyWidth[column], metrics.horizontalAdvance(entries.at(index).first));
    textWidth[column] = std::max(
        textWidth[column], metrics.horizontalAdvance(entries.at(index).second));
  }
  qreal columnWidth[columns] = {};
  qreal width = 2 * padding;
  for (int column = 0; column < columns; ++column) {
    if (keyWidth[column] <= 0 && textWidth[column] <= 0)
      continue;
    columnWidth[column] = keyWidth[column] + keyGap + textWidth[column];
    width += columnWidth[column];
    if (column > 0)
      width += columnGap;
  }
  width = std::min(width, bounds.width() - 28.0);
  const qreal height = rows * 19 + 24;
  const QRectF right(bounds.left() + bounds.width() - width - 14,
                     bounds.top() + 14, width, height);
  const QRectF left(bounds.left() + 14, bounds.top() + 14, width, height);
  auto hiddenCount = [&](const QRectF &candidate) {
    int count = 0;
    for (const QPointF &point : keepVisible) {
      if (candidate.contains(point))
        ++count;
    }
    return count;
  };
  const bool cursorWantsLeft =
      right.adjusted(-28, -28, 28, 28).contains(cursor);
  QRectF panel = cursorWantsLeft ? left : right;
  const QRectF other = cursorWantsLeft ? right : left;
  if (hiddenCount(panel) > hiddenCount(other))
    panel = other;
  if (keepVisible.isEmpty() &&
      panel.adjusted(-28, -28, 28, 28).contains(cursor) &&
      other.adjusted(-28, -28, 28, 28).contains(cursor))
    return;

  painter.setPen(QPen(QColor(255, 255, 255, 34), 1));
  painter.setBrush(QColor(13, 15, 20, 224));
  painter.drawRoundedRect(panel, 11, 11);
  painter.setFont(font);
  for (int index = 0; index < entries.size(); ++index) {
    const int column = std::min(index / rows, columns - 1);
    const int row = index % rows;
    qreal x = panel.left() + padding;
    for (int before = 0; before < column; ++before)
      x += columnWidth[before] + columnGap;
    const qreal y = panel.top() + 12 + row * 19;
    painter.setPen(QColor(QStringLiteral("#a9b6cb")));
    painter.drawText(QRectF(x, y, keyWidth[column], 18),
                     Qt::AlignLeft | Qt::AlignVCenter, entries.at(index).first);
    painter.setPen(QColor(QStringLiteral("#f5f5f7")));
    painter.drawText(
        QRectF(x + keyWidth[column] + keyGap, y, textWidth[column], 18),
        Qt::AlignLeft | Qt::AlignVCenter, entries.at(index).second);
  }
}

void drawStatusPill(QPainter &painter, const QRect &bounds,
                    const QString &text) {
  QFont font(QStringLiteral("Noto Sans"));
  font.setPixelSize(13);
  painter.setFont(font);
  const int width = painter.fontMetrics().horizontalAdvance(text) + 28;
  const QRectF pill((bounds.width() - width) / 2.0, bounds.height() - 42.0,
                    width, 30);
  painter.setPen(QPen(QColor(255, 255, 255, 32), 1));
  painter.setBrush(QColor(18, 18, 22, 232));
  painter.drawRoundedRect(pill, 10, 10);
  painter.setPen(Qt::white);
  painter.drawText(pill, Qt::AlignCenter, text);
}
