# Writes shared Xcode schemes for each CPU build of Tablarion:
#   "Tablarion - Apple Silicon"  -> configuration AppleSilicon (arm64)
#   "Tablarion - Intel"          -> configuration Intel (x86_64)
#   "Tablarion - Debug"          -> configuration Debug (this Mac's CPU)
# Usage: cmake -DXCODEPROJ=build/Tablarion.xcodeproj -P Tools/xcode_schemes.cmake
if(NOT XCODEPROJ OR NOT EXISTS "${XCODEPROJ}/project.pbxproj")
    message(FATAL_ERROR "XCODEPROJ must point to a generated Tablarion.xcodeproj")
endif()
file(READ "${XCODEPROJ}/project.pbxproj" pbx)

function(find_target_id section name out)
    string(FIND "${pbx}" "/* Begin ${section} section */" begin)
    string(FIND "${pbx}" "/* End ${section} section */" end)
    if(begin EQUAL -1 OR end EQUAL -1)
        message(FATAL_ERROR "section ${section} not found")
    endif()
    math(EXPR len "${end} - ${begin}")
    string(SUBSTRING "${pbx}" ${begin} ${len} body)
    string(REGEX MATCH "([0-9A-F]+) /\\* ${name} \\*/ = {" m "${body}")
    if(NOT m)
        message(FATAL_ERROR "target ${name} not found in ${section}")
    endif()
    set(${out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

find_target_id(PBXAggregateTarget Tablarion_All all_id)
find_target_id(PBXNativeTarget Tablarion_Standalone app_id)
# xcodebuild only resolves the targets with an absolute container path (as CMake's own schemes use);
# re-run setup.sh if the project folder is moved.
get_filename_component(container "${XCODEPROJ}" ABSOLUTE)

set(dir "${XCODEPROJ}/xcshareddata/xcschemes")
file(MAKE_DIRECTORY "${dir}")

foreach(entry "Apple Silicon|AppleSilicon" "Intel|Intel" "Debug|Debug")
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 label)
    list(GET parts 1 cfg)
    file(WRITE "${dir}/Tablarion - ${label}.xcscheme" "<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<Scheme LastUpgradeVersion = \"2700\" version = \"1.7\">
   <BuildAction parallelizeBuildables = \"YES\" buildImplicitDependencies = \"YES\">
      <BuildActionEntries>
         <BuildActionEntry buildForTesting = \"YES\" buildForRunning = \"YES\" buildForProfiling = \"YES\" buildForArchiving = \"YES\" buildForAnalyzing = \"YES\">
            <BuildableReference BuildableIdentifier = \"primary\" BlueprintIdentifier = \"${all_id}\" BuildableName = \"Tablarion_All\" BlueprintName = \"Tablarion_All\" ReferencedContainer = \"container:${container}\">
            </BuildableReference>
         </BuildActionEntry>
      </BuildActionEntries>
   </BuildAction>
   <TestAction buildConfiguration = \"${cfg}\" selectedDebuggerIdentifier = \"Xcode.DebuggerFoundation.Debugger.LLDB\" selectedLauncherIdentifier = \"Xcode.DebuggerFoundation.Launcher.LLDB\" shouldUseLaunchSchemeArgsEnv = \"YES\">
   </TestAction>
   <LaunchAction buildConfiguration = \"${cfg}\" selectedDebuggerIdentifier = \"Xcode.DebuggerFoundation.Debugger.LLDB\" selectedLauncherIdentifier = \"Xcode.DebuggerFoundation.Launcher.LLDB\" launchStyle = \"0\" useCustomWorkingDirectory = \"NO\" ignoresPersistentStateOnLaunch = \"NO\" debugDocumentVersioning = \"YES\" debugServiceExtension = \"internal\" allowLocationSimulation = \"YES\">
      <BuildableProductRunnable runnableDebuggingMode = \"0\">
         <BuildableReference BuildableIdentifier = \"primary\" BlueprintIdentifier = \"${app_id}\" BuildableName = \"Tablarion.app\" BlueprintName = \"Tablarion_Standalone\" ReferencedContainer = \"container:${container}\">
         </BuildableReference>
      </BuildableProductRunnable>
   </LaunchAction>
   <ProfileAction buildConfiguration = \"${cfg}\" shouldUseLaunchSchemeArgsEnv = \"YES\" savedToolIdentifier = \"\" useCustomWorkingDirectory = \"NO\" debugDocumentVersioning = \"YES\">
   </ProfileAction>
   <AnalyzeAction buildConfiguration = \"${cfg}\">
   </AnalyzeAction>
   <ArchiveAction buildConfiguration = \"${cfg}\" revealArchiveInOrganizer = \"YES\">
   </ArchiveAction>
</Scheme>
")
    message(STATUS "Xcode scheme: Tablarion - ${label} (${cfg})")
endforeach()
