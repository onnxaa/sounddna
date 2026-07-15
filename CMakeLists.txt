cmake_minimum_required(VERSION 3.14)
project(SoundDNA VERSION 1.0.0)

if(NOT DEFINED IPLUG2_DIR)
  set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/iPlug2" CACHE PATH "iPlug2 root directory")
endif()

include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3 REQUIRED fftw3f)
pkg_check_modules(SNDFILE REQUIRED sndfile)

set(SOURCES
  src/SDNA_Plugin.cpp
  src/DSP/SDNA_Types.cpp
  src/DSP/SDNA_FFT.cpp
  src/DSP/SDNA_Analyzer.cpp
  src/DSP/SDNA_SpectralProcessor.cpp
  src/DSP/SDNA_DynamicsProcessor.cpp
  src/DSP/SDNA_StereoProcessor.cpp
  src/DSP/SDNA_NoiseProcessor.cpp
  src/DSP/SDNA_TextureProcessor.cpp
  src/DSP/SDNA_AirProcessor.cpp
  src/DSP/SDNA_MovementProcessor.cpp
  src/DSP/SDNA_SpaceProcessor.cpp
  src/DSP/SDNA_GlueProcessor.cpp
  src/DSP/SDNA_ResonanceProcessor.cpp
  src/DSP/SDNA_TransferEngine.cpp
  src/DSP/SDNA_MorphEngine.cpp
)

set(WEB_RESOURCES
  resources/web/index.html
  resources/web/css/style.css
  resources/web/js/app.js
  resources/web/js/dnaMap.js
  resources/web/js/dnaAnalyzer.js
  resources/web/js/dnaTransfer.js
  resources/web/js/dnaCompare.js
  resources/web/js/dnaMorph.js
  resources/web/js/dnaBrowser.js
  resources/web/js/utils.js
)

iplug_add_plugin(${PROJECT_NAME}
  SOURCES ${SOURCES}
  WEB_RESOURCES ${WEB_RESOURCES}
  UI WEBVIEW
  EXCLUDE_FORMATS WAM
)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${FFTW3_INCLUDE_DIRS}
  ${SNDFILE_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  ${FFTW3_LIBRARIES}
  ${SNDFILE_LIBRARIES}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
  USE_FFTW3
)
