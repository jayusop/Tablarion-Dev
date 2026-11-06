cmake_minimum_required(VERSION 3.22)

# ---- Target CPUs (must be set before project()) ---------------------------------------------------
#   Xcode generator : configurations  Debug         -> this Mac's CPU only (development)
#                                     AppleSilicon  -> arm64          release
#                                     Intel         -> x86_64         release
#                                     Universal     -> arm64 + x86_64 release (one binary for both)
#   Other generators: TABLARION_ARCH = arm64 | x86_64 | native (default)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version (Xcode 27 supports 12.0+)")
set(TABLARION_RELEASE_CONFIGS AppleSilicon Intel Universal)

# This Mac's real CPU (CMAKE_HOST_SYSTEM_PROCESSOR is not known before project()).
execute_process(COMMAND uname -m OUTPUT_VARIABLE TABLARION_HOST_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
if(TABLARION_HOST_ARCH STREQUAL "x86_64")
    execute_process(COMMAND sysctl -n hw.optional.arm64 OUTPUT_VARIABLE _arm OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_arm STREQUAL "1")
        set(TABLARION_HOST_ARCH "arm64") # Terminal running under Rosetta on Apple Silicon
    endif()
endif()
set(_host_arch "${TABLARION_HOST_ARCH}")

if(CMAKE_GENERATOR STREQUAL "Xcode")
    set(CMAKE_CONFIGURATION_TYPES "Debug;AppleSilicon;Intel;Universal" CACHE STRING "Build configurations" FORCE)
    # Architectures are fixed per configuration (see tablarion_apply_xcode_archs).
    set(CMAKE_OSX_ARCHITECTURES "" CACHE STRING "Set per configuration" FORCE)
    set("CMAKE_XCODE_ATTRIBUTE_ARCHS[variant=Debug]" "${TABLARION_HOST_ARCH}")
    set("CMAKE_XCODE_ATTRIBUTE_ARCHS[variant=AppleSilicon]" "arm64")
    set("CMAKE_XCODE_ATTRIBUTE_ARCHS[variant=Intel]" "x86_64")
    set("CMAKE_XCODE_ATTRIBUTE_ARCHS[variant=Universal]" "arm64 x86_64")
    set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "NO")
else()
    set(TABLARION_ARCH "native" CACHE STRING "Target CPU for non-Xcode builds: arm64, x86_64 or native")
    set_property(CACHE TABLARION_ARCH PROPERTY STRINGS arm64 x86_64 native)
    if(TABLARION_ARCH STREQUAL "native")
        set(CMAKE_OSX_ARCHITECTURES "${_host_arch}" CACHE STRING "Target architecture" FORCE)
    elseif(TABLARION_ARCH STREQUAL "arm64" OR TABLARION_ARCH STREQUAL "x86_64")
        set(CMAKE_OSX_ARCHITECTURES "${TABLARION_ARCH}" CACHE STRING "Target architecture" FORCE)
    else()
        message(FATAL_ERROR "TABLARION_ARCH must be arm64, x86_64 or native (got '${TABLARION_ARCH}')")
    endif()
endif()
unset(TABLARION_UNIVERSAL CACHE)

option(TABLARION_INSTALL_PLUGINS
       "Copy the VST3/AU into ~/Library/Audio/Plug-Ins after building (only when they can run on this Mac)" ON)

project(Tablarion VERSION 1.0.0 LANGUAGES C CXX)

# The CPU-specific configurations are full release builds: copy every Release flag set.
foreach(_cfg IN LISTS TABLARION_RELEASE_CONFIGS)
    string(TOUPPER "${_cfg}" _CFG)
    foreach(_lang C CXX OBJC OBJCXX)
        set(CMAKE_${_lang}_FLAGS_${_CFG} "${CMAKE_${_lang}_FLAGS_RELEASE}")
    endforeach()
    foreach(_kind EXE SHARED MODULE STATIC)
        set(CMAKE_${_kind}_LINKER_FLAGS_${_CFG} "${CMAKE_${_kind}_LINKER_FLAGS_RELEASE}")
    endforeach()
endforeach()

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
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST FALSE
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

# JUCE's recommended flags only recognise a configuration named "Release"; give the CPU-specific
# release configurations the same optimisation and link-time optimisation.
add_library(tablarion_release_flags INTERFACE)
target_compile_options(tablarion_release_flags INTERFACE $<$<CONFIG:AppleSilicon,Intel,Universal>:-O3 -flto>)
target_link_options(tablarion_release_flags INTERFACE $<$<CONFIG:AppleSilicon,Intel,Universal>:-flto -Wl,-weak_reference_mismatches,weak>)

target_link_libraries(Tablarion
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        tablarion_release_flags)

# JUCE always compiles its ARA and LV2 support units; with those formats disabled they are empty,
# which makes libtool warn "has no symbols". Silence that specific, harmless warning.
set_target_properties(Tablarion PROPERTIES XCODE_ATTRIBUTE_OTHER_LIBTOOLFLAGS "-no_warning_for_no_symbols")

# JUCE's GUI module still uses Metal "managed storage" APIs deprecated in macOS 27. Silence deprecation
# warnings for that single JUCE file only (our own sources keep every warning).
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/JUCE/modules/juce_gui_basics/juce_gui_basics.mm"
    TARGET_DIRECTORY Tablarion
    PROPERTIES COMPILE_OPTIONS "-Wno-deprecated-declarations")

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

# Install the plug-ins for the DAWs on this Mac - but only a build whose CPU matches this Mac
# (an Intel build made on Apple Silicon, or vice versa, stays in the build folder).
if(TABLARION_INSTALL_PLUGINS)
    set(_install_script "${CMAKE_CURRENT_SOURCE_DIR}/Tools/install_plugin.sh")
    if(TARGET Tablarion_VST3)
        add_custom_command(TARGET Tablarion_VST3 POST_BUILD
            COMMAND /bin/sh "${_install_script}" "$<TARGET_BUNDLE_DIR:Tablarion_VST3>" "$ENV{HOME}/Library/Audio/Plug-Ins/VST3"
            VERBATIM)
    endif()
    if(TARGET Tablarion_AU)
        add_custom_command(TARGET Tablarion_AU POST_BUILD
            COMMAND /bin/sh "${_install_script}" "$<TARGET_BUNDLE_DIR:Tablarion_AU>" "$ENV{HOME}/Library/Audio/Plug-Ins/Components"
            VERBATIM)
    endif()
endif()

# Xcode: CMake writes ARCHS into every target, which would override the project-level per-configuration
# values. Pin the CPU per configuration on each buildable target (deferred so JUCE's late targets are seen).
function(tablarion_apply_xcode_archs)
    if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
        return()
    endif()
    get_property(targets DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(t IN LISTS targets)
        get_target_property(type ${t} TYPE)
        if(type STREQUAL "INTERFACE_LIBRARY")
            continue()
        endif()
        set_target_properties(${t} PROPERTIES
            "XCODE_ATTRIBUTE_ARCHS[variant=Debug]"        "${TABLARION_HOST_ARCH}"
            "XCODE_ATTRIBUTE_ARCHS[variant=AppleSilicon]" "arm64"
            "XCODE_ATTRIBUTE_ARCHS[variant=Intel]"        "x86_64"
            "XCODE_ATTRIBUTE_ARCHS[variant=Universal]"    "arm64 x86_64"
            XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH              "NO")
    endforeach()
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} CALL tablarion_apply_xcode_archs)

if(TABLARION_BUILD_TOOLS)
# ---- UI snapshot tool (development) ------------------------------------------
juce_add_console_app(UISnapshot PRODUCT_NAME "UISnapshot")
target_sources(UISnapshot PRIVATE Tools/UISnapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp ${TABLARION_ENGINE_SOURCES})
target_include_directories(UISnapshot PRIVATE Source)
target_compile_definitions(UISnapshot PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="Tablarion" JucePlugin_VersionString="1.0.0" JucePlugin_Build_Standalone=0 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(UISnapshot PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
endif()
