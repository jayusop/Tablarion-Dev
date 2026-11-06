cmake_minimum_required(VERSION 3.22)

# Target platform: must be set before project().
#   TABLARION_ARCH = universal (default) | arm64 (Apple Silicon) | x86_64 (Intel) | native (this Mac's CPU)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version (Xcode 27 supports 12.0+)")
set(TABLARION_ARCH "universal" CACHE STRING "Target CPU: universal, arm64 (Apple Silicon), x86_64 (Intel) or native")
set_property(CACHE TABLARION_ARCH PROPERTY STRINGS universal arm64 x86_64 native)

# Backwards compatibility with the old -DTABLARION_UNIVERSAL=OFF switch
if(DEFINED TABLARION_UNIVERSAL AND NOT TABLARION_UNIVERSAL AND TABLARION_ARCH STREQUAL "universal")
    set(TABLARION_ARCH "native" CACHE STRING "" FORCE)
endif()
unset(TABLARION_UNIVERSAL CACHE)

# CMAKE_HOST_SYSTEM_PROCESSOR is not known before project(), so ask the kernel (arm64 or x86_64)
execute_process(COMMAND uname -m OUTPUT_VARIABLE TABLARION_HOST_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
if(TABLARION_HOST_ARCH STREQUAL "x86_64")
    # A Terminal running under Rosetta reports x86_64 on Apple Silicon: detect the real CPU
    execute_process(COMMAND sysctl -n hw.optional.arm64 OUTPUT_VARIABLE _arm OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_arm STREQUAL "1")
        set(TABLARION_HOST_ARCH "arm64")
    endif()
endif()

if(TABLARION_ARCH STREQUAL "universal")
    set(_archs "arm64;x86_64")
elseif(TABLARION_ARCH STREQUAL "arm64" OR TABLARION_ARCH STREQUAL "x86_64")
    set(_archs "${TABLARION_ARCH}")
elseif(TABLARION_ARCH STREQUAL "native")
    set(_archs "${TABLARION_HOST_ARCH}")
else()
    message(FATAL_ERROR "TABLARION_ARCH must be universal, arm64, x86_64 or native (got '${TABLARION_ARCH}')")
endif()
set(CMAKE_OSX_ARCHITECTURES "${_archs}" CACHE STRING "Target architectures" FORCE)

# Auto-install the plug-ins into ~/Library/Audio/Plug-Ins only when they can run natively on this Mac
# (universal, or built for this Mac's own CPU). A cross build, e.g. Intel on Apple Silicon, stays in the build folder.
if(_archs MATCHES "${TABLARION_HOST_ARCH}")
    set(_install_default ON)
else()
    set(_install_default OFF)
endif()
option(TABLARION_INSTALL_PLUGINS "Copy the VST3/AU into ~/Library/Audio/Plug-Ins after building" ${_install_default})
message(STATUS "Tablarion: target CPU = ${_archs} (host ${TABLARION_HOST_ARCH}), install plug-ins = ${TABLARION_INSTALL_PLUGINS}")

project(Tablarion VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found. Run ./setup.sh first (clones JUCE 8.0.9 into ThirdParty/JUCE).")
endif()
add_subdirectory(ThirdParty/JUCE)
set(TABLARION_SOURCE_URL "https://github.com/YOUR-ACCOUNT/Tablarion" CACHE STRING "Public URL of the corresponding source code (required by the AGPL when distributing binaries)")
option(TABLARION_BUILD_TOOLS "Build developer tools (parser test, offline renderer, UI snapshot)" OFF)

set(TABLARION_ENGINE_SOURCES
    Source/Model/Score.cpp
    Source/IO/GP345Reader.cpp
    Source/IO/GPIFReader.cpp
    Source/Engine/Sequencer.cpp
    Source/Engine/StringInstrument.cpp
    Source/Engine/KeysInstrument.cpp
    Source/Engine/SynthInstrument.cpp
    Source/Engine/DrumInstrument.cpp
    Source/Engine/SoundFont.cpp
    Source/Engine/SF2Instrument.cpp
    Source/Engine/TrackFX.cpp
    Source/Engine/SongEngine.cpp)

if(TABLARION_BUILD_TOOLS)
# ---- Parser test tool -------------------------------------------------------
juce_add_console_app(ScoreTest PRODUCT_NAME "ScoreTest")
target_sources(ScoreTest PRIVATE
    Tools/ScoreTest.cpp
    Source/Model/Score.cpp
    Source/IO/GP345Reader.cpp
    Source/IO/GPIFReader.cpp)
target_compile_definitions(ScoreTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(ScoreTest PRIVATE juce::juce_core PUBLIC juce::juce_recommended_config_flags)

# ---- Offline render tool ----------------------------------------------------

juce_add_console_app(RenderTest PRODUCT_NAME "RenderTest")
target_sources(RenderTest PRIVATE Tools/RenderTest.cpp ${TABLARION_ENGINE_SOURCES})
target_compile_definitions(RenderTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(RenderTest PRIVATE juce::juce_audio_formats juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)

# ---- SoundFont builder (renders the modeled instruments into a GM .sf2) -----
juce_add_console_app(SF2Builder PRODUCT_NAME "SF2Builder")
target_sources(SF2Builder PRIVATE Tools/SF2Builder.cpp ${TABLARION_ENGINE_SOURCES})
target_compile_definitions(SF2Builder PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(SF2Builder PRIVATE juce::juce_audio_formats juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
endif()

# ---- The plug-in: VST3 + AU + Standalone ---------------------------------------
juce_add_plugin(Tablarion
    COMPANY_NAME "Tablarion Audio"
    COMPANY_WEBSITE "${TABLARION_SOURCE_URL}"
    COMPANY_COPYRIGHT "Copyright (C) 2026 the Tablarion contributors. Licensed under the AGPLv3."
    BUNDLE_ID com.tablarion.tablarion
    PRODUCT_NAME "Tablarion"
    VERSION 1.0.0
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${TABLARION_INSTALL_PLUGINS}
    PLUGIN_MANUFACTURER_CODE Tblr
    PLUGIN_CODE Tbln
    FORMATS VST3 AU Standalone
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icon/icon_1024.png"
    MICROPHONE_PERMISSION_ENABLED FALSE
    HARDENED_RUNTIME_ENABLED TRUE)

target_sources(Tablarion PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ${TABLARION_ENGINE_SOURCES})

target_include_directories(Tablarion PRIVATE Source)

target_compile_definitions(Tablarion PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    TABLARION_SOURCE_URL="${TABLARION_SOURCE_URL}")

target_link_libraries(Tablarion
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# JUCE always compiles its ARA and LV2 support units; with those formats disabled they are empty,
# which makes libtool warn "has no symbols". Silence that specific, harmless warning.
set_target_properties(Tablarion PROPERTIES XCODE_ATTRIBUTE_OTHER_LIBTOOLFLAGS "-no_warning_for_no_symbols")

# JUCE's VST3 manifest helper (a build-time tool that writes moduleinfo.json) compiles Steinberg SDK
# code that uses std::wstring_convert, deprecated in recent macOS SDKs. Silence that third-party warning
# for the helper only. JUCE may create the helper late, so apply this at the end of configuration.
function(tablarion_quiet_vst3_helper)
    if(TARGET Tablarion_vst3_helper)
        target_compile_options(Tablarion_vst3_helper PRIVATE -Wno-deprecated-declarations)
    endif()
endfunction()

# JUCE's GUI module still uses Metal "managed storage" APIs deprecated in macOS 27. Silence deprecation
# warnings for that single JUCE file only (our own sources keep every warning).
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/JUCE/modules/juce_gui_basics/juce_gui_basics.mm"
    TARGET_DIRECTORY Tablarion
    PROPERTIES COMPILE_OPTIONS "-Wno-deprecated-declarations")
cmake_language(DEFER DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} CALL tablarion_quiet_vst3_helper)

# Give the VST3 / AU bundles the same icon as the standalone app (Finder & DAW plug-in browsers)
foreach(fmt VST3 AU Standalone)
    if(TARGET Tablarion_${fmt})
        add_custom_command(TARGET Tablarion_${fmt} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
                "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
                "$<TARGET_BUNDLE_CONTENT_DIR:Tablarion_${fmt}>/Resources/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icon/Tablarion.icns"
                "$<TARGET_BUNDLE_CONTENT_DIR:Tablarion_${fmt}>/Resources/Tablarion.icns"
            COMMAND /usr/libexec/PlistBuddy -c "Delete :CFBundleIconFile" "$<TARGET_BUNDLE_CONTENT_DIR:Tablarion_${fmt}>/Info.plist" || true
            COMMAND /usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string Tablarion" "$<TARGET_BUNDLE_CONTENT_DIR:Tablarion_${fmt}>/Info.plist"
            VERBATIM)
    endif()
endforeach()

if(TABLARION_BUILD_TOOLS)
# ---- UI snapshot tool (development) ------------------------------------------
juce_add_console_app(UISnapshot PRODUCT_NAME "UISnapshot")
target_sources(UISnapshot PRIVATE Tools/UISnapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp ${TABLARION_ENGINE_SOURCES})
target_include_directories(UISnapshot PRIVATE Source)
target_compile_definitions(UISnapshot PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="Tablarion" JucePlugin_VersionString="1.0.0" JucePlugin_Build_Standalone=0 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(UISnapshot PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
endif()
