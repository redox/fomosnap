/** @fileoverview The system-wide hotkey for the resident agent. Carbon's
 *  RegisterEventHotKey is the only public API that claims a global shortcut
 *  without Accessibility permission, so a capture never costs the user a
 *  second TCC prompt. It is long-deprecated and long-unremoved. */

#include "mac-platform.hpp"

#import <Carbon/Carbon.h>

#include <QHash>

namespace {

EventHotKeyRef g_hotKey = nullptr;
EventHandlerRef g_handlerRef = nullptr;
void (*g_handler)() = nullptr;

OSStatus hotKeyPressed(EventHandlerCallRef, EventRef, void *) {
  // Carbon delivers on the main thread already, but hopping through the queue
  // keeps the handler off the event-dispatch stack, so it may open windows.
  if (g_handler)
    dispatch_async(dispatch_get_main_queue(), ^{
      if (g_handler)
        g_handler();
    });
  return noErr;
}

[[nodiscard]] const QHash<QString, quint32> &keyCodes() {
  static const QHash<QString, quint32> codes = {
      {QStringLiteral("a"), kVK_ANSI_A}, {QStringLiteral("b"), kVK_ANSI_B},
      {QStringLiteral("c"), kVK_ANSI_C}, {QStringLiteral("d"), kVK_ANSI_D},
      {QStringLiteral("e"), kVK_ANSI_E}, {QStringLiteral("f"), kVK_ANSI_F},
      {QStringLiteral("g"), kVK_ANSI_G}, {QStringLiteral("h"), kVK_ANSI_H},
      {QStringLiteral("i"), kVK_ANSI_I}, {QStringLiteral("j"), kVK_ANSI_J},
      {QStringLiteral("k"), kVK_ANSI_K}, {QStringLiteral("l"), kVK_ANSI_L},
      {QStringLiteral("m"), kVK_ANSI_M}, {QStringLiteral("n"), kVK_ANSI_N},
      {QStringLiteral("o"), kVK_ANSI_O}, {QStringLiteral("p"), kVK_ANSI_P},
      {QStringLiteral("q"), kVK_ANSI_Q}, {QStringLiteral("r"), kVK_ANSI_R},
      {QStringLiteral("s"), kVK_ANSI_S}, {QStringLiteral("t"), kVK_ANSI_T},
      {QStringLiteral("u"), kVK_ANSI_U}, {QStringLiteral("v"), kVK_ANSI_V},
      {QStringLiteral("w"), kVK_ANSI_W}, {QStringLiteral("x"), kVK_ANSI_X},
      {QStringLiteral("y"), kVK_ANSI_Y}, {QStringLiteral("z"), kVK_ANSI_Z},
      {QStringLiteral("0"), kVK_ANSI_0}, {QStringLiteral("1"), kVK_ANSI_1},
      {QStringLiteral("2"), kVK_ANSI_2}, {QStringLiteral("3"), kVK_ANSI_3},
      {QStringLiteral("4"), kVK_ANSI_4}, {QStringLiteral("5"), kVK_ANSI_5},
      {QStringLiteral("6"), kVK_ANSI_6}, {QStringLiteral("7"), kVK_ANSI_7},
      {QStringLiteral("8"), kVK_ANSI_8}, {QStringLiteral("9"), kVK_ANSI_9},
      {QStringLiteral("f1"), kVK_F1},    {QStringLiteral("f2"), kVK_F2},
      {QStringLiteral("f3"), kVK_F3},    {QStringLiteral("f4"), kVK_F4},
      {QStringLiteral("f5"), kVK_F5},    {QStringLiteral("f6"), kVK_F6},
      {QStringLiteral("f7"), kVK_F7},    {QStringLiteral("f8"), kVK_F8},
      {QStringLiteral("f9"), kVK_F9},    {QStringLiteral("f10"), kVK_F10},
      {QStringLiteral("f11"), kVK_F11},  {QStringLiteral("f12"), kVK_F12},
      {QStringLiteral("f13"), kVK_F13},  {QStringLiteral("f14"), kVK_F14},
      {QStringLiteral("f15"), kVK_F15},  {QStringLiteral("f16"), kVK_F16},
      {QStringLiteral("space"), kVK_Space},
      {QStringLiteral("return"), kVK_Return},
      {QStringLiteral("escape"), kVK_Escape},
      {QStringLiteral("tab"), kVK_Tab},
  };
  return codes;
}

} // namespace

namespace mac {

bool parseHotkey(const QString &spec, quint32 &keyCode, quint32 &modifiers,
                 QString &error) {
  keyCode = 0;
  modifiers = 0;
  const QStringList parts =
      spec.toLower().split(QLatin1Char('+'), Qt::SkipEmptyParts);
  if (parts.isEmpty()) {
    error = QStringLiteral("Empty hotkey");
    return false;
  }

  bool haveKey = false;
  for (const QString &raw : parts) {
    const QString part = raw.trimmed();
    if (part == QStringLiteral("cmd") || part == QStringLiteral("command") ||
        part == QStringLiteral("meta")) {
      modifiers |= cmdKey;
    } else if (part == QStringLiteral("ctrl") ||
               part == QStringLiteral("control")) {
      modifiers |= controlKey;
    } else if (part == QStringLiteral("alt") || part == QStringLiteral("opt") ||
               part == QStringLiteral("option")) {
      modifiers |= optionKey;
    } else if (part == QStringLiteral("shift")) {
      modifiers |= shiftKey;
    } else if (const auto found = keyCodes().constFind(part);
               found != keyCodes().constEnd()) {
      if (haveKey) {
        error = QStringLiteral("Hotkey names more than one key: %1").arg(spec);
        return false;
      }
      keyCode = found.value();
      haveKey = true;
    } else {
      error = QStringLiteral("Unknown hotkey component: %1").arg(part);
      return false;
    }
  }

  if (!haveKey) {
    error = QStringLiteral("Hotkey names no key: %1").arg(spec);
    return false;
  }
  if (modifiers == 0) {
    error = QStringLiteral("Hotkey needs at least one modifier: %1").arg(spec);
    return false;
  }
  return true;
}

bool registerHotkey(quint32 keyCode, quint32 modifiers, void (*handler)(),
                    QString &error) {
  unregisterHotkey();
  g_handler = handler;

  if (!g_handlerRef) {
    EventTypeSpec eventType{kEventClassKeyboard, kEventHotKeyPressed};
    const OSStatus installed = InstallApplicationEventHandler(
        &hotKeyPressed, 1, &eventType, nullptr, &g_handlerRef);
    if (installed != noErr) {
      error = QStringLiteral("Could not install the hotkey handler (%1)")
                  .arg(installed);
      return false;
    }
  }

  const EventHotKeyID hotKeyId{'omsn', 1};
  const OSStatus registered =
      RegisterEventHotKey(keyCode, modifiers, hotKeyId,
                          GetApplicationEventTarget(), 0, &g_hotKey);
  if (registered != noErr) {
    g_hotKey = nullptr;
    error = registered == eventHotKeyExistsErr
                ? QStringLiteral("That shortcut is already claimed by another "
                                 "application")
                : QStringLiteral("Could not register the hotkey (%1)")
                      .arg(registered);
    return false;
  }
  return true;
}

void unregisterHotkey() {
  if (g_hotKey) {
    UnregisterEventHotKey(g_hotKey);
    g_hotKey = nullptr;
  }
  g_handler = nullptr;
}

} // namespace mac
