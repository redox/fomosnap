# typed: strict
# frozen_string_literal: true

# Homebrew cask for FOMOsnap. The canonical copy lives here so it is
# version-controlled with the code it installs; the tap holds a copy.
cask "fomosnap" do
  version "2.1.3"
  sha256 "b4443a19b71b377e3a63b46732e4dc938635d309c1b85095df3423244e80eb4b"

  url "https://github.com/redox/fomosnap/releases/download/v#{version}/fomosnap-#{version}-macos-arm64.tar.gz"
  name "FOMOsnap"
  desc "Native screenshot and annotation overlay"
  homepage "https://github.com/redox/fomosnap"

  depends_on macos: :sonoma

  app "FOMOsnap.app"
  binary "fomosnap"

  postflight do
    marker =
      Pathname(Dir.home)/"Library/Application Support/fomosnap/homebrew-agent-defaulted"
    plist =
      Pathname(Dir.home)/"Library/LaunchAgents/com.fomosnap.FOMOsnap.agent.plist"
    executable = appdir/"FOMOsnap.app/Contents/MacOS/FOMOsnap"

    # Enable the resident agent on a fresh install. On upgrades, only reload
    # it when the user already has the login item; an explicit
    # --uninstall-agent must remain an opt-out.
    if plist.exist? || !marker.exist?
      system_command executable, args: ["--install-agent"]
    end
    next if marker.exist?

    marker.parent.mkpath
    FileUtils.touch(marker.to_s)
  end
end
