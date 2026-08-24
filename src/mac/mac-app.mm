/** @fileoverview Process identity and user notifications: Dock policy, the
 *  login item for the resident agent, and UNUserNotificationCenter delivery
 *  with a thumbnail. Replaces omarchy-notification-send. */

#include "mac-platform.hpp"

#import <AppKit/AppKit.h>
#import <UserNotifications/UserNotifications.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>
#include <QUuid>

#include <unistd.h>

namespace {

/// Notification centre APIs require a bundle identity; running the bare
/// executable out of a build tree has none, and touching them there raises.
[[nodiscard]] bool isBundled() {
  return [[NSBundle mainBundle] bundleIdentifier] != nil;
}

/// Reopens a saved capture by launching a fresh FOMOsnap on it. This is the
/// same code path whether the agent is resident (a second process hands the
/// file over through the instance lock) or the notification relaunched us.
void openInFOMOsnap(NSString *path) {
  NSString *executable = [[NSBundle mainBundle] executablePath];
  if (!executable)
    return;
  NSTask *task = [[NSTask alloc] init];
  task.executableURL = [NSURL fileURLWithPath:executable];
  task.arguments = @[ @"--file", path ];
  [task launchAndReturnError:nil];
}

} // namespace

/// Routes a notification click back into the editor. Installed for the whole
/// process, because the system delivers the response to a relaunched app too.
@interface FOMOsnapNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

@implementation FOMOsnapNotificationDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter *)center
       willPresentNotification:(UNNotification *)notification
         withCompletionHandler:
             (void (^)(UNNotificationPresentationOptions))completionHandler {
  // FOMOsnap is frontmost when it saves, and the banner is the whole point of
  // the confirmation, so present it anyway.
  completionHandler(UNNotificationPresentationOptionBanner);
}

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
    didReceiveNotificationResponse:(UNNotificationResponse *)response
             withCompletionHandler:(void (^)(void))completionHandler {
  NSString *path = response.notification.request.content.userInfo[@"path"];
  if (path.length > 0)
    openInFOMOsnap(path);
  completionHandler();
}
@end

namespace mac {

void installNotificationHandler() {
  if (!isBundled())
    return;
  static FOMOsnapNotificationDelegate *delegate;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    delegate = [[FOMOsnapNotificationDelegate alloc] init];
    UNUserNotificationCenter *center =
        [UNUserNotificationCenter currentNotificationCenter];
    center.delegate = delegate;
    [center requestAuthorizationWithOptions:UNAuthorizationOptionAlert
                          completionHandler:^(BOOL, NSError *){
                          }];
  });
}

void postNotification(const QString &message, const QString &imagePath) {
  if (!isBundled())
    return;
  installNotificationHandler();

  UNMutableNotificationContent *content =
      [[UNMutableNotificationContent alloc] init];
  content.title = @"FOMOsnap";
  content.body = message.toNSString();

  if (!imagePath.isEmpty() && QFile::exists(imagePath)) {
    content.userInfo = @{@"path" : imagePath.toNSString()};
    content.subtitle = @"Click to edit";
    // The notification centre takes ownership of an attachment's file, so it
    // gets a throwaway copy rather than the screenshot the user just saved.
    const QString copyPath =
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("fomosnap-notify-%1.png")
                          .arg(QUuid::createUuid().toString(QUuid::Id128)));
    if (QFile::copy(imagePath, copyPath)) {
      NSError *attachmentError = nil;
      UNNotificationAttachment *attachment = [UNNotificationAttachment
          attachmentWithIdentifier:@"capture"
                               URL:[NSURL fileURLWithPath:copyPath.toNSString()]
                           options:nil
                             error:&attachmentError];
      if (attachment)
        content.attachments = @[ attachment ];
      else
        QFile::remove(copyPath);
    }
  }

  UNNotificationRequest *request = [UNNotificationRequest
      requestWithIdentifier:QUuid::createUuid().toString(QUuid::Id128).toNSString()
                    content:content
                    trigger:nil];
  [[UNUserNotificationCenter currentNotificationCenter]
      addNotificationRequest:request
       withCompletionHandler:nil];
}

void becomeAccessoryApp() {
  // No Dock tile and no menu bar: the overlay is summoned by a key, never
  // switched to, and a Dock icon would only be one more thing to dismiss.
  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

void activateApp() { [NSApp activateIgnoringOtherApps:YES]; }

/// Absolute path of the executable inside this app bundle. The plist needs an
/// absolute path, and it must be a stable one: under Homebrew the bundle lives
/// in a versioned Cellar directory reached through a stable `opt` symlink, and
/// the process is launched through that symlink, so this is what it resolves.
[[nodiscard]] QString agentExecutablePath() {
  NSString *executable = [[NSBundle mainBundle] executablePath];
  return executable ? QString::fromNSString(executable) : QString();
}

[[nodiscard]] QString agentPlistPath() {
  return QDir(QDir::homePath())
      .filePath(QStringLiteral("Library/LaunchAgents/%1.plist")
                    .arg(QString::fromLatin1(FOMOSNAP_AGENT_LABEL)));
}

/// Runs launchctl and reports whether it succeeded. Bootout is allowed to fail:
/// it is also used to clear a job that may not be loaded.
[[nodiscard]] bool runLaunchctl(const QStringList &arguments,
                                QString *diagnostic = nullptr) {
  if (qEnvironmentVariableIsSet("FOMOSNAP_TEST_NO_LAUNCHCTL"))
    return true;
  QProcess launchctl;
  launchctl.start(QStringLiteral("launchctl"), arguments);
  if (!launchctl.waitForStarted(5000)) {
    if (diagnostic)
      *diagnostic = launchctl.errorString();
    return false;
  }
  if (!launchctl.waitForFinished(10000)) {
    if (diagnostic)
      *diagnostic = QStringLiteral("timed out");
    return false;
  }
  if (launchctl.exitCode() == 0)
    return true;
  if (diagnostic) {
    *diagnostic = QString::fromLocal8Bit(launchctl.readAllStandardError());
    if (diagnostic->trimmed().isEmpty())
      *diagnostic =
          QStringLiteral("exited with code %1").arg(launchctl.exitCode());
  }
  return false;
}

bool setLaunchAtLogin(bool enabled, QString &error) {
  const QString label = QString::fromLatin1(FOMOSNAP_AGENT_LABEL);
  const QString plistPath = agentPlistPath();
  const QString domain = QStringLiteral("gui/%1").arg(::getuid());

  if (!enabled) {
    static_cast<void>(runLaunchctl(
        {QStringLiteral("bootout"), QStringLiteral("%1/%2").arg(domain, label)}));
    if (QFile::exists(plistPath) && !QFile::remove(plistPath)) {
      error = QStringLiteral("Could not remove %1").arg(plistPath);
      return false;
    }
    return true;
  }

  const QString executable = agentExecutablePath();
  if (executable.isEmpty()) {
    error = QStringLiteral(
        "Launch at login needs the bundled FOMOsnap.app, not a bare binary");
    return false;
  }

  // A plain user LaunchAgent rather than SMAppService. SMAppService checks a
  // code requirement that an ad-hoc signature in a Homebrew keg does not
  // satisfy: launchd accepted the registration and then refused to spawn it,
  // with EX_CONFIG. This works from any location.
  //
  // ProgramArguments names --agent explicitly. Launching the app bare would
  // start an ordinary capture and put a selection overlay on screen at login.
  const QString plist =
      QStringLiteral(
          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
          "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
          "<plist version=\"1.0\">\n"
          "<dict>\n"
          "  <key>Label</key>\n  <string>%1</string>\n"
          "  <key>ProgramArguments</key>\n"
          "  <array>\n    <string>%2</string>\n    <string>--agent</string>\n"
          "  </array>\n"
          "  <key>RunAtLoad</key>\n  <true/>\n"
          // launchd must restore the agent after macOS quits it for a
          // permission change. An explicit --uninstall-agent removes the
          // plist, which remains the opt-out.
          "  <key>KeepAlive</key>\n  <true/>\n"
          // A floor on restarts. If the agent ever does fail at launch, this
          // is the difference between a slow retry and a tight loop that
          // raises a system prompt every time round.
          "  <key>ThrottleInterval</key>\n  <integer>1</integer>\n"
          "  <key>ProcessType</key>\n  <string>Interactive</string>\n"
          "</dict>\n</plist>\n")
          .arg(label, executable.toHtmlEscaped());

  if (!QDir().mkpath(QFileInfo(plistPath).absolutePath())) {
    error = QStringLiteral("Could not create %1")
                .arg(QFileInfo(plistPath).absolutePath());
    return false;
  }
  QSaveFile file(plistPath);
  if (!file.open(QIODevice::WriteOnly) ||
      file.write(plist.toUtf8()) != plist.toUtf8().size() || !file.commit()) {
    error = QStringLiteral("Could not write %1: %2")
                .arg(plistPath, file.errorString());
    return false;
  }

  // Replace any previous registration, which may point at an older path.
  static_cast<void>(runLaunchctl(
      {QStringLiteral("bootout"), QStringLiteral("%1/%2").arg(domain, label)}));
  const QStringList bootstrapArguments = {
      QStringLiteral("bootstrap"), domain, plistPath};
  QString launchctlError;
  constexpr int kBootstrapAttempts = 20;
  for (int attempt = 0; attempt < kBootstrapAttempts; ++attempt) {
    launchctlError.clear();
    if (runLaunchctl(bootstrapArguments, &launchctlError))
      return true;
    // bootout returns before launchd has necessarily finished terminating the
    // old process. Retry the replacement while that transient state clears.
    if (attempt + 1 < kBootstrapAttempts)
      QThread::msleep(100);
  }

  error = QStringLiteral("Could not load the login item (%1)")
              .arg(plistPath);
  if (!launchctlError.trimmed().isEmpty())
    error += QStringLiteral(": ") + launchctlError.trimmed();
  return false;
}

bool launchesAtLogin() { return QFile::exists(agentPlistPath()); }

} // namespace mac
