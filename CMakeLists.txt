cmake_minimum_required(VERSION 3.22)
project(AVA VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
option(AVA_ENABLE_LTO "Enable memory-intensive link-time optimization" OFF)
option(AVA_BUILD_PLUGIN "Build the JUCE plugin as well as the portable DSP" ON)
add_subdirectory(dsp)
if(NOT AVA_BUILD_PLUGIN)
    return()
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "JUCE is missing. Run: bash scripts/setup_build.sh")
endif()
add_subdirectory(JUCE)

juce_add_plugin(Ava
    COMPANY_NAME gisilvs
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Juce
    PLUGIN_CODE Dem0
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Ava")
juce_generate_juce_header(Ava)

target_sources(Ava PRIVATE
    plugin/PluginEditor.cpp
    plugin/PluginProcessor.cpp
    plugin/HostRenderer.cpp
    plugin/PresetFile.cpp
    inference/LatentMapper.cpp
    inference/LatentJob.cpp
    inference/TextTokenizer.cpp
    inference/TextGenerator.cpp)
target_compile_features(Ava PRIVATE cxx_std_17)
target_include_directories(Ava PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(Ava PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
set(ORT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/build/onnxruntime-osx-arm64-1.31.0" CACHE PATH "ONNX Runtime SDK")
if(NOT EXISTS "${ORT_ROOT}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR "ONNX Runtime is missing. Run: bash scripts/setup_onnx.sh")
endif()
add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION "${ORT_ROOT}/lib/libonnxruntime.1.31.0.dylib"
    INTERFACE_INCLUDE_DIRECTORIES "${ORT_ROOT}/include")
set(AVA_MAPPER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/models/mapper" CACHE PATH "Exported Ava mapper")
if(NOT EXISTS "${AVA_MAPPER_DIR}/mapper.onnx" OR NOT EXISTS "${AVA_MAPPER_DIR}/mapper.json")
    message(FATAL_ERROR "Export the Ava mapper first, or set AVA_MAPPER_DIR.")
endif()
foreach(wrapper Ava_Standalone Ava_AU Ava_VST3)
    set_property(TARGET ${wrapper} APPEND PROPERTY BUILD_RPATH "@loader_path/../Frameworks")
    add_custom_command(TARGET ${wrapper} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:${wrapper}>/Contents/Resources/mapper"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${AVA_MAPPER_DIR}/mapper.onnx" "${AVA_MAPPER_DIR}/mapper.json"
            "$<TARGET_BUNDLE_DIR:${wrapper}>/Contents/Resources/mapper"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/models/mapper/LICENSE.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/models/mapper/NOTICE" "${CMAKE_CURRENT_SOURCE_DIR}/models/mapper/LICENSE_STABILITY.md"
            "$<TARGET_BUNDLE_DIR:${wrapper}>/Contents/Resources/mapper"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE" "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
            "$<TARGET_BUNDLE_DIR:${wrapper}>/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/licenses"
            "$<TARGET_BUNDLE_DIR:${wrapper}>/Contents/Resources/licenses"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:${wrapper}>/Contents/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_ROOT}/lib/libonnxruntime.1.31.0.dylib"
            "$<TARGET_BUNDLE_DIR:${wrapper}>/Contents/Frameworks/libonnxruntime.1.dylib"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_ROOT}/LICENSE"
            "$<TARGET_BUNDLE_DIR:${wrapper}>/Contents/Resources/ONNXRuntime-LICENSE"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_ROOT}/ThirdPartyNotices.txt"
            "$<TARGET_BUNDLE_DIR:${wrapper}>/Contents/Resources/ONNXRuntime-ThirdPartyNotices.txt")
endforeach()
target_link_libraries(Ava PRIVATE ava_dsp onnxruntime juce::juce_audio_utils juce::juce_cryptography
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_warning_flags)

if(AVA_ENABLE_LTO)
    target_link_libraries(Ava PUBLIC juce::juce_recommended_lto_flags)
endif()

# The large generator runs in a native child process, keeping its heap out of Logic.
add_executable(AvaGenerator inference/GeneratorMain.cpp)
target_compile_features(AvaGenerator PRIVATE cxx_std_17)
target_link_libraries(AvaGenerator PRIVATE Ava)
# The helper uses JUCE Core types, without the plugin-generated JuceHeader.h.
target_include_directories(AvaGenerator PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/modules")
set_property(TARGET AvaGenerator APPEND PROPERTY BUILD_RPATH "@executable_path/../Frameworks")
foreach(wrapper Ava_Standalone Ava_AU Ava_VST3)
    add_dependencies(${wrapper} AvaGenerator)
    add_custom_command(TARGET ${wrapper} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:${wrapper}>/Contents/Helpers"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:AvaGenerator>"
            "$<TARGET_BUNDLE_DIR:${wrapper}>/Contents/Helpers/AvaGenerator")
endforeach()

# Compile plugin checks with the same JUCE settings as the actual processor.
if(BUILD_TESTING)
    target_sources(Ava PRIVATE tests/plugin_checks.cpp)
    add_executable(ava_plugin_tests tests/run_plugin_tests.cpp)
    target_link_libraries(ava_plugin_tests PRIVATE Ava)
    add_dependencies(ava_plugin_tests AvaGenerator)
    add_test(NAME ava_plugin_tests COMMAND ava_plugin_tests)
endif()
