cmake_minimum_required(VERSION 3.24)
project(fomosnap VERSION 2.0.6 LANGUAGES C CXX OBJCXX)
include(GNUInstallDirs)
include(CTest)
set(CMAKE_AUTOMOC ON)

if(NOT APPLE)
  message(FATAL_ERROR "FOMOsnap targets macOS only.")
endif()

# ScreenCaptureKit's one-shot SCScreenshotManager.captureImageWithFilter and
# SMAppService both land in macOS 14, and nothing here needs a lower floor.
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")

find_package(Qt6 6.8 REQUIRED COMPONENTS Concurrent Core Gui Test Widgets)

# The bundle identifier is the TCC identity: Screen Recording is granted to
# it, not to the path, so changing it makes the user re-approve FOMOsnap.
set(FOMOSNAP_BUNDLE_ID "com.fomosnap.FOMOsnap" CACHE STRING "Bundle identifier")

# The login item is a user LaunchAgent written to ~/Library/LaunchAgents at
# --install-agent time; only its label is fixed at build time.
set(FOMOSNAP_AGENT_LABEL "${FOMOSNAP_BUNDLE_ID}.agent")

set(FOMOSNAP_MAC_SOURCES
  src/mac/mac-platform.hpp
  src/mac/mac-window.hpp
  src/mac/mac-app.mm
  src/mac/mac-hotkey.mm
  src/mac/mac-ocr.mm
  src/mac/mac-screen.mm
  src/mac/mac-scroll-inject.mm
  src/mac/mac-window.mm
)

add_library(fomosnap-core STATIC
  src/cli-path.cpp
  src/cli-path.hpp
  src/icons.cpp
  src/icons.hpp
  src/instance-lock.cpp
  src/instance-lock.hpp
  src/pin-file.cpp
  src/pin-file.hpp
  src/pin-layout.cpp
  src/pin-layout.hpp
  src/capture.cpp
  src/capture.hpp
  src/cut.cpp
  src/cut.hpp
  src/palette-config.cpp
  src/palette-config.hpp
  src/recent-snaps.cpp
  src/recent-snaps.hpp
  src/output-config.cpp
  src/output-config.hpp
  src/overlay-chrome.cpp
  src/overlay-chrome.hpp
  src/eyedropper.cpp
  src/eyedropper.hpp
  src/scroll-capture.cpp
  src/scroll-capture.hpp
  src/scroll-inject.hpp
  src/stitch.cpp
  src/stitch.hpp
  src/auto-capture.cpp
  src/auto-capture.hpp
  src/startup-timing.cpp
  src/startup-timing.hpp
  src/editor.cpp
  src/editor.hpp
  ${FOMOSNAP_MAC_SOURCES}
)
qt_add_resources(fomosnap-core "capture-fonts"
  PREFIX "/fonts"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets"
  FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/Neucha.ttf"
)

# Objective-C++ only; the plain C++ translation units never see an Apple
# header, so ARC is scoped to the platform layer rather than the whole build.
set_source_files_properties(
  src/mac/mac-app.mm
  src/mac/mac-hotkey.mm
  src/mac/mac-ocr.mm
  src/mac/mac-screen.mm
  src/mac/mac-scroll-inject.mm
  src/mac/mac-window.mm
  PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
)

target_include_directories(fomosnap-core PUBLIC
  "${CMAKE_CURRENT_BINARY_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_definitions(fomosnap-core PRIVATE
  FOMOSNAP_AGENT_LABEL="${FOMOSNAP_AGENT_LABEL}")
target_compile_features(fomosnap-core PUBLIC cxx_std_23)
target_compile_options(fomosnap-core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(fomosnap-core PUBLIC
  Qt6::Concurrent
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  "-framework AppKit"
  "-framework Carbon"
  "-framework CoreGraphics"
  "-framework CoreMedia"
  "-framework CoreVideo"
  "-framework Foundation"
  "-framework ScreenCaptureKit"
  "-framework UserNotifications"
  "-framework Vision"
)

set(FOMOSNAP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/FOMOsnap.icns")
set_source_files_properties("${FOMOSNAP_ICON}" PROPERTIES
  MACOSX_PACKAGE_LOCATION "Resources")

qt_add_executable(fomosnap MACOSX_BUNDLE
  src/main.cpp src/pin.cpp src/pin.hpp "${FOMOSNAP_ICON}")
target_compile_options(fomosnap PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(fomosnap PRIVATE
  FOMOSNAP_VERSION="${PROJECT_VERSION}")
target_link_libraries(fomosnap PRIVATE fomosnap-core)
set_target_properties(fomosnap PROPERTIES
  OUTPUT_NAME "FOMOsnap"
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in"
  MACOSX_BUNDLE_BUNDLE_NAME "FOMOsnap"
  MACOSX_BUNDLE_GUI_IDENTIFIER "${FOMOSNAP_BUNDLE_ID}"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_COPYRIGHT "MIT licensed"
  MACOSX_BUNDLE_ICON_FILE "FOMOsnap"
)

# TCC identifies the app by its code signature, so the signing identity decides
# how often macOS re-asks for Screen Recording.
#
# The ad-hoc default ("-") has no stable identity: its hash changes with the
# binary, so every rebuild looks like a different app and the permission has to
# be granted again. Set this to a real signing identity -- a Developer ID, or a
# self-signed certificate from Keychain Access -- and the grant survives
# rebuilds. `security find-identity -v -p codesigning` lists what is available.
set(FOMOSNAP_CODESIGN_IDENTITY "-" CACHE STRING
  "codesign identity; \"-\" is ad-hoc and re-prompts for TCC on every rebuild")

add_custom_command(TARGET fomosnap POST_BUILD
  COMMAND codesign --force --sign "${FOMOSNAP_CODESIGN_IDENTITY}"
          --timestamp=none "$<TARGET_BUNDLE_DIR:fomosnap>"
  COMMENT "Signing FOMOsnap.app with identity ${FOMOSNAP_CODESIGN_IDENTITY}"
  VERBATIM
)

if(BUILD_TESTING)
qt_add_executable(fomosnap-smoke
  tests/editor-smoke.cpp
  tests/clipboard-smoke.cpp
  tests/clipboard-smoke.hpp
  tests/instance-lock-smoke.cpp
  tests/instance-lock-smoke.hpp
  tests/capture-probe-smoke.cpp
  tests/capture-probe-smoke.hpp
  tests/session-exit-smoke.cpp
  tests/session-exit-smoke.hpp
  tests/cut-smoke.cpp
  tests/cut-smoke.hpp
  tests/cut-mapping-smoke.cpp
  tests/cut-mapping-smoke.hpp
  tests/palette-config-smoke.cpp
  tests/palette-config-smoke.hpp
  tests/pin-lifecycle-smoke.cpp
  tests/pin-lifecycle-smoke.hpp
  tests/pin-layout-smoke.cpp
  tests/pin-layout-smoke.hpp
  tests/stitch-smoke.cpp
  tests/stitch-smoke.hpp
)
target_include_directories(fomosnap-smoke PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/tests"
)
target_compile_options(fomosnap-smoke PRIVATE -Wall -Wextra -Wpedantic)
# The process-lifetime checks drive the real executable, so they need its path.
target_compile_definitions(fomosnap-smoke PRIVATE
  FOMOSNAP_EXECUTABLE="$<TARGET_FILE:fomosnap>"
  FOMOSNAP_AGENT_LABEL="${FOMOSNAP_AGENT_LABEL}")
add_dependencies(fomosnap-smoke fomosnap)
target_link_libraries(fomosnap-smoke PRIVATE fomosnap-core Qt6::Test)
add_test(NAME fomosnap-smoke COMMAND fomosnap-smoke "${CMAKE_CURRENT_BINARY_DIR}/fomosnap-smoke-output")
set_tests_properties(fomosnap-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

qt_add_executable(icon-generator tools/icon-generator.cpp)
target_compile_features(icon-generator PRIVATE cxx_std_23)
target_compile_options(icon-generator PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(icon-generator PRIVATE Qt6::Core Qt6::Gui)

qt_add_executable(stitch-replay
  src/stitch-replay.cpp
  src/stitch.cpp
  src/stitch.hpp
)
target_include_directories(stitch-replay PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_features(stitch-replay PRIVATE cxx_std_23)
target_compile_options(stitch-replay PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(stitch-replay PRIVATE Qt6::Core Qt6::Gui)

# A standalone install copies Qt into the bundle so it runs on a Mac without
# Homebrew. A Homebrew formula declares qt as a dependency instead, and should
# not ship a second copy of it inside the keg.
option(FOMOSNAP_BUNDLE_QT "Copy Qt into the bundle at install time" ON)
find_program(MACDEPLOYQT macdeployqt HINTS "${QT_HOST_PATH}/bin"
  "${CMAKE_PREFIX_PATH}/bin")
if(FOMOSNAP_BUNDLE_QT AND NOT MACDEPLOYQT)
  message(FATAL_ERROR "macdeployqt not found; set FOMOSNAP_BUNDLE_QT=OFF")
endif()
configure_file(packaging/install-bundle.cmake.in install-bundle.cmake @ONLY)

install(TARGETS fomosnap
  BUNDLE DESTINATION "."
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/install-bundle.cmake")
