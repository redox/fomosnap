#include "recent-snaps.hpp"

#include "capture.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QLatin1StringView>
#include <QStringList>

#include <algorithm>

namespace {
// Entries are named by a zero-padded millisecond stamp so a plain name sort is
// a time sort: <stamp>.png (source), <stamp>.json (log), <stamp>.thumb.png.
constexpr QLatin1StringView kThumbSuffix(".thumb.png");

QString entryStem() {
  return QStringLiteral("%1").arg(QDateTime::currentMSecsSinceEpoch(), 16, 10,
                                  QChar('0'));
}

QString stemOf(const QString &thumbName) {
  return thumbName.chopped(kThumbSuffix.size());
}

RecentSnap snapForStem(const QDir &dir, const QString &stem) {
  RecentSnap snap;
  snap.sourcePath = dir.filePath(stem + QStringLiteral(".png"));
  snap.thumbPath = dir.filePath(stem + kThumbSuffix);
  snap.stampMs = stem.toLongLong();
  const QString log = dir.filePath(stem + QStringLiteral(".json"));
  if (QFile::exists(log))
    snap.logPath = log;
  return snap;
}

bool moveFile(const QString &from, const QString &to) {
  if (QFile::rename(from, to))
    return true;
  if (!QFile::copy(from, to))
    return false;
  QFile::remove(from);
  return true;
}

QStringList thumbNamesNewestFirst(const QDir &dir) {
  QStringList names =
      dir.entryList({QStringLiteral("*") + kThumbSuffix}, QDir::Files);
  std::ranges::sort(names, std::greater<>());
  return names;
}
} // namespace

QString recentSnapsDirectory() {
  QString root = qEnvironmentVariable("FOMOSNAP_RECENT_DIR");
  if (root.isEmpty()) {
    const QString state =
        QStandardPaths::writableLocation(QStandardPaths::StateLocation);
    if (state.isEmpty())
      return {};
    root = QDir(state).filePath(QStringLiteral("recent"));
  }
  // Working documents hold whole-monitor pixels, so the shelf is as private
  // as the runtime directory those came from.
  return ensurePrivateDirectory(root) ? QDir::cleanPath(root) : QString();
}

QVector<RecentSnap> listRecentSnaps(bool loadThumbnails) {
  const QString root = recentSnapsDirectory();
  if (root.isEmpty())
    return {};
  const QDir dir(root);
  QVector<RecentSnap> snaps;
  for (const QString &name : thumbNamesNewestFirst(dir)) {
    RecentSnap snap = snapForStem(dir, stemOf(name));
    if (!QFile::exists(snap.sourcePath)) {
      QFile::remove(snap.thumbPath); // orphaned by a failed move; tidy up
      continue;
    }
    if (loadThumbnails) {
      snap.thumbnail.load(snap.thumbPath);
      if (snap.thumbnail.isNull())
        continue;
    }
    snaps.push_back(std::move(snap));
    if (snaps.size() >= kRecentSnapLimit)
      break;
  }
  return snaps;
}

bool recordRecentSnap(const QString &sourcePath, const QString &logPath,
                      const QImage &rendered, QString &error) {
  if (rendered.isNull() || sourcePath.isEmpty() || !QFile::exists(sourcePath)) {
    error = QStringLiteral("Nothing to remember: no working document");
    return false;
  }
  const QString root = recentSnapsDirectory();
  if (root.isEmpty()) {
    error = QStringLiteral("Could not create the recent captures directory");
    return false;
  }
  const QDir dir(root);
  QString stem = entryStem();
  while (QFile::exists(dir.filePath(stem + kThumbSuffix)))
    stem = QStringLiteral("%1").arg(stem.toLongLong() + 1, 16, 10, QChar('0'));
  const RecentSnap snap = snapForStem(dir, stem);

  // Thumbnail first: listing keys off it, and the move of the source (a
  // rename within the same filesystem, normally) is the cheap part.
  const QImage thumb = rendered.scaled(kRecentThumbEdge, kRecentThumbEdge,
                                       Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
  QSaveFile thumbFile(snap.thumbPath);
  thumbFile.setDirectWriteFallback(false);
  if (!thumbFile.open(QIODevice::WriteOnly) ||
      !thumbFile.setPermissions(QFileDevice::ReadOwner |
                                QFileDevice::WriteOwner) ||
      !thumb.save(&thumbFile, "PNG") || !thumbFile.commit()) {
    error = QStringLiteral("Could not write recent capture thumbnail: %1")
                .arg(thumbFile.errorString());
    return false;
  }
  if (!moveFile(sourcePath, snap.sourcePath)) {
    QFile::remove(snap.thumbPath);
    error = QStringLiteral("Could not move working document into %1").arg(root);
    return false;
  }
  if (!logPath.isEmpty() && QFile::exists(logPath))
    moveFile(logPath, dir.filePath(stem + QStringLiteral(".json")));

  const QStringList names = thumbNamesNewestFirst(dir);
  for (qsizetype index = kRecentSnapLimit; index < names.size(); ++index)
    removeRecentSnap(snapForStem(dir, stemOf(names.at(index))));
  return true;
}

void removeRecentSnap(const RecentSnap &snap) {
  QFile::remove(snap.thumbPath);
  QFile::remove(snap.sourcePath);
  if (!snap.logPath.isEmpty())
    QFile::remove(snap.logPath);
  else if (!snap.sourcePath.isEmpty())
    QFile::remove(operationLogPath(snap.sourcePath));
}
