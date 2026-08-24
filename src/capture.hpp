/** @fileoverview Declares screenshot capture, rendering, and output types. */
#pragma once

#include "cut.hpp"

#include <cstdint>
#include <memory>

#include <QPainterPath>
#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QVector>

class QFont;
class QPainter;

struct MonitorInfo {
  /// Human-readable display name, as macOS localizes it ("Built-in Retina
  /// Display"). Shown in logs; `displayId` is what addresses the hardware.
  QString name;
  /// CGDirectDisplayID of the display. 0 when unknown.
  quint32 displayId = 0;
  QRect geometry;
  QSize pixelSize;
  qreal scale = 1.0;
  /// Always 0 on macOS: Spaces have no enumerable identity, and window
  /// discovery already returns only the current Space.
  int workspaceId = 0;
};

struct WindowTarget {
  QRect rect;
  QString stableId;
  QString title;
  /** Owning application name (e.g. `Safari`, `Ghostty`). */
  QString appClass;
};

struct CaptureData {
  MonitorInfo monitor;
  QImage source;
  /** Logical size the native source image is presented at. */
  QSize previewSize;
  QVector<WindowTarget> windows;
};

enum class BackgroundStyle { None, Slate, Aurora, Sunset, Lagoon, Violet };
enum class QuickOutputMode { None, Copy, Save, Both };

enum class SpotlightShape { Ellipse, Rectangle, RoundedRectangle };
enum class RedactionStyle { Solid, Pixelate };
enum class TextBackground { Plain, Pill, Outline };
enum class TextFont { Neucha, JetBrainsMono, InterDisplay };

struct Annotation {
  enum class Kind {
    Arrow,
    Line,
    Freehand,
    Highlighter,
    Marker,
    Rectangle,
    Ellipse,
    Text,
    Redaction,
    Spotlight
  };

  Kind kind = Kind::Arrow;
  QPointF start;
  QPointF end;
  QString text;
  QColor color;
  qreal size = 4.0;
  int number = 0;
  QVector<QPointF> points;
  bool filled = false;
  qreal cornerRadius = 0.0;
  RedactionStyle redactionStyle = RedactionStyle::Pixelate;
  qreal magnification = 2.0;
  SpotlightShape spotlightShape = SpotlightShape::Ellipse;
  quint32 redactionSeed = 0;
  TextBackground textBackground = TextBackground::Pill;
  /// Typeface is a layer property so reopened and duplicated labels keep it.
  TextFont textFont = TextFont::Neucha;
  quint64 id = 0;

  bool operator==(const Annotation &) const = default;
};

struct Operation {
  enum class Type { Crop, Background, Annotate, Patch, Delete, Cut };

  Type type = Type::Annotate;
  QRectF crop;
  BackgroundStyle background = BackgroundStyle::None;
  bool imageShadow = true;
  QVector<Annotation> annotations;
  QVector<quint64> ids;
  CutOp cut;

  bool operator==(const Operation &) const = default;
};

struct OperationLog {
  QVector<Operation> ops;
  int index = 0;
  quint64 nextId = 1;
  int nextMarker = 1;
  /// Logical size the source was presented at when the log was written. Op
  /// coordinates live in that space, so a source captured on a scaled
  /// monitor reopens at the same scale. Invalid when unknown.
  QSize previewSize;

  bool operator==(const OperationLog &) const = default;
};

enum class AnnotationLayer { Redaction, Default };

[[nodiscard]] constexpr AnnotationLayer annotationLayer(Annotation::Kind kind) {
  return kind == Annotation::Kind::Redaction ? AnnotationLayer::Redaction
                                             : AnnotationLayer::Default;
}

[[nodiscard]] bool loadCaptureFonts();
/** User-facing name for a bundled annotation typeface. */
[[nodiscard]] QString annotationTextFontName(TextFont textFont);
/** Bundled annotation font at Omasnap's logical text size. */
[[nodiscard]] QFont annotationTextFont(qreal size,
                                       TextFont textFont = TextFont::Neucha);
/**
 * Discovers the focused monitor (name, geometry, scale): the display under
 * the pointer. Safe to call on the main thread before the hotkey frame is
 * grabbed.
 */
[[nodiscard]] bool probeFocusedMonitor(MonitorInfo &monitor, QString &error);
/**
 * Captures the focused monitor's pixels onto the given monitor, and its window
 * list when `includeWindows` is set. When `excludeOwnWindows` is true, current
 * FOMOsnap windows are omitted from the frame so a visible overlay cannot
 * become part of the capture. Pure I/O and image work (no GUI objects): safe
 * on any thread.
 */
[[nodiscard]] bool captureMonitorPixels(const MonitorInfo &monitor,
                                        CaptureData &capture,
                                        bool includeWindows, QString &error,
                                        bool excludeOwnWindows = false);
/** Convenience: probes the focused monitor, then captures its pixels. */
[[nodiscard]] bool captureFocusedMonitor(CaptureData &capture,
                                         bool includeWindows, QString &error);
/** Bounds of a text layer's glyph box, or of its readability pill when it
 *  has one; `start` is the baseline origin. */
[[nodiscard]] QRectF annotationTextBounds(const Annotation &annotation);
/** Same pill as `annotationTextBounds`, but for a live editor whose font and
 *  glyph origin are already in widget pixels. */
[[nodiscard]] QRectF textLabelBounds(const QFont &font, const QString &text,
                                     const QPointF &glyphTopLeft,
                                     TextBackground background);
/**
 * Tight, pixel-aligned annotation space containing both the source frame and
 * every annotation's painted extent. The source frame always starts at 0,0;
 * a negative top/left means background was added before it. Keeping this as a
 * derived value avoids translating layers or repeatedly copying the source as
 * the canvas grows and shrinks.
 */
[[nodiscard]] QRectF
captureCanvasRect(const QSizeF &sourceFrameSize,
                  const QVector<Annotation> &annotations);
/** A repeatable capture session for one display (`MonitorInfo::name`): open
 *  once, then grab frames repeatedly. A scroll capture takes many per second
 *  and must not pay a process spawn for each. Frames are captured without the
 *  cursor and returned upright in display pixels. */
class OutputCapture {
public:
  OutputCapture();
  ~OutputCapture();
  OutputCapture(const OutputCapture &) = delete;
  OutputCapture &operator=(const OutputCapture &) = delete;
  [[nodiscard]] bool open(const QString &outputName, QString &error);
  /// Grab the next frame. `timeoutMs` is accepted for call compatibility and
  /// unused: ScreenCaptureKit stills carry their own internal timeout. Returns
  /// false with `error` set on failure; poll sessionStopped() to tell a dead
  /// session from a transient failure and simply retry the rest.
  [[nodiscard]] bool grab(QImage &image, QString &error, int timeoutMs = 2000);
  [[nodiscard]] bool isOpen() const;
  /// True once the session is dead (display gone); further grabs cannot
  /// succeed.
  [[nodiscard]] bool sessionStopped() const;
  /** Pixel size of delivered frames (empty until open). */
  [[nodiscard]] QSize bufferSize() const;
  void close();

private:
  struct State;
  std::unique_ptr<State> state_;
};

[[nodiscard]] bool captureOutputSurface(const MonitorInfo &monitor,
                                        QImage &image, QString &error,
                                        bool excludeOwnWindows = false);
[[nodiscard]] QString operationLogPath(const QString &imagePath);
[[nodiscard]] bool saveOperationLog(const QString &path, const OperationLog &log,
                                    QString &error);
[[nodiscard]] bool loadOperationLog(const QString &path, OperationLog &log,
                                    QString &error);
[[nodiscard]] QString temporaryExportPath();
/** Presents a loaded image as the thing being edited. A log written by the
 *  editor carries the logical size its ops were laid out in; a source taken
 *  on a scaled monitor then opens at that scale rather than at 1:1. */
void describeFileCapture(CaptureData &capture, QImage image,
                         const OperationLog &log);
[[nodiscard]] QImage renderCapture(const CaptureData &capture,
                                   const QRectF &selection,
                                   const QVector<Annotation> &annotations,
                                   BackgroundStyle backgroundStyle,
                                   bool imageShadow = true);
/** Loads the current clipboard image. */
[[nodiscard]] bool loadClipboardImage(QImage &image, QString &error);
[[nodiscard]] bool copyPngFileToClipboard(const QString &path, QString &error);
[[nodiscard]] bool copyImageToClipboard(const QImage &image, QString &error);
[[nodiscard]] bool quickOutput(const QImage &image, QuickOutputMode mode,
                               QString &error);
[[nodiscard]] bool copyTextToClipboard(const QString &text, QString &error);
void paintAnnotation(QPainter &painter, const Annotation &annotation);
[[nodiscard]] QPainterPath spotlightPath(const Annotation &annotation);
void paintSpotlights(QPainter &painter, const QImage &source,
                     const QRectF &targetBounds, const QRectF &sourceRect,
                     const QVector<Annotation> &annotations);
/**
 * Paints the default annotation layer (spotlights, then vectors) in selection
 * space. Spotlights sample `redacted`, which must already include the
 * redaction layer so a loupe cannot magnify source pixels.
 */
void paintDefaultLayer(QPainter &painter, const QImage &redacted,
                       const QRectF &logicalBounds,
                       const QVector<Annotation> &annotations);
void paintCaptureBackground(QPainter &painter, const QRectF &bounds,
                            BackgroundStyle backgroundStyle);
/** Paints the app's soft ambient-plus-key shadow around `imageRect`. */
void paintCaptureImageShadow(QPainter &painter, const QRectF &imageRect,
                             qreal scaleX = 1.0, qreal scaleY = 1.0);
/**
 * Renders the selection region at `targetSize` for the redaction layer. The
 * result carries no annotations; callers overlay redactions with
 * applyRedactionsScaled and cache it while the selection is unchanged.
 */
[[nodiscard]] QImage renderSelectionBase(const CaptureData &capture,
                                         const QRectF &selection,
                                         const QSize &targetSize);
/**
 * Paints redaction annotations over a display-resolution selection image. The
 * source image MUST be the exact selection region scaled to `targetSize`;
 * annotations are selection-relative, spanning 0..`selection` size.
 */
QImage applyRedactionsScaled(QImage image, const QVector<Annotation> &redactions,
                             const QRectF &selection, const QSizeF &targetSize);
/** Creates or repairs a private directory owned by the current user. */
[[nodiscard]] bool ensurePrivateDirectory(const QString &path);
/** Returns FOMOsnap's private runtime directory, or empty on failure. */
[[nodiscard]] QString secureRuntimeDirectory();
/**
 * Filename-safe token for a window class: lowercase, `[a-z0-9-]` only,
 * last segment of a reverse-DNS class, at most 24 characters. Empty when
 * nothing usable remains.
 */
[[nodiscard]] QString appFilenameSlug(const QString &appClass);
/**
 * Class of the window covering most of `selection` (preview coordinates),
 * or empty when no window overlaps it.
 */
[[nodiscard]] QString dominantAppClass(const QVector<WindowTarget> &windows,
                                       const QRectF &selection);
/**
 * Moves a finished export into the screenshots directory as
 * `screenshot-<yyyy-MM-dd_HH-mm-ss>[-<appSlug>].png`. The date leads so the
 * folder always sorts chronologically.
 */
[[nodiscard]] QString moveSnapshotToScreenshots(const QString &sourcePath,
                                                QString &error,
                                                const QString &appSlug = {});
[[nodiscard]] QString temporarySnapshotPath();
[[nodiscard]] QString pinnedSnapshotPath(int index);
void prunePinnedSnapshots();
/**
 * Writes `image` into the private runtime directory. `quality` is the Qt PNG
 * quality knob, which maps inversely onto zlib levels: -1 keeps the default
 * level, higher values compress less and encode faster.
 */
/** Saves a pinned snapshot plus a sidecar log recording the logical size,
 *  so editing the pin later reopens at the captured scale. */
[[nodiscard]] bool savePinnedSnapshot(const QImage &image, const QString &path,
                                      const QSize &logicalSize, QString &error);
[[nodiscard]] bool saveTemporarySnapshot(const QImage &image, QString path,
                                         QString &error, int quality = -1);
[[nodiscard]] QString recognizeText(const QImage &image, QString &error);
void sendCaptureNotification(const QString &message,
                             const QString &imagePath = {});
