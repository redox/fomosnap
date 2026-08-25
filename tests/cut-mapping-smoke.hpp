/** @fileoverview Cut-tool mapping: drag this band, those pixels must go. */
#pragma once

#include <QString>

class QApplication;

[[nodiscard]] bool runCutMappingSmoke(QApplication &application,
                                      const QString &outputRoot,
                                      QString &error);
