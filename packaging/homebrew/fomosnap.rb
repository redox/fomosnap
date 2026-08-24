# Homebrew formula for FOMOsnap. The canonical copy lives here so it is
# version-controlled with the code it installs; the tap holds a copy.
#
# A formula, not a cask, because there is no released binary artifact to point
# a cask at yet. It builds from source against Homebrew's Qt.
class Fomosnap < Formula
  desc "Native macOS screenshot and annotation overlay, ported from Omasnap"
  homepage "https://github.com/redox/fomosnap"
  url "https://github.com/redox/fomosnap/archive/refs/tags/v2.0.3.tar.gz"
  sha256 "5d9cd2450fe326572412750b6a1de67c66007c84ea27c518669a039d87097d38"
  license "MIT"
  head "https://github.com/redox/fomosnap.git", branch: "main"

  depends_on "cmake" => :build
  depends_on "ninja" => :build
  # ScreenCaptureKit's one-shot capture API and SMAppService both land in 14.
  depends_on macos: :sonoma
  depends_on "qt"

  def install
    system "cmake", "-S", ".", "-B", "build",
           "-GNinja",
           "-DCMAKE_BUILD_TYPE=Release",
           # Qt is a declared dependency, so the keg must not carry a
           # second copy of it inside the bundle.
           "-DFOMOSNAP_BUNDLE_QT=OFF",
           # The smoke suite and the dev tools are not part of the install.
           "-DBUILD_TESTING=OFF",
           *std_cmake_args(install_prefix: prefix)
    system "cmake", "--build", "build", "--parallel"
    system "cmake", "--install", "build", "--prefix", prefix

    # A wrapper that execs, not a symlink. Launched through a symlink, dyld
    # reports the symlink as the executable path, so +[NSBundle mainBundle]
    # finds no bundle -- which silently disables notifications and the login
    # item. exec'ing the real path inside the bundle keeps the identity.
    (bin/"fomosnap").write <<~SH
      #!/bin/bash
      exec "#{opt_prefix}/FOMOsnap.app/Contents/MacOS/FOMOsnap" "$@"
    SH
    chmod 0755, bin/"fomosnap"
  end

  def post_install
    marker = var/"fomosnap/agent-defaulted"
    plist = Pathname(Dir.home)/"Library/LaunchAgents/com.fomosnap.FOMOsnap.agent.plist"

    # Enable the resident agent on a fresh install. On upgrades, only reload
    # it when the user already has the login item; an explicit
    # --uninstall-agent must remain an opt-out.
    if plist.exist? || !marker.exist?
      system opt_bin/"fomosnap", "--install-agent"
    end
    return if marker.exist?

    marker.parent.mkpath
    touch marker
  end

  def caveats
    <<~EOS
      FOMOsnap needs Screen Recording permission. The first capture explains
      and opens System Settings; grant it there, then start FOMOsnap again.
      Auto-scroll also needs Accessibility; grant it the same way.

      The default shortcut is Ctrl+Cmd+4 (Cmd+Shift+3/4/5 belong to macOS).

      The Homebrew install starts the resident agent automatically. To manage
      it manually:
        fomosnap --agent                   # foreground, to try it
        fomosnap --install-agent           # enable or reload at login
        fomosnap --uninstall-agent         # disable at login

      The app bundle is at:
        #{opt_prefix}/FOMOsnap.app
      Link it into /Applications if you want it in Launchpad and Spotlight:
        ln -sfn #{opt_prefix}/FOMOsnap.app /Applications/FOMOsnap.app
    EOS
  end

  test do
    assert_match "fomosnap", shell_output("#{bin}/fomosnap --version")
  end
end
