/** @fileoverview Implements pinned snapshot and slot locking. */
#include "pin-file.hpp"

#include "capture.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

PinSnapshotFile::PinSnapshotFile(QString path)
    : path_(QFileInfo(path).absoluteFilePath()) {
  fd_ = ::open(QFile::encodeName(path_).constData(),
               O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd_ < 0 || ::flock(fd_, LOCK_SH | LOCK_NB) != 0) {
    if (fd_ >= 0)
      ::close(fd_);
    fd_ = -1;
  }
}

PinSnapshotFile::~PinSnapshotFile() {
  if (fd_ < 0)
    return;
  // Only the last holder may unlink. Upgrade SH -> EX; if another pin still
  // holds SH, leave the file for that process.
  const bool lastOwner = !preserve_ && ::flock(fd_, LOCK_EX | LOCK_NB) == 0;
  static const QRegularExpression internalName(
      QStringLiteral("^pin-[1-9][0-9]*-[1-9][0-9]*-[0-9a-f]{16}\\.png$"));
  const QFileInfo fileInfo(path_);
  const QString runtime = secureRuntimeDirectory();
  if (lastOwner && !runtime.isEmpty() && fileInfo.absolutePath() == runtime &&
      internalName.match(fileInfo.fileName()).hasMatch()) {
    QFile::remove(path_);
    QFile::remove(operationLogPath(path_));
  }
  ::close(fd_);
}

bool PinSnapshotFile::isLocked() const { return fd_ >= 0; }

void PinSnapshotFile::preserveForEditor() { preserve_ = true; }

PinSlotLock::PinSlotLock() {
  const QString runtime = secureRuntimeDirectory();
  if (runtime.isEmpty())
    return;
  for (int candidate = 0; candidate < 1024; ++candidate) {
    const QString path = QDir(runtime).filePath(
        QStringLiteral(".pin-slot-%1.lock").arg(candidate));
    const int fd = ::open(QFile::encodeName(path).constData(),
                          O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW,
                          S_IRUSR | S_IWUSR);
    if (fd < 0)
      continue;
    if (::fchmod(fd, S_IRUSR | S_IWUSR) == 0 &&
        ::flock(fd, LOCK_EX | LOCK_NB) == 0) {
      fd_ = fd;
      index_ = candidate;
      return;
    }
    ::close(fd);
  }
}

PinSlotLock::~PinSlotLock() {
  if (fd_ < 0)
    return;
  ::close(fd_);
}

bool PinSlotLock::isLocked() const { return fd_ >= 0; }

int PinSlotLock::index() const { return index_; }
