/** @fileoverview Process identity and user notifications: Dock policy, the
 *  login item for the resident agent, and UNUserNotificationCenter delivery
 *  with a thumbnail. Replaces omarchy-notification-send. */

#include "mac-platform.hpp"

#import <AppKit/AppKit.h>
#import <ServiceManagement/ServiceManagement.h>
#import <UserNotifications/UserNotifications.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>

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

bool setLaunchAtLogin(bool enabled, QString &error) {
  if (!isBundled()) {
    error = QStringLiteral(
        "Launch at login needs the bundled FOMOsnap.app, not a bare binary");
    return false;
  }
  SMAppService *service = [SMAppService mainAppService];
  NSError *failure = nil;
  const BOOL succeeded = enabled
                             ? [service registerAndReturnError:&failure]
                             : [service unregisterAndReturnError:&failure];
  if (!succeeded) {
    error = QStringLiteral("Could not %1 the login item: %2")
                .arg(enabled ? QStringLiteral("register")
                             : QStringLiteral("remove"),
                     failure ? QString::fromNSString(failure.localizedDescription)
                             : QStringLiteral("no detail reported"));
    return false;
  }
  return true;
}

bool launchesAtLogin() {
  if (!isBundled())
    return false;
  return [SMAppService mainAppService].status == SMAppServiceStatusEnabled;
}

} // namespace mac
