cmake_minimum_required(VERSION 3.18)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(AvaDSP LANGUAGES CXX)
    include(CTest)
endif()

add_library(ava_dsp STATIC
    Engine.cpp Envelope.cpp LowPass.cpp Oscillator.cpp Preset.cpp)
target_compile_features(ava_dsp PUBLIC cxx_std_17)
target_include_directories(ava_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(BUILD_TESTING)
    add_executable(ava_dsp_tests ../tests/dsp_tests.cpp)
    target_link_libraries(ava_dsp_tests PRIVATE ava_dsp)
    add_test(NAME ava_dsp_tests COMMAND ava_dsp_tests)
endif()
