/** @fileoverview Process-level checks that FOMOsnap always ends. Everything
 *  else in this suite exercises the editor in-process, which cannot catch a
 *  teardown that deadlocks or a window that outlives its application, so
 *  these drive the real executable and insist it exits within a deadline.
 *
 *  Each child gets its own HOME, so its runtime directory and single-instance
 *  lock are private to the check and cannot disturb a running FOMOsnap. */
#include "session-exit-smoke.hpp"

#include <QDir>
#include <QImage>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <csignal>

namespace {

/// Generous: this is a deadlock detector, not a benchmark. A healthy run is
/// well under a second, and a hang is forever.
constexpr int kExitDeadlineMs = 15000;

[[nodiscard]] QProcessEnvironment childEnvironment(const QString &home,
                                                   const QString &sourcePath) {
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("HOME"), home);
  environment.insert(QStringLiteral("QT_QPA_PLATFORM"),
                     QStringLiteral("offscreen"));
  // No display probe and no Screen Recording grant: the monitor is described
  // and its pixels come from a file.
  environment.insert(QStringLiteral("FOMOSNAP_TEST_MONITOR"),
                     QStringLiteral("TEST:0,0,400,300@1"));
  environment.insert(QStringLiteral("FOMOSNAP_TEST_CAPTURE"), sourcePath);
  environment.insert(QStringLiteral("FOMOSNAP_SCREENSHOT_DIR"),
                     QDir(home).filePath(QStringLiteral("shots")));
  return environment;
}

/// Runs FOMOsnap to completion. False means it had to be killed.
[[nodiscard]] bool runToExit(const QStringList &arguments,
                             const QProcessEnvironment &environment,
                             int &exitCode, QString &error) {
  QProcess process;
  process.setProcessEnvironment(environment);
  process.setProgram(QStringLiteral(FOMOSNAP_EXECUTABLE));
  process.setArguments(arguments);
  process.start();
  if (!process.waitForStarted(5000)) {
    error = QStringLiteral("Could not start FOMOsnap: %1")
                .arg(process.errorString());
    return false;
  }
  if (!process.waitForFinished(kExitDeadlineMs)) {
    process.kill();
    process.waitForFinished(5000);
    error = QStringLiteral("FOMOsnap %1 did not exit within %2 ms")
                .arg(arguments.join(QLatin1Char(' ')))
                .arg(kExitDeadlineMs);
    return false;
  }
  if (process.exitStatus() != QProcess::NormalExit) {
    error = QStringLiteral("FOMOsnap %1 crashed on the way out")
                .arg(arguments.join(QLatin1Char(' ')));
    return false;
  }
  exitCode = process.exitCode();
  return true;
}

/// A completed quick capture must end the process. This is the path that hung:
/// the overlay closed itself, and tearing the session down from inside the
/// editor's own close notification deadlocked the event loop.
bool runQuickOutputExits(const QProcessEnvironment &environment,
                         QString &error) {
  for (const QString &mode : {QStringLiteral("--save"), QStringLiteral("--copy"),
                              QStringLiteral("--copy")}) {
    int exitCode = -1;
    if (!runToExit({QStringLiteral("--capture-fullscreen"), mode}, environment,
                   exitCode, error))
      return false;
    if (exitCode != 0) {
      error = QStringLiteral("FOMOsnap --capture-fullscreen %1 exited %2")
                  .arg(mode)
                  .arg(exitCode);
      return false;
    }
  }
  return true;
}

/// Editing an image opens the editor and waits for the user, so it has to be
/// asked to stop. SIGTERM is how a second FOMOsnap does that.
bool runTerminationExits(const QProcessEnvironment &environment,
                         const QString &sourcePath, QString &error) {
  QProcess process;
  process.setProcessEnvironment(environment);
  process.setProgram(QStringLiteral(FOMOSNAP_EXECUTABLE));
  process.setArguments({QStringLiteral("--file"), sourcePath});
  process.start();
  if (!process.waitForStarted(5000)) {
    error = QStringLiteral("Could not start the editor: %1")
                .arg(process.errorString());
    return false;
  }

  // Long enough that the editor is up and the overlay mapped, so this is a
  // termination test rather than a startup race.
  if (process.waitForFinished(1500)) {
    error = QStringLiteral("The editor exited on its own with no input");
    return false;
  }

  ::kill(static_cast<pid_t>(process.processId()), SIGTERM);
  if (!process.waitForFinished(kExitDeadlineMs)) {
    process.kill();
    process.waitForFinished(5000);
    error = QStringLiteral("The editor ignored SIGTERM for %1 ms")
                .arg(kExitDeadlineMs);
    return false;
  }
  if (process.exitStatus() != QProcess::NormalExit) {
    error = QStringLiteral("The editor crashed instead of quitting");
    return false;
  }
  return true;
}

/// The resident agent stays up with no overlay, and must still be stoppable:
/// an idle agent that ignores SIGTERM needs SIGKILL, and Ctrl-C in a terminal
/// would not work either.
bool runAgentExits(const QProcessEnvironment &environment, QString &error) {
  QProcess process;
  process.setProcessEnvironment(environment);
  process.setProgram(QStringLiteral(FOMOSNAP_EXECUTABLE));
  // A shortcut nothing else is likely to hold, so a busy machine does not
  // fail the check.
  process.setArguments(
      {QStringLiteral("--agent"), QStringLiteral("--hotkey"),
       QStringLiteral("ctrl+alt+shift+f13")});
  process.start();
  if (!process.waitForStarted(5000)) {
    error =
        QStringLiteral("Could not start the agent: %1").arg(process.errorString());
    return false;
  }

  if (process.waitForFinished(1500)) {
    error = QStringLiteral("The agent exited instead of staying resident (%1)")
                .arg(process.exitCode());
    return false;
  }

  ::kill(static_cast<pid_t>(process.processId()), SIGTERM);
  if (!process.waitForFinished(kExitDeadlineMs)) {
    process.kill();
    process.waitForFinished(5000);
    error = QStringLiteral("An idle agent ignored SIGTERM for %1 ms")
                .arg(kExitDeadlineMs);
    return false;
  }
  if (process.exitStatus() != QProcess::NormalExit) {
    error = QStringLiteral("The agent crashed instead of quitting");
    return false;
  }
  return true;
}

/// Usage errors must not reach the overlay at all.
bool runUsageErrorExits(const QProcessEnvironment &environment,
                        QString &error) {
  int exitCode = -1;
  if (!runToExit({QStringLiteral("--capture-region"),
                  QStringLiteral("--capture-window")},
                 environment, exitCode, error))
    return false;
  if (exitCode != 2) {
    error = QStringLiteral("Conflicting capture modes exited %1, wanted 2")
                .arg(exitCode);
    return false;
  }
  return true;
}

} // namespace

bool runSessionExitSmoke(QString &error) {
  QTemporaryDir home;
  if (!home.isValid()) {
    error = QStringLiteral("Could not create a private home for FOMOsnap");
    return false;
  }

  QImage source(400, 300, QImage::Format_RGB32);
  source.fill(QColor(QStringLiteral("#28405c")));
  const QString sourcePath =
      QDir(home.path()).filePath(QStringLiteral("source.png"));
  if (!source.save(sourcePath, "PNG")) {
    error = QStringLiteral("Could not write the session-exit source image");
    return false;
  }

  const QProcessEnvironment environment =
      childEnvironment(home.path(), sourcePath);
  return runQuickOutputExits(environment, error) &&
         runTerminationExits(environment, sourcePath, error) &&
         runAgentExits(environment, error) &&
         runUsageErrorExits(environment, error);
}
