cmake_minimum_required(VERSION 3.22)

# Mac : binaire universel (Intel + Apple Silicon), à définir avant project()
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project(OniriqueTheChoir VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# AU (Logic, GarageBand) uniquement sur Mac
set(ONIRIQUE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND ONIRIQUE_FORMATS AU)
endif()

juce_add_plugin(OniriqueTheChoir
    COMPANY_NAME                Onirique
    PLUGIN_MANUFACTURER_CODE    Onir
    PLUGIN_CODE                 Ochr
    FORMATS                     ${ONIRIQUE_FORMATS}
    AU_MAIN_TYPE                kAudioUnitType_Effect
    PRODUCT_NAME                "ONIRIQUE THE CHOIR"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    VST3_CATEGORIES             Fx Pitch Shift
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(OniriqueTheChoir PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/OniriqueUI.cpp)

# logo du site (sigil blanc), intégré au binaire
juce_add_binary_data(OniriqueChoirAssets SOURCES Resources/logo.png)

target_include_directories(OniriqueTheChoir PRIVATE Source)

# Licence ONIRIQUE : vérification du fichier écrit par ONIRIQUE CENTRAL, pastille et page d'activation.
include(License/OniriqueLicense.cmake)
onirique_add_license(OniriqueTheChoir)

target_compile_definitions(OniriqueTheChoir PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# Mac : rendu Metal, seules les zones qui bougent sont redessinées (voir VOCALBOX)
if(APPLE)
    target_compile_definitions(OniriqueTheChoir PUBLIC JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS=1)
endif()

target_link_libraries(OniriqueTheChoir
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
        OniriqueChoirAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- essais hors ligne du moteur d'harmonies (hauteur de chaque voix, niveau)
juce_add_console_app(OniriqueChoirTests PRODUCT_NAME "OniriqueChoirTests")
target_sources(OniriqueChoirTests PRIVATE Tests/DspTest.cpp)
target_compile_definitions(OniriqueChoirTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(OniriqueChoirTests
    PRIVATE juce::juce_dsp juce::juce_audio_basics juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# ---- essai de bout en bout : le processeur et l'interface réels du plugin (pas construit par la CI)
if(ONIRIQUE_PLUGIN_TEST)
    juce_add_console_app(OniriqueChoirPluginTest PRODUCT_NAME "OniriqueChoirPluginTest")
    target_sources(OniriqueChoirPluginTest PRIVATE Tests/PluginTest.cpp)
    target_compile_definitions(OniriqueTheChoir PUBLIC JUCE_MODAL_LOOPS_PERMITTED=1)   # boucle de messages pilotée par l'essai
    target_link_libraries(OniriqueChoirPluginTest PRIVATE OniriqueTheChoir)
    # mêmes chemins et réglages que le plugin, sans recompiler JUCE une seconde fois
    target_include_directories(OniriqueChoirPluginTest PRIVATE $<TARGET_PROPERTY:OniriqueTheChoir,INCLUDE_DIRECTORIES>)
    target_compile_definitions(OniriqueChoirPluginTest PRIVATE $<TARGET_PROPERTY:OniriqueTheChoir,COMPILE_DEFINITIONS>)
endif()
