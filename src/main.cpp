#include "capture.hpp"
#include "cli-path.hpp"
#include "editor.hpp"
#include "instance-lock.hpp"
#include "mac/mac-platform.hpp"
#include "mac/mac-window.hpp"
#include "pin.hpp"
#include "recent-snaps.hpp"
#include "startup-timing.hpp"

#include <QImageReader>
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QGuiApplication>
#include <QLockFile>
#include <QScreen>
#include <QSocketNotifier>
#include <QUrl>
#include <QWindow>

#include <csignal>
#include <functional>
#include <memory>
#include <optional>
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
/// SIGTERM is how a second FOMOsnap asks a running one to give up its overlay.
/// A one-shot process answers by quitting; the resident agent answers by
/// closing the current session and staying alive for the next hotkey.
class PosixSignalNotifier final : public QObject {
public:
  using Action = std::function<void()>;

  explicit PosixSignalNotifier(QObject *parent = nullptr) : QObject(parent) {
    // macOS has no SOCK_NONBLOCK/SOCK_CLOEXEC socketpair flags, so both ends
    // are set up afterwards.
    if (::socketpair(AF_UNIX, SOCK_DGRAM, 0, fds_) != 0)
      return; // Default signal disposition stays in effect.
    for (const int fd : fds_) {
      ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
      ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD, 0) | FD_CLOEXEC);
    }
    signalFd_ = fds_[0];

    struct sigaction sa{};
    // The signal number is the message, so one socket can carry all of them.
    sa.sa_handler = [](int signo) {
      const int savedErrno = errno;
      const char byte = static_cast<char>(signo);
      const int fd = signalFd_;
      if (fd >= 0)
        static_cast<void>(::write(fd, &byte, sizeof(byte)));
      errno = savedErrno;
    };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigintInstalled_ = ::sigaction(SIGINT, &sa, &previousSigint_) == 0;
    sigtermInstalled_ = ::sigaction(SIGTERM, &sa, &previousSigterm_) == 0;
    sigusr1Installed_ = ::sigaction(SIGUSR1, &sa, &previousSigusr1_) == 0;
    if (!sigintInstalled_ && !sigtermInstalled_) {
      closeSockets();
      return;
    }

    notifier_ = new QSocketNotifier(fds_[1], QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, [this] {
      notifier_->setEnabled(false);
      char bytes[32];
      bool stopRequested = false;
      bool triggerRequested = false;
      for (ssize_t count; (count = ::read(fds_[1], bytes, sizeof(bytes))) > 0;) {
        for (ssize_t index = 0; index < count; ++index) {
          if (bytes[index] == static_cast<char>(SIGUSR1))
            triggerRequested = true;
          else
            stopRequested = true;
        }
      }
      if (triggerRequested && triggerAction_)
        triggerAction_();
      if (stopRequested) {
        if (action_)
          action_();
        else
          QCoreApplication::quit();
      }
      notifier_->setEnabled(true);
    });
  }

  /// What SIGINT/SIGTERM mean. Default: quit.
  void setAction(Action action) { action_ = std::move(action); }
  /// What SIGUSR1 means. The agent uses it to open or dismiss the overlay
  /// without the hotkey, which is how an external launcher can drive it and
  /// how the headless suite exercises the warm path.
  void setTriggerAction(Action action) { triggerAction_ = std::move(action); }

  ~PosixSignalNotifier() override {
    if (sigintInstalled_)
      ::sigaction(SIGINT, &previousSigint_, nullptr);
    if (sigtermInstalled_)
      ::sigaction(SIGTERM, &previousSigterm_, nullptr);
    if (sigusr1Installed_)
      ::sigaction(SIGUSR1, &previousSigusr1_, nullptr);
    closeSockets();
  }

private:
  void closeSockets() {
    signalFd_ = -1;
    for (int &fd : fds_) {
      if (fd >= 0) {
        ::close(fd);
        fd = -1;
      }
    }
  }

  static inline int fds_[2]{-1, -1};
  static inline volatile sig_atomic_t signalFd_ = -1;
  struct sigaction previousSigint_{};
  struct sigaction previousSigterm_{};
  struct sigaction previousSigusr1_{};
  bool sigintInstalled_ = false;
  bool sigtermInstalled_ = false;
  bool sigusr1Installed_ = false;
  QSocketNotifier *notifier_ = nullptr;
  Action action_;
  Action triggerAction_;
};

/// Everything one capture invocation needs, parsed once so the resident agent
/// can replay it on every hotkey press.
struct SessionOptions {
  CaptureEditor::CaptureMode captureMode = CaptureEditor::CaptureMode::Region;
  QuickOutputMode quickOutputMode = QuickOutputMode::None;
  bool editingImage = false;
  bool clipboardInput = false;
  QString filePath;
};

/// Notices when an overlay closes itself. Only the agent needs this: a
/// one-shot process ends with its last window, but the agent keeps running,
/// and nothing else would tell it the session is over so the next hotkey
/// press opens a new overlay rather than toggling a dead one.
///
/// Parented to the editor it watches, so it can never outlive it and is never
/// deleted from inside its own callback.
class CloseWatcher final : public QObject {
public:
  CloseWatcher(QObject *parent, std::function<void()> onClose)
      : QObject(parent), onClose_(std::move(onClose)) {}

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() == QEvent::Close || event->type() == QEvent::Hide) {
      // Queued: the editor is still delivering this event to itself, and the
      // callback tears the editor down.
      QMetaObject::invokeMethod(
          this,
          [this] {
            if (onClose_)
              onClose_();
          },
          Qt::QueuedConnection);
    }
    return QObject::eventFilter(watched, event);
  }

private:
  std::function<void()> onClose_;
};

/// One capture overlay and the single-instance lock it holds while up. The
/// lock is per session, not per process: an idle agent holds nothing, so a
/// command-line FOMOsnap can still take the screen.
class CaptureSession {
public:
  [[nodiscard]] bool active() const { return editor_ != nullptr; }

  void stop() {
    // Reentrant by construction: stop() runs from the editor's own close
    // notification, and tearing the editor down sends more of them.
    if (stopping_)
      return;
    stopping_ = true;
    if (CaptureEditor *editor = editor_.release()) {
      // deleteLater, never delete: the call stack above us belongs to this
      // editor and to the event filter parented to it.
      editor->hide();
      editor->deleteLater();
    }
    lock_.reset();
    mac::becomeAccessoryApp();
    stopping_ = false;
  }

  /// Opens the overlay. Returns the process exit code; `shown` distinguishes
  /// "a window is up, run the event loop" from "the work is already done".
  /// `watchForClose` is for the agent, which must notice a session ending.
  [[nodiscard]] int start(const SessionOptions &options, bool watchForClose,
                          bool &shown);

private:
  std::unique_ptr<QLockFile> lock_;
  std::unique_ptr<CaptureEditor> editor_;
  bool stopping_ = false;
};

/// The agent's session. A file-scope object because the hotkey handler is a
/// plain function pointer with nowhere to carry context.
CaptureSession g_session;
SessionOptions g_hotkeyOptions;

void onHotkey() {
  // The hotkey toggles, matching what the Hyprland binding did: press once to
  // open the overlay, again to dismiss it.
  if (g_session.active()) {
    g_session.stop();
    return;
  }
  bool shown = false;
  static_cast<void>(
      g_session.start(g_hotkeyOptions, /*watchForClose=*/true, shown));
}

int CaptureSession::start(const SessionOptions &options, bool watchForClose,
                          bool &shown) {
  shown = false;
  stop();

  const QString runtime = secureRuntimeDirectory();
  startupTimingMark("runtime directory ready");
  if (runtime.isEmpty()) {
    qCritical() << "Could not create private runtime directory";
    return 1;
  }
  auto lock = std::make_unique<QLockFile>(
      QDir(runtime).filePath(QStringLiteral("fomosnap.instance")));
  // Every capture, quick output included, dismisses a running overlay instead
  // of starting a second one: a late capture would otherwise photograph that
  // overlay. Editing an image always takes over so the editor can open.
  const InstanceLockResult lockResult =
      acquireInstanceLock(*lock, options.editingImage ? InstanceMode::EditFile
                                                      : InstanceMode::Capture);
  startupTimingMark("instance lock acquired");
  if (lockResult.signalledPid != 0)
    qInfo().noquote()
        << QStringLiteral("Asked the running fomosnap (pid %1) to quit")
               .arg(lockResult.signalledPid);
  if (!lockResult.proceed) {
    if (!lockResult.error.isEmpty())
      qCritical().noquote() << lockResult.error;
    return lockResult.exitCode;
  }

  // Screen Recording cannot be granted in-process: the first ask raises the
  // system prompt and the grant only applies to a fresh launch.
  //
  // Checked here, per capture, rather than at startup. The agent must not ask
  // at launch: it would raise the prompt at login, exit non-zero when the
  // permission is missing, and be restarted by launchd -- prompting again,
  // forever. Editing an image needs no permission at all.
  if (!options.editingImage) {
    QString permissionError;
    if (!mac::ensureScreenRecordingAccess(permissionError)) {
      qCritical().noquote() << permissionError;
      // Only the first time in this process: a resident agent that reopened
      // System Settings on every hotkey press would be its own kind of spam.
      static bool openedSettings = false;
      if (!openedSettings) {
        openedSettings = true;
        mac::openScreenRecordingSettings();
      }
      sendCaptureNotification(permissionError);
      return 1;
    }
  }

  CaptureData capture;
  OperationLog restoredLog;
  QString error;
  CaptureEditor::CaptureMode captureMode = options.captureMode;
  if (options.editingImage) {
    QImage image;
    QString inputName;
    if (options.clipboardInput) {
      if (!loadClipboardImage(image, error)) {
        const QString message =
            QStringLiteral("Could not load clipboard image: %1").arg(error);
        qCritical().noquote() << message;
        sendCaptureNotification(message);
        return 1;
      }
      inputName = QStringLiteral("clipboard image");
    } else {
      QString localFile = QUrl(options.filePath).toLocalFile();
      if (localFile.isEmpty())
        localFile = options.filePath;
      image.load(localFile);
      if (image.isNull()) {
        qCritical().noquote()
            << QStringLiteral("Could not load image: %1").arg(options.filePath);
        return 1;
      }
      inputName = localFile;
      const QString sidecar = operationLogPath(localFile);
      if (QFile::exists(sidecar) &&
          !loadOperationLog(sidecar, restoredLog, error)) {
        qCritical().noquote()
            << QStringLiteral("Could not restore operation log: %1").arg(error);
        return 1;
      }
    }
    describeFileCapture(capture, image, restoredLog);
    captureMode = CaptureEditor::CaptureMode::File;
    qInfo().noquote() << QStringLiteral("Opened %1 for annotation (%2x%3)")
                             .arg(inputName)
                             .arg(image.width())
                             .arg(image.height());
  } else if (!probeFocusedMonitor(capture.monitor, error)) {
    qCritical().noquote() << error;
    sendCaptureNotification(QStringLiteral("Screenshot failed: %1").arg(error));
    return 1;
  }
  startupTimingMark(options.editingImage ? "input image prepared"
                                         : "focused monitor probed");

  // Grab the display before the overlay window exists. Nothing about
  // ScreenCaptureKit would stop us photographing our own window, so the
  // ordering is what keeps the overlay out of its own screenshot.
  const bool instantFullscreenOutput =
      !options.editingImage &&
      captureMode == CaptureEditor::CaptureMode::Fullscreen &&
      options.quickOutputMode != QuickOutputMode::None;
  if (!options.editingImage &&
      !captureMonitorPixels(capture.monitor, capture, !instantFullscreenOutput,
                            error)) {
    qCritical().noquote() << error;
    sendCaptureNotification(QStringLiteral("Screenshot failed: %1").arg(error));
    return 1;
  }
  startupTimingMark(options.editingImage ? "pixel capture skipped"
                                         : "monitor pixels captured");

  if (instantFullscreenOutput) {
    QString outputError;
    const QSize expectedSize(
        qRound(capture.previewSize.width() * capture.monitor.scale),
        qRound(capture.previewSize.height() * capture.monitor.scale));
    const QImage output =
        capture.monitor.scale <= 1.0 || capture.source.size() == expectedSize
            ? capture.source
            : renderCapture(capture, QRectF(QPointF(), capture.previewSize), {},
                            BackgroundStyle::None);
    if (!quickOutput(output, options.quickOutputMode, outputError)) {
      qCritical().noquote() << outputError;
      return 1;
    }
    return 0;
  }

  // Displays are matched by geometry rather than by name: Qt and macOS agree
  // on the layout in points, but not on what to call a screen.
  QScreen *targetScreen = QGuiApplication::primaryScreen();
  for (QScreen *screen : QGuiApplication::screens()) {
    if (screen->geometry() == capture.monitor.geometry ||
        screen->name() == capture.monitor.name) {
      targetScreen = screen;
      break;
    }
  }

  if (!options.editingImage) {
    qInfo().noquote()
        << QStringLiteral("Captured %1 with %2 selectable windows")
               .arg(capture.monitor.name)
               .arg(capture.windows.size());
  }

  auto editor = std::make_unique<CaptureEditor>(
      std::move(capture), captureMode, options.quickOutputMode, restoredLog);
  startupTimingMark("CaptureEditor constructed");
  // Frameless and translucent before the native window is realised: the edit
  // phase draws a scrim over the live desktop, and a title bar or a drop
  // shadow on a full-screen overlay reads as a rendering bug.
  // Dialog, not Window: Qt realises an NSPanel, which can steal key focus
  // while another app stays frontmost. A normal NSWindow cannot, so Esc
  // would go to whatever was focused until a click.
  editor->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint |
                         Qt::NoDropShadowWindowHint);
  editor->setAttribute(Qt::WA_TranslucentBackground);
  editor->setAttribute(Qt::WA_ShowWithoutActivating);
  editor->setScreen(targetScreen);
  editor->setGeometry(targetScreen->geometry());
  editor->winId();
  QWindow *window = editor->windowHandle();
  if (!window) {
    qCritical() << "Could not create the capture overlay window";
    return 1;
  }
  macwindow::configure(window, macwindow::Level::Shielding,
                       macwindow::Keyboard::Exclusive,
                       /*joinsAllSpaces=*/true, /*transparent=*/true);
  editor->setSafeAreaTop(macwindow::safeAreaTopInset(window));
  startupTimingMark("overlay window configured");
  editor->show();
  macwindow::activate(window);
  editor->setFocus(Qt::ActiveWindowFocusReason);

  if (watchForClose) {
    auto *watcher = new CloseWatcher(editor.get(), [this] { stop(); });
    editor->installEventFilter(watcher);
  }

  lock_ = std::move(lock);
  editor_ = std::move(editor);
  shown = true;
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  startupTimingMark("entered main");
  QCoreApplication::setApplicationName(QStringLiteral("fomosnap"));
  QCoreApplication::setApplicationVersion(QString::fromLatin1(FOMOSNAP_VERSION));
  QCoreApplication::setOrganizationName(QStringLiteral("FOMOsnap"));
  QApplication application(argc, argv);
  startupTimingMark("QApplication constructed");
  mac::installNotificationHandler();

  // A stitched scroll capture (or any tall pinned image) exceeds Qt's default
  // 256 MB image-decode allocation limit; lift it so --file/--pin can open it.
  QImageReader::setAllocationLimit(0);
  PosixSignalNotifier signalNotifier(&application);

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral(
      "Native macOS screenshot and annotation overlay.\n"
      "\n"
      "Only one capture overlay runs at a time. Starting fomosnap again while "
      "an\noverlay is open dismisses it: the running instance is asked to "
      "quit and the\nnew process exits without capturing, so the same hotkey "
      "opens and closes the\noverlay. Quick output (--copy, --save) dismisses "
      "it the same way instead of\nscreenshotting the overlay. With --file (or "
      "an image path) or --clipboard, the running\ninstance is stopped and "
      "the editor opens on that image instead.\n"
      "\n"
      "Exit codes: 0 success, including dismissing a running overlay; 1 "
      "capture,\nimage, or single-instance lock failure; 2 usage error."));
  parser.addHelpOption();
  parser.addVersionOption();
  const QCommandLineOption fullscreenOption(
      QStringLiteral("capture-fullscreen"),
      QStringLiteral("Start with the entire focused monitor selected."));
  const QCommandLineOption windowOption(
      {QStringLiteral("capture-window"), QStringLiteral("capture-windows")},
      QStringLiteral("Start in window selection mode."));
  const QCommandLineOption regionOption(
      QStringLiteral("capture-region"),
      QStringLiteral("Start in freeform region selection mode (default)."));
  parser.addOption(fullscreenOption);
  parser.addOption(windowOption);
  parser.addOption(regionOption);
  const QCommandLineOption copyOption(
      QStringLiteral("copy"),
      QStringLiteral("Copy the capture directly without opening the editor."));
  const QCommandLineOption saveOption(
      QStringLiteral("save"),
      QStringLiteral("Save the capture directly without opening the editor."));
  parser.addOption(copyOption);
  parser.addOption(saveOption);
  const QCommandLineOption fileOption(
      QStringLiteral("file"),
      QStringLiteral("Open an existing image file in the annotation editor "
                     "instead of capturing the screen."),
      QStringLiteral("path"));
  parser.addOption(fileOption);
  const QCommandLineOption clipboardOption(
      QStringLiteral("clipboard"),
      QStringLiteral("Open the current clipboard image in the annotation "
                     "editor instead of capturing the screen."));
  parser.addOption(clipboardOption);
  const QCommandLineOption pinOption(
      QStringLiteral("pin"),
      QStringLiteral("Show an image as a pinned always-visible layer."),
      QStringLiteral("path"));
  parser.addOption(pinOption);
  const QCommandLineOption scrollOption(
      QStringLiteral("scroll"),
      QStringLiteral("Capture a scrolling region and stitch it into one tall "
                     "image, then open it in the editor."));
  parser.addOption(scrollOption);
  const QCommandLineOption agentOption(
      QStringLiteral("agent"),
      QStringLiteral("Stay resident with no Dock icon and open the overlay on "
                     "a system-wide hotkey. Qt is already warm, so the "
                     "overlay appears with no launch cost."));
  parser.addOption(agentOption);
  const QCommandLineOption hotkeyOption(
      QStringLiteral("hotkey"),
      QStringLiteral("Hotkey for --agent, e.g. \"ctrl+cmd+4\" (default) or "
                     "\"cmd+shift+2\"."),
      QStringLiteral("keys"));
  parser.addOption(hotkeyOption);
  const QCommandLineOption installAgentOption(
      QStringLiteral("install-agent"),
      QStringLiteral("Start the agent at login, then exit."));
  parser.addOption(installAgentOption);
  const QCommandLineOption uninstallAgentOption(
      QStringLiteral("uninstall-agent"),
      QStringLiteral("Stop starting the agent at login, then exit."));
  parser.addOption(uninstallAgentOption);
  parser.addPositionalArgument(
      QStringLiteral("target"),
      QStringLiteral("Capture mode (smart, region, windows, fullscreen) or the "
                     "path of an image file to edit."),
      QStringLiteral("[target]"));
  parser.process(application);
  startupTimingMark("command line parsed");

  if (parser.isSet(installAgentOption) || parser.isSet(uninstallAgentOption)) {
    const bool enable = parser.isSet(installAgentOption);
    QString error;
    if (!mac::setLaunchAtLogin(enable, error)) {
      qCritical().noquote() << error;
      return 1;
    }
    qInfo().noquote() << (enable ? QStringLiteral("FOMOsnap will start at login")
                                 : QStringLiteral("FOMOsnap will no longer "
                                                  "start at login"));
    return 0;
  }

  QString filePath = parser.value(fileOption);
  const bool clipboardInput = parser.isSet(clipboardOption);

  QuickOutputMode quickOutputMode = QuickOutputMode::None;
  if (parser.isSet(copyOption) && parser.isSet(saveOption))
    quickOutputMode = QuickOutputMode::Both;
  else if (parser.isSet(copyOption))
    quickOutputMode = QuickOutputMode::Copy;
  else if (parser.isSet(saveOption))
    quickOutputMode = QuickOutputMode::Save;

  CaptureEditor::CaptureMode captureMode = CaptureEditor::CaptureMode::Region;
  int requestedModes = parser.isSet(fullscreenOption) +
                       parser.isSet(windowOption) + parser.isSet(regionOption) +
                       parser.isSet(scrollOption);
  if (parser.isSet(fullscreenOption))
    captureMode = CaptureEditor::CaptureMode::Fullscreen;
  else if (parser.isSet(windowOption))
    captureMode = CaptureEditor::CaptureMode::Window;
  else if (parser.isSet(scrollOption))
    captureMode = CaptureEditor::CaptureMode::Scroll;

  const QStringList positional = parser.positionalArguments();
  if (parser.isSet(pinOption)) {
    if (!filePath.isEmpty() || clipboardInput || requestedModes > 0 ||
        !positional.isEmpty() || quickOutputMode != QuickOutputMode::None) {
      qCritical()
          << "Pinned mode cannot be combined with capture or edit targets";
      return 2;
    }
    QString pinPath = QUrl(parser.value(pinOption)).toLocalFile();
    if (pinPath.isEmpty())
      pinPath = parser.value(pinOption);
    if (!loadCaptureFonts())
      return 1;
    return runPinnedCapture(pinPath);
  }
  if (positional.size() > 1) {
    qCritical() << "Only one capture target may be specified";
    return 2;
  }
  if (!positional.isEmpty()) {
    const QString localTarget = resolveLocalImagePath(positional.first());
    if (filePath.isEmpty() && !localTarget.isEmpty()) {
      filePath = localTarget;
    } else {
      ++requestedModes;
      const QString mode = positional.first();
      if (mode == QStringLiteral("fullscreen"))
        captureMode = CaptureEditor::CaptureMode::Fullscreen;
      else if (mode == QStringLiteral("windows") ||
               mode == QStringLiteral("window"))
        captureMode = CaptureEditor::CaptureMode::Window;
      else if (mode == QStringLiteral("smart") ||
               mode == QStringLiteral("region"))
        captureMode = CaptureEditor::CaptureMode::Region;
      else if (mode == QStringLiteral("scroll"))
        captureMode = CaptureEditor::CaptureMode::Scroll;
      else {
        qCritical().noquote()
            << QStringLiteral("Unknown capture target: %1").arg(mode);
        return 2;
      }
    }
  }
  if (filePath.isEmpty() && requestedModes > 1) {
    qCritical() << "Capture mode options are mutually exclusive";
    return 2;
  }
  if (!filePath.isEmpty() && requestedModes > 0) {
    qCritical() << "An image file cannot be combined with a capture mode";
    return 2;
  }
  if (clipboardInput && (!filePath.isEmpty() || requestedModes > 0)) {
    qCritical() << "Clipboard input cannot be combined with another target";
    return 2;
  }
  const bool editingImage = clipboardInput || !filePath.isEmpty();
  if (editingImage && quickOutputMode != QuickOutputMode::None) {
    qCritical()
        << "Quick output options cannot be combined with an image input";
    return 2;
  }
  const bool agentMode = parser.isSet(agentOption);
  if (agentMode && (editingImage || quickOutputMode != QuickOutputMode::None)) {
    qCritical() << "Agent mode cannot be combined with an image input or "
                   "quick output";
    return 2;
  }
  startupTimingMark("options resolved");
  if (!loadCaptureFonts())
    return 1;
  startupTimingMark("capture font loaded");

  SessionOptions options;
  options.captureMode = captureMode;
  options.quickOutputMode = quickOutputMode;
  options.editingImage = editingImage;
  options.clipboardInput = clipboardInput;
  options.filePath = filePath;

  if (agentMode) {
    // One agent per user. Carbon delivers a hotkey to every process that
    // registered it, so a second agent means both open an overlay and the
    // single-instance handover then asks the other to give up the screen: the
    // overlay appears and vanishes, and the capture looks like it did nothing.
    const QString agentRuntime = secureRuntimeDirectory();
    if (agentRuntime.isEmpty()) {
      qCritical() << "Could not create private runtime directory";
      return 1;
    }
    static QLockFile agentLock(
        QDir(agentRuntime).filePath(QStringLiteral("fomosnap.agent")));
    agentLock.setStaleLockTime(0);
    if (!agentLock.tryLock(0)) {
      qint64 holder = 0;
      QString host;
      QString application;
      agentLock.getLockInfo(&holder, &host, &application);
      // Exit 0, not an error: "an agent is already running" is the desired end
      // state, and a non-zero exit would have launchd restart this forever.
      qInfo().noquote()
          << QStringLiteral("A FOMOsnap agent is already running%1; leaving it "
                            "in charge.")
                 .arg(holder != 0 ? QStringLiteral(" (pid %1)").arg(holder)
                                  : QString());
      return 0;
    }

    // The agent outlives every overlay it opens, so closing one must not end
    // the process, and a termination request only dismisses the overlay.
    // FOMOsnap is a Dockless utility in every mode; the bundle's LSUIElement
    // setting also keeps the permission-relaunch instance out of the Dock.
    mac::becomeAccessoryApp();
    application.setQuitOnLastWindowClosed(false);
    // SIGUSR1 opens or dismisses the overlay, exactly as the hotkey does.
    signalNotifier.setTriggerAction([] { onHotkey(); });
    // A termination request means "give up the screen" while an overlay is up
    // -- that is how a second FOMOsnap takes over -- and "stop being resident"
    // when there is nothing to give up. Without the second half, an idle agent
    // would ignore both SIGTERM and Ctrl-C and need SIGKILL.
    signalNotifier.setAction([] {
      if (g_session.active())
        g_session.stop();
      else
        QCoreApplication::quit();
    });

    QString hotkeySpec = parser.value(hotkeyOption);
    if (hotkeySpec.isEmpty())
      hotkeySpec = qEnvironmentVariable("FOMOSNAP_HOTKEY",
                                        QStringLiteral("ctrl+cmd+4"));
    quint32 keyCode = 0;
    quint32 modifiers = 0;
    QString hotkeyError;
    if (!mac::parseHotkey(hotkeySpec, keyCode, modifiers, hotkeyError) ||
        !mac::registerHotkey(keyCode, modifiers, &onHotkey, hotkeyError)) {
      qCritical().noquote() << hotkeyError;
      return 1;
    }
    g_hotkeyOptions = options;
    qInfo().noquote()
        << QStringLiteral("FOMOsnap agent ready on %1").arg(hotkeySpec);
    const int code = application.exec();
    mac::unregisterHotkey();
    // Before QApplication goes away: g_session outlives main, and a QWidget
    // destroyed after its application is a crash on the way out.
    g_session.stop();
    return code;
  }

  application.setQuitOnLastWindowClosed(true);
  bool shown = false;
  const int code = g_session.start(options, /*watchForClose=*/false, shown);
  if (code != 0 || !shown) {
    g_session.stop();
    return code;
  }
  startupTimingMark("show requested; entering event loop");
  const int exitCode = application.exec();
  g_session.stop();
  return exitCode;
}
