#pragma once

#include <QString>

/** Runs a detached pinned-image layer using the current FOMOsnap process. */
[[nodiscard]] int runPinnedCapture(const QString &path);
