# Homebrew formula for FOMOsnap. The canonical copy lives here so it is
# version-controlled with the code it installs; the tap holds a copy.
#
# A formula, not a cask, because there is no released binary artifact to point
# a cask at yet. It builds from source against Homebrew's Qt.
class Fomosnap < Formula
  desc "Native macOS screenshot and annotation overlay, ported from Omasnap"
  homepage "https://github.com/redox/fomosnap"
  license "MIT"
  head "https://github.com/redox/fomosnap.git", branch: "main"

  # Filled in when a v* tag exists; until then, install with --HEAD.
  # url "https://github.com/redox/fomosnap/archive/refs/tags/v2.0.0.tar.gz"
  # sha256 "..."
  # version "2.0.0"

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
           *std_cmake_args(install_prefix: prefix)
    system "cmake", "--build", "build", "--parallel"
    system "cmake", "--install", "build", "--prefix", prefix

    # The executable must be launched from inside the bundle: Screen Recording
    # is granted to the bundle's identity, and a copied-out binary has none.
    bin.install_symlink prefix/"FOMOsnap.app/Contents/MacOS/FOMOsnap" => "fomosnap"
  end

  def caveats
    <<~EOS
      FOMOsnap needs Screen Recording permission. The first capture explains
      and opens System Settings; grant it there, then start FOMOsnap again.

      To hold a global hotkey, run the resident agent:
        fomosnap --agent
        fomosnap --install-agent   # and start it at login

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
