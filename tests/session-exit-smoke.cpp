/** @fileoverview Process-level checks that FOMOsnap always ends. Everything
 *  else in this suite exercises the editor in-process, which cannot catch a
 *  teardown that deadlocks or a window that outlives its application, so
 *  these drive the real executable and insist it exits within a deadline.
 *
 *  Each child gets its own HOME and FOMOSNAP_TEST_MONITOR, so runtime locks
 *  stay under that home and cannot collide with a resident agent. */
#include "session-exit-smoke.hpp"

#include <QDir>
#include <QFile>
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

/// The agent must go resident without asking for Screen Recording. Asking at
/// launch meant: prompt at login, exit non-zero because the permission is not
/// held, launchd restarts it under KeepAlive, prompt again -- a loop that
/// spammed the user with system dialogs until the job was booted out.
///
/// The child gets a home with no TCC grant of its own; what matters is that it
/// stays up rather than exiting, whichever way the permission resolves.
bool runAgentSurvivesMissingPermission(const QProcessEnvironment &environment,
                                       QString &error) {
  QProcess process;
  process.setProcessEnvironment(environment);
  process.setProgram(QStringLiteral(FOMOSNAP_EXECUTABLE));
  process.setArguments(
      {QStringLiteral("--agent"), QStringLiteral("--hotkey"),
       QStringLiteral("ctrl+alt+shift+f14")});
  process.start();
  if (!process.waitForStarted(5000)) {
    error = QStringLiteral("Could not start the agent: %1")
                .arg(process.errorString());
    return false;
  }

  // Long enough to be past any startup-time permission gate.
  if (process.waitForFinished(2500)) {
    error = QStringLiteral(
                "The agent exited at startup (%1) instead of going resident; a "
                "permission gate there restarts under launchd and prompts "
                "forever")
                .arg(process.exitCode());
    return false;
  }

  ::kill(static_cast<pid_t>(process.processId()), SIGTERM);
  if (!process.waitForFinished(kExitDeadlineMs)) {
    process.kill();
    process.waitForFinished(5000);
    error = QStringLiteral("The agent would not stop after the check");
    return false;
  }
  return true;
}

/// The login item is a user LaunchAgent. It must name --agent explicitly:
/// registering the app bare would launch an ordinary capture at login and put
/// a selection overlay across the screen. launchctl is skipped here; this
/// checks the plist that launchd would be handed.
bool runLoginItemRoundTrip(const QString &home, QString &error) {
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("HOME"), home);
  environment.insert(QStringLiteral("QT_QPA_PLATFORM"),
                     QStringLiteral("offscreen"));
  environment.insert(QStringLiteral("FOMOSNAP_TEST_NO_LAUNCHCTL"),
                     QStringLiteral("1"));

  const QString plistPath =
      QDir(home).filePath(QStringLiteral("Library/LaunchAgents/%1.plist")
                              .arg(QString::fromLatin1(FOMOSNAP_AGENT_LABEL)));

  int exitCode = -1;
  if (!runToExit({QStringLiteral("--install-agent")}, environment, exitCode,
                 error))
    return false;
  if (exitCode != 0) {
    error = QStringLiteral("--install-agent exited %1").arg(exitCode);
    return false;
  }

  QFile plist(plistPath);
  if (!plist.open(QIODevice::ReadOnly)) {
    error = QStringLiteral("--install-agent wrote no plist at %1").arg(plistPath);
    return false;
  }
  const QString contents = QString::fromUtf8(plist.readAll());
  plist.close();
  if (!contents.contains(QStringLiteral("--agent"))) {
    error = QStringLiteral("The login item does not pass --agent, so it would "
                           "capture at login instead of going resident");
    return false;
  }
  if (!contents.contains(QStringLiteral("FOMOsnap.app/Contents/MacOS/"))) {
    error = QStringLiteral("The login item does not point inside the bundle, "
                           "so it would lose the Screen Recording grant");
    return false;
  }
  if (!contents.contains(QStringLiteral("<key>RunAtLoad</key>"))) {
    error = QStringLiteral("The login item would not start at login");
    return false;
  }
  if (!contents.contains(QStringLiteral("<key>KeepAlive</key>\n"
                                        "  <true/>"))) {
    error = QStringLiteral(
        "The login item would not restart the agent after Quit & Reopen");
    return false;
  }

  if (!runToExit({QStringLiteral("--uninstall-agent")}, environment, exitCode,
                 error))
    return false;
  if (exitCode != 0) {
    error = QStringLiteral("--uninstall-agent exited %1").arg(exitCode);
    return false;
  }
  if (QFile::exists(plistPath)) {
    error = QStringLiteral("--uninstall-agent left the plist behind");
    return false;
  }
  return true;
}

/// launchctl tears down an old job asynchronously. A replacement bootstrap
/// can briefly return EIO while that teardown is still in flight, so exercise
/// the refresh path with a fake launchctl that fails the first bootstrap.
bool runLoginItemRefresh(const QString &home, QString &error) {
  const QString fakeLaunchctl =
      QDir(home).filePath(QStringLiteral("launchctl"));
  QFile script(fakeLaunchctl);
  if (!script.open(QIODevice::WriteOnly) ||
      script.write("#!/bin/sh\n"
                   "if [ \"$1\" = bootstrap ] && [ ! -e "
                   "\"$HOME/launchctl-bootstrap-attempted\" ]; then\n"
                   "  : > \"$HOME/launchctl-bootstrap-attempted\"\n"
                   "  exit 5\n"
                   "fi\n"
                   "exit 0\n") < 0 ||
      !script.flush() ||
      !script.setPermissions(QFileDevice::ReadOwner |
                             QFileDevice::WriteOwner |
                             QFileDevice::ExeOwner)) {
    error = QStringLiteral("Could not create the fake launchctl");
    return false;
  }
  script.close();

  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("HOME"), home);
  environment.insert(QStringLiteral("QT_QPA_PLATFORM"),
                     QStringLiteral("offscreen"));
  environment.insert(QStringLiteral("PATH"),
                     home + QDir::listSeparator() +
                         environment.value(QStringLiteral("PATH")));

  int exitCode = -1;
  if (!runToExit({QStringLiteral("--install-agent")}, environment, exitCode,
                 error))
    return false;
  if (exitCode != 0) {
    error = QStringLiteral(
                "--install-agent did not recover from a transient bootstrap "
                "failure (exit %1)")
                .arg(exitCode);
    return false;
  }

  if (!runToExit({QStringLiteral("--uninstall-agent")}, environment, exitCode,
                 error))
    return false;
  if (exitCode != 0) {
    error = QStringLiteral("--uninstall-agent after refresh exited %1")
                .arg(exitCode);
    return false;
  }
  return true;
}

/// Only one agent may run per user. Carbon delivers a hotkey to every process
/// that registered it, so a second agent means two overlays open on one press
/// and the single-instance handover asks the other to give up the screen --
/// the overlay flashes and the capture looks like it did nothing.
bool runSecondAgentDeclines(const QProcessEnvironment &environment,
                            QString &error) {
  QProcess first;
  first.setProcessEnvironment(environment);
  first.setProgram(QStringLiteral(FOMOSNAP_EXECUTABLE));
  first.setArguments({QStringLiteral("--agent"), QStringLiteral("--hotkey"),
                      QStringLiteral("ctrl+alt+shift+f15")});
  first.start();
  if (!first.waitForStarted(5000)) {
    error = QStringLiteral("Could not start the first agent: %1")
                .arg(first.errorString());
    return false;
  }
  // Let it take the lock before the second one looks for it.
  if (first.waitForFinished(1500)) {
    error = QStringLiteral("The first agent exited instead of going resident");
    return false;
  }

  const auto stopFirst = [&first] {
    ::kill(static_cast<pid_t>(first.processId()), SIGTERM);
    if (!first.waitForFinished(kExitDeadlineMs))
      first.kill();
    first.waitForFinished(5000);
  };

  int exitCode = -1;
  QString runError;
  if (!runToExit({QStringLiteral("--agent"), QStringLiteral("--hotkey"),
                  QStringLiteral("ctrl+alt+shift+f15")},
                 environment, exitCode, runError)) {
    stopFirst();
    error = QStringLiteral("The second agent did not exit: %1").arg(runError);
    return false;
  }
  stopFirst();

  // Exit 0 on purpose: "already running" is the desired end state, and a
  // non-zero exit would make launchd restart it forever.
  if (exitCode != 0) {
    error = QStringLiteral("A second agent exited %1, wanted 0").arg(exitCode);
    return false;
  }
  return true;
}

/// --scroll is a real capture session now (the overlay plus the scroll
/// panel). Teardown that only closed the area editor has already hung once;
/// this path has to die on SIGTERM the same way.
bool runScrollSessionExits(const QProcessEnvironment &environment,
                           QString &error) {
  QProcess process;
  process.setProcessEnvironment(environment);
  process.setProgram(QStringLiteral(FOMOSNAP_EXECUTABLE));
  process.setArguments({QStringLiteral("--scroll")});
  process.start();
  if (!process.waitForStarted(5000)) {
    error = QStringLiteral("Could not start --scroll: %1")
                .arg(process.errorString());
    return false;
  }

  if (process.waitForFinished(1500)) {
    error = QStringLiteral("--scroll exited on its own with no input");
    return false;
  }

  ::kill(static_cast<pid_t>(process.processId()), SIGTERM);
  if (!process.waitForFinished(kExitDeadlineMs)) {
    process.kill();
    process.waitForFinished(5000);
    error = QStringLiteral("--scroll ignored SIGTERM for %1 ms")
                .arg(kExitDeadlineMs);
    return false;
  }
  if (process.exitStatus() != QProcess::NormalExit) {
    error = QStringLiteral("--scroll crashed instead of quitting");
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
         runScrollSessionExits(environment, error) &&
         runAgentExits(environment, error) &&
         runAgentSurvivesMissingPermission(environment, error) &&
         runSecondAgentDeclines(environment, error) &&
         runLoginItemRoundTrip(home.path(), error) &&
         runLoginItemRefresh(home.path(), error) &&
         runUsageErrorExits(environment, error);
}
