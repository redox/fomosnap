# typed: strict
# frozen_string_literal: true

# Homebrew cask for FOMOsnap. The canonical copy lives here so it is
# version-controlled with the code it installs; the tap holds a copy.
cask "fomosnap" do
  version "2.4.0"
  sha256 "7c1eb2151e3e94c45b5e320d5482141b8f87573137c968a270eba9b809ec4323"

  url "https://github.com/redox/fomosnap/releases/download/v#{version}/fomosnap-#{version}-macos-arm64.tar.gz"
  name "FOMOsnap"
  desc "Native screenshot and annotation overlay"
  homepage "https://github.com/redox/fomosnap"

  depends_on macos: :sonoma

  app "FOMOsnap.app"
  binary "fomosnap"

  # Enable the resident agent on a fresh install. On upgrades, only reload it
  # when the login item is already present; an explicit --uninstall-agent must
  # remain an opt-out. The marker records that the default was applied.
  postflight_steps do
    if_path_exists "~/Library/LaunchAgents/com.fomosnap.FOMOsnap.agent.plist" do
      run "FOMOsnap.app/Contents/MacOS/FOMOsnap",
          args:           ["--install-agent"],
          base:           :appdir,
          writable_paths: ["~/Library/LaunchAgents"]
    end
    unless_path_exists "~/Library/Application Support/fomosnap/homebrew-agent-defaulted" do
      run "FOMOsnap.app/Contents/MacOS/FOMOsnap",
          args:           ["--install-agent"],
          base:           :appdir,
          writable_paths: ["~/Library/LaunchAgents"]
      mkdir_p "~/Library/Application Support/fomosnap"
      touch "~/Library/Application Support/fomosnap/homebrew-agent-defaulted"
    end
  end
end
