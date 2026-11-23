cmake_minimum_required(VERSION 3.22)
project(Glizzarine VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# all source files are UTF-8 (some UI texts use · and →)
if(MSVC)
    add_compile_options(/utf-8)
endif()

option(GLZ_TESTS "Build the offline renderer and test tools" OFF)
option(GLZ_SANITIZE "Build with AddressSanitizer (+ UBSan on clang/gcc)" OFF)
if(GLZ_SANITIZE)
    if(MSVC)
        add_compile_options(/fsanitize=address /Zi)
    else()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

juce_add_plugin(Glizzarine
    COMPANY_NAME                "NebulaAudio"
    PLUGIN_MANUFACTURER_CODE    Nbla
    PLUGIN_CODE                 Glzr
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "Glizzarine"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Synth)

target_sources(Glizzarine PRIVATE PluginProcessor.cpp PluginEditor.cpp WaveBank.cpp)
target_compile_definitions(Glizzarine PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 GLZ_VERSION="v1.0.1")
target_link_libraries(Glizzarine
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

if(GLZ_TESTS)
    # the plugin code without the plugin wrapper, shared by the test tools
    add_library(glz_testlib STATIC PluginProcessor.cpp PluginEditor.cpp WaveBank.cpp)
    target_include_directories(glz_testlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(glz_testlib PUBLIC JUCE_STANDALONE_APPLICATION=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0
        JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="Glizzarine" JucePlugin_Build_VST3=0 JucePlugin_Build_Standalone=0 GLZ_VERSION="v1.0.1")
    target_link_libraries(glz_testlib PUBLIC juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
    foreach(t test_screenshots test_qa test_fuzz test_bounce test_soak test_cpu)
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${t}.cpp)
            add_executable(${t} ${t}.cpp)
            target_link_libraries(${t} PRIVATE glz_testlib)
        endif()
    endforeach()

    # engine-only tools: the real engine without plugin code
    add_executable(render render.cpp WaveBank.cpp)
    target_include_directories(render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(render PRIVATE JUCE_STANDALONE_APPLICATION=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(render PRIVATE juce::juce_audio_processors juce::juce_dsp juce::juce_recommended_config_flags)
endif()
