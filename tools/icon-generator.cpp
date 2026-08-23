/** @fileoverview Draws the FOMOsnap app icon and writes a full .iconset.
 *  Vector, like every other icon in the app: no bitmap asset to keep in sync,
 *  and each size is rendered rather than downscaled, so the 16pt icon is drawn
 *  for 16pt instead of being a blurred 1024. Run `make icon` to regenerate.
 *
 *  The mark is a selection frame with an annotation arrow through it: the two
 *  things FOMOsnap does, in the order it does them. */

#include <QGuiApplication>
#include <QDir>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <cmath>

namespace {

/// macOS draws app icons on a squircle inset from the canvas, with a corner
/// radius near 22.4% of the squircle's own width. Matching it is what makes an
/// icon sit correctly next to the system's.
constexpr qreal kInset = 0.098;
constexpr qreal kRadius = 0.224;

void paintIcon(QPainter &painter, qreal size) {
  painter.setRenderHint(QPainter::Antialiasing, true);

  const qreal inset = size * kInset;
  const QRectF body(inset, inset, size - 2 * inset, size - 2 * inset);
  const qreal radius = body.width() * kRadius;

  QPainterPath squircle;
  squircle.addRoundedRect(body, radius, radius);

  // Deep slate to near-black, the same range the overlay chrome lives in.
  QLinearGradient background(body.topLeft(), body.bottomLeft());
  background.setColorAt(0.0, QColor(QStringLiteral("#243247")));
  background.setColorAt(0.55, QColor(QStringLiteral("#141c28")));
  background.setColorAt(1.0, QColor(QStringLiteral("#0b0f16")));
  painter.fillPath(squircle, background);

  // A single light edge along the top: enough to read as a raised surface
  // without becoming a bevel.
  painter.save();
  painter.setClipPath(squircle);
  QLinearGradient sheen(body.topLeft(), QPointF(body.left(), body.top() + body.height() * 0.5));
  sheen.setColorAt(0.0, QColor(255, 255, 255, 26));
  sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
  painter.fillPath(squircle, sheen);
  painter.restore();

  painter.setPen(QPen(QColor(255, 255, 255, 30), std::max(1.0, size * 0.004)));
  painter.setBrush(Qt::NoBrush);
  painter.drawPath(squircle);

  // The selection frame, drawn as four corner brackets. Brackets survive small
  // sizes far better than a dashed marquee, which turns to grey mush at 16pt.
  const qreal frameInset = size * 0.265;
  const QRectF frame(frameInset, size * 0.30, size - 2 * frameInset,
                     size - 2 * size * 0.30);
  const qreal stroke = size * 0.052;
  const qreal arm = frame.width() * 0.30;
  painter.setPen(QPen(QColor(QStringLiteral("#f2f5f9")), stroke, Qt::SolidLine,
                      Qt::RoundCap, Qt::RoundJoin));

  const QPointF corners[4] = {frame.topLeft(), frame.topRight(),
                              frame.bottomRight(), frame.bottomLeft()};
  const QPointF horizontal[4] = {{arm, 0}, {-arm, 0}, {-arm, 0}, {arm, 0}};
  const QPointF vertical[4] = {{0, arm}, {0, arm}, {0, -arm}, {0, -arm}};
  for (int index = 0; index < 4; ++index) {
    QPainterPath bracket;
    bracket.moveTo(corners[index] + horizontal[index]);
    bracket.lineTo(corners[index]);
    bracket.lineTo(corners[index] + vertical[index]);
    painter.drawPath(bracket);
  }

  // The annotation arrow, in the accent the editor draws arrows with. It is
  // deliberately drawn by hand rather than ruled: a straight diagonal arrow
  // between corner brackets is the system's "enter full screen" glyph, and
  // the curve both tells them apart and matches the handwritten face the
  // editor sets its text in.
  const QColor accent(QStringLiteral("#0a84ff"));
  const QPointF tail(size * 0.360, size * 0.660);
  const QPointF head(size * 0.650, size * 0.372);
  const qreal shaft = size * 0.060;

  // Just off the axis midpoint: a hand's drift, not a swoosh. Pulling the
  // control any further turns the arrow into a refresh symbol.
  const QPointF control(size * 0.468, size * 0.478);
  QPainterPath stem;
  stem.moveTo(tail);
  stem.quadTo(control, head);
  painter.setPen(QPen(accent, shaft, Qt::SolidLine, Qt::RoundCap));
  painter.setBrush(Qt::NoBrush);
  painter.drawPath(stem);

  // The head follows the curve's own exit direction, so it never looks
  // stapled onto the end of the stroke.
  const QPointF exit = head - control;
  const qreal exitLength = std::hypot(exit.x(), exit.y());
  const QPointF direction(exit.x() / exitLength, exit.y() / exitLength);
  const QPointF normal(-direction.y(), direction.x());
  const qreal headLength = size * 0.150;
  const QPointF base = head - direction * headLength;

  const qreal headHalfWidth = size * 0.082;
  QPolygonF arrowHead;
  arrowHead << head << (base + normal * headHalfWidth)
            << (base - normal * headHalfWidth);
  painter.setPen(Qt::NoPen);
  painter.setBrush(accent);
  painter.drawPolygon(arrowHead);
}

[[nodiscard]] bool writeSize(const QString &directory, const QString &name,
                             int pixels) {
  QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  paintIcon(painter, pixels);
  painter.end();
  return image.save(QDir(directory).filePath(name), "PNG");
}

} // namespace

int main(int argc, char **argv) {
  QGuiApplication application(argc, argv);
  const QStringList arguments = QCoreApplication::arguments();
  if (arguments.size() != 2) {
    qWarning("usage: icon-generator <output.iconset directory>");
    return 2;
  }

  const QString directory = arguments.at(1);
  if (!QDir().mkpath(directory)) {
    qWarning("could not create %s", qUtf8Printable(directory));
    return 1;
  }

  // The exact set `iconutil` expects; anything missing makes it refuse.
  const struct {
    const char *name;
    int pixels;
  } entries[] = {
      {"icon_16x16.png", 16},     {"icon_16x16@2x.png", 32},
      {"icon_32x32.png", 32},     {"icon_32x32@2x.png", 64},
      {"icon_128x128.png", 128},  {"icon_128x128@2x.png", 256},
      {"icon_256x256.png", 256},  {"icon_256x256@2x.png", 512},
      {"icon_512x512.png", 512},  {"icon_512x512@2x.png", 1024},
  };
  for (const auto &entry : entries) {
    if (!writeSize(directory, QString::fromLatin1(entry.name), entry.pixels)) {
      qWarning("could not write %s", entry.name);
      return 1;
    }
  }
  return 0;
}
