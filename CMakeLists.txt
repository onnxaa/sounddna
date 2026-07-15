cmake_minimum_required(VERSION 3.14)
project(SoundDNA VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT DEFINED IPLUG2_DIR)
  set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/iPlug2" CACHE PATH "iPlug2 root directory")
endif()

set(IPLUG_DIR ${IPLUG2_DIR}/IPlug)
set(WDL_DIR ${IPLUG2_DIR}/WDL)
set(DEPS_DIR ${IPLUG2_DIR}/Dependencies)
set(IGRAPHICS_DIR ${IPLUG2_DIR}/IGraphics)
set(IGRAPHICS_DEPS_DIR ${DEPS_DIR}/IGraphics)
set(IPLUG_DEPS_DIR ${DEPS_DIR}/IPlug)

# ── Linux-specific setup ─────────────────────────────────────────────────────
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # Patch files BEFORE find_package(iPlug2) includes them

  # Patch CLAP.cmake for Linux
  if(EXISTS "${IPLUG2_DIR}/Scripts/cmake/CLAP.cmake"
     AND NOT EXISTS "${IPLUG2_DIR}/Scripts/cmake/CLAP_LINUX_PATCHED")
    file(READ "${IPLUG2_DIR}/Scripts/cmake/CLAP.cmake" CLAP_CMAKE_CONTENT)
    string(REPLACE
      "elseif(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)\n    # Linux support - to be added later\n    message(WARNING \"CLAP Linux support not yet implemented\")"
      "elseif(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)\n    # Linux CLAP support\n    # No additional sources needed - header only"
      CLAP_CMAKE_CONTENT "${CLAP_CMAKE_CONTENT}")
    string(REPLACE
      "elseif(CMAKE_SYSTEM_NAME STREQUAL \"Emscripten\")\n    # WASM/Emscripten"
      "elseif(CMAKE_SYSTEM_NAME STREQUAL \"Linux\")\n    set_target_properties(\${target} PROPERTIES\n      OUTPUT_NAME \"\${project_name}\"\n      SUFFIX \".clap\"\n      LIBRARY_OUTPUT_DIRECTORY \"\${CMAKE_BINARY_DIR}/out\"\n    )\n  elseif(CMAKE_SYSTEM_NAME STREQUAL \"Emscripten\")\n    # WASM/Emscripten"
      CLAP_CMAKE_CONTENT "${CLAP_CMAKE_CONTENT}")
    file(WRITE "${IPLUG2_DIR}/Scripts/cmake/CLAP.cmake" "${CLAP_CMAKE_CONTENT}")
    file(WRITE "${IPLUG2_DIR}/Scripts/cmake/CLAP_LINUX_PATCHED" "")
    message(STATUS "Patched CLAP.cmake for Linux")
  endif()

  # Patch VST3.cmake for Linux
  if(EXISTS "${IPLUG2_DIR}/Scripts/cmake/VST3.cmake"
     AND NOT EXISTS "${IPLUG2_DIR}/Scripts/cmake/VST3_LINUX_PATCHED")
    file(READ "${IPLUG2_DIR}/Scripts/cmake/VST3.cmake" VST3_CMAKE_CONTENT)
    string(REPLACE
      "elseif(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)\n    # Linux support - to be added later\n    message(WARNING \"VST3 Linux support not yet implemented\")"
      "elseif(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)\n    # Linux VST3 support (handled by pre-created stub target)"
      VST3_CMAKE_CONTENT "${VST3_CMAKE_CONTENT}")
    file(WRITE "${IPLUG2_DIR}/Scripts/cmake/VST3.cmake" "${VST3_CMAKE_CONTENT}")
    file(WRITE "${IPLUG2_DIR}/Scripts/cmake/VST3_LINUX_PATCHED" "")
    message(STATUS "Patched VST3.cmake for Linux")
  endif()

  # Patch IPlugTimer.h
  if(EXISTS "${IPLUG_DIR}/IPlugTimer.h"
     AND NOT EXISTS "${IPLUG_DIR}/IPlugTimer_LINUX_PATCHED")
    file(READ "${IPLUG_DIR}/IPlugTimer.h" TIMER_H_CONTENT)
    string(REPLACE "#else\n  #error NOT IMPLEMENTED"
      "#elif defined OS_LINUX\nclass Timer_impl : public Timer\n{\npublic:\n  Timer_impl(ITimerFunction func, uint32_t intervalMs);\n  ~Timer_impl();\n  void Stop() override;\nprivate:\n  unsigned int mTag = 0;\n  ITimerFunction mTimerFunc;\n};\n#else\n  #error NOT IMPLEMENTED"
      TIMER_H_CONTENT "${TIMER_H_CONTENT}")
    file(WRITE "${IPLUG_DIR}/IPlugTimer.h" "${TIMER_H_CONTENT}")
    file(WRITE "${IPLUG_DIR}/IPlugTimer_LINUX_PATCHED" "")
    message(STATUS "Patched IPlugTimer.h for Linux")
  endif()

  # Patch IPlugCLAP.cpp for Linux X11
  if(EXISTS "${IPLUG_DIR}/CLAP/IPlugCLAP.cpp"
     AND NOT EXISTS "${IPLUG_DIR}/CLAP/IPlugCLAP_LINUX_PATCHED")
    file(READ "${IPLUG_DIR}/CLAP/IPlugCLAP.cpp" IPLUG_CLAP_CONTENT)
    string(REPLACE
      "#elif defined OS_WIN\n  return !isFloating && !strcmp(api, CLAP_WINDOW_API_WIN32);\n#else\n#error Not Implemented!"
      "#elif defined OS_WIN\n  return !isFloating && !strcmp(api, CLAP_WINDOW_API_WIN32);\n#elif defined OS_LINUX\n  return !isFloating && !strcmp(api, CLAP_WINDOW_API_X11);\n#else\n#error Not Implemented!"
      IPLUG_CLAP_CONTENT "${IPLUG_CLAP_CONTENT}")
    string(REPLACE
      "#elif defined OS_WIN\n  return GUIWindowAttach(pWindow->win32);\n#else\n#error Not Implemented!"
      "#elif defined OS_WIN\n  return GUIWindowAttach(pWindow->win32);\n#elif defined OS_LINUX\n  mWindow = (void*)(intptr_t)pWindow->x11;\n  OpenWindow(mWindow);\n  mGUIOpen = true;\n  return true;\n#else\n#error Not Implemented!"
      IPLUG_CLAP_CONTENT "${IPLUG_CLAP_CONTENT}")
    string(REPLACE "pWindow->x11.window" "pWindow->x11"
      IPLUG_CLAP_CONTENT "${IPLUG_CLAP_CONTENT}")
    file(WRITE "${IPLUG_DIR}/CLAP/IPlugCLAP.cpp" "${IPLUG_CLAP_CONTENT}")
    file(WRITE "${IPLUG_DIR}/CLAP/IPlugCLAP_LINUX_PATCHED" "")
    message(STATUS "Patched IPlugCLAP.cpp for Linux X11")
  endif()

  # Patch IPlugVST3_View.h for Linux X11
  if(EXISTS "${IPLUG_DIR}/VST3/IPlugVST3_View.h"
     AND NOT EXISTS "${IPLUG_DIR}/VST3/IPlugVST3_View_LINUX_PATCHED")
    file(READ "${IPLUG_DIR}/VST3/IPlugVST3_View.h" VST3_VIEW_CONTENT)
    string(REPLACE
      "#elif defined OS_MAC\n      if (strcmp(type, Steinberg::kPlatformTypeNSView) == 0)\n        return Steinberg::kResultTrue;\n#endif"
      "#elif defined OS_MAC\n      if (strcmp(type, Steinberg::kPlatformTypeNSView) == 0)\n        return Steinberg::kResultTrue;\n#elif defined OS_LINUX\n      if (strcmp(type, Steinberg::kPlatformTypeX11EmbedWindowID) == 0)\n        return Steinberg::kResultTrue;\n#endif"
      VST3_VIEW_CONTENT "${VST3_VIEW_CONTENT}")
    string(REPLACE
      "#elif defined OS_MAC\n      if (strcmp(type, Steinberg::kPlatformTypeNSView) == 0)\n        pView = mOwner.OpenWindow(pParent);\n      else // Carbon\n        return Steinberg::kResultFalse;\n#endif"
      "#elif defined OS_MAC\n      if (strcmp(type, Steinberg::kPlatformTypeNSView) == 0)\n        pView = mOwner.OpenWindow(pParent);\n      else // Carbon\n        return Steinberg::kResultFalse;\n#elif defined OS_LINUX\n      if (strcmp(type, Steinberg::kPlatformTypeX11EmbedWindowID) == 0)\n        pView = mOwner.OpenWindow(pParent);\n      else\n        return Steinberg::kResultFalse;\n#endif"
      VST3_VIEW_CONTENT "${VST3_VIEW_CONTENT}")
    file(WRITE "${IPLUG_DIR}/VST3/IPlugVST3_View.h" "${VST3_VIEW_CONTENT}")
    file(WRITE "${IPLUG_DIR}/VST3/IPlugVST3_View_LINUX_PATCHED" "")
    message(STATUS "Patched IPlugVST3_View.h for Linux X11")
  endif()

  # Download VST3 SDK if needed
  if(EXISTS "${IPLUG_DEPS_DIR}/VST3_SDK"
     AND NOT EXISTS "${IPLUG_DEPS_DIR}/VST3_SDK/base/source/baseiids.cpp")
    message(STATUS "Downloading VST3 SDK...")
    execute_process(
      COMMAND bash "${IPLUG2_DIR}/Dependencies/IPlug/download-vst3-sdk.sh"
      WORKING_DIRECTORY "${IPLUG_DEPS_DIR}"
      RESULT_VARIABLE VST3_DL_RESULT
    )
    if(VST3_DL_RESULT EQUAL 0)
      message(STATUS "VST3 SDK downloaded successfully")
    else()
      message(WARNING "VST3 SDK download failed (${VST3_DL_RESULT})")
    endif()
  endif()

  # Download CLAP SDKs if needed
  if(EXISTS "${IPLUG_DEPS_DIR}/CLAP_SDK"
     AND NOT EXISTS "${IPLUG_DEPS_DIR}/CLAP_SDK/include/clap/entry.h")
    message(STATUS "Downloading CLAP SDKs...")
    execute_process(
      COMMAND bash "${IPLUG2_DIR}/Dependencies/IPlug/download-clap-sdks.sh"
      WORKING_DIRECTORY "${IPLUG_DEPS_DIR}"
      RESULT_VARIABLE CLAP_DL_RESULT
    )
    if(CLAP_DL_RESULT EQUAL 0)
      message(STATUS "CLAP SDKs downloaded successfully")
    else()
      message(WARNING "CLAP SDK download failed (${CLAP_DL_RESULT})")
    endif()
  endif()

  find_package(PkgConfig REQUIRED)
  pkg_check_modules(WEBKIT2GTK REQUIRED IMPORTED_TARGET webkit2gtk-4.1)
  pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
  pkg_check_modules(GLIB2 REQUIRED IMPORTED_TARGET glib-2.0)

  # Pre-create iPlug2::IPlug so IPlug.cmake doesn't hit "Linux not yet supported"
  if(NOT TARGET iPlug2::IPlug)
    add_library(iPlug2::IPlug INTERFACE IMPORTED)
    target_sources(iPlug2::IPlug INTERFACE
      ${IPLUG_DIR}/IPlugAPIBase.h ${IPLUG_DIR}/IPlugAPIBase.cpp
      ${IPLUG_DIR}/IPlugConstants.h
      ${IPLUG_DIR}/IPlugEditorDelegate.h
      ${IPLUG_DIR}/IPlugLogger.h
      ${IPLUG_DIR}/IPlugMidi.h
      ${IPLUG_DIR}/IPlugParameter.h ${IPLUG_DIR}/IPlugParameter.cpp
      ${IPLUG_DIR}/IPlugPaths.h ${IPLUG_DIR}/IPlugPaths.cpp
      ${IPLUG_DIR}/IPlugPlatform.h
      ${IPLUG_DIR}/IPlugPluginBase.h ${IPLUG_DIR}/IPlugPluginBase.cpp
      ${IPLUG_DIR}/IPlugProcessor.h ${IPLUG_DIR}/IPlugProcessor.cpp
      ${IPLUG_DIR}/IPlugQueue.h
      ${IPLUG_DIR}/IPlugStructs.h
      ${IPLUG_DIR}/IPlugTimer.h ${IPLUG_DIR}/IPlugTimer.cpp
      ${IPLUG_DIR}/IPlugUtilities.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/IPlugTimer_linux.cpp
    )
    target_include_directories(iPlug2::IPlug INTERFACE
      ${IPLUG_DIR} ${WDL_DIR} ${WDL_DIR}/libpng ${WDL_DIR}/zlib
      ${IPLUG_DIR}/Extras
    )
    target_compile_definitions(iPlug2::IPlug INTERFACE
      NOMINMAX
      $<$<CONFIG:Debug>:DEBUG>
      $<$<CONFIG:Debug>:_DEBUG>
    )
    target_link_libraries(iPlug2::IPlug INTERFACE pthread dl PkgConfig::GLIB2)
  endif()

  # Stub iPlug2::IGraphics — we use WEBVIEW, not IGRAPHICS
  if(NOT TARGET iPlug2::IGraphics)
    add_library(iPlug2::IGraphics INTERFACE IMPORTED)
  endif()

  # Pre-create dummy FlexBox object to prevent IGraphics.cmake from compiling FlexBox
  if(NOT TARGET iPlug2_Extras_FlexBox_obj)
    file(WRITE "${CMAKE_BINARY_DIR}/_dummy_flexbox.cpp"
      "// Dummy — FlexBox not needed on Linux (WEBVIEW/NO_IGRAPHICS)\n"
      "namespace iplug { namespace igraphics { void _dummyFlexBox() {} } }\n"
    )
    add_library(iPlug2_Extras_FlexBox_obj OBJECT
      "${CMAKE_BINARY_DIR}/_dummy_flexbox.cpp"
    )
    target_include_directories(iPlug2_Extras_FlexBox_obj PRIVATE
      ${IPLUG_DIR} ${WDL_DIR}
    )
  endif()

  # Pre-create iPlug2::WebView for Linux (WebView.cmake only handles APPLE/WIN32/Emscripten)
  if(NOT TARGET iPlug2::WebView)
    add_library(iPlug2::WebView INTERFACE IMPORTED)
    target_sources(iPlug2::WebView INTERFACE
      ${IPLUG_DIR}/Extras/WebView/IPlugWebViewEditorDelegate.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/linux/IPlugWebView_linux.cpp
    )
    target_include_directories(iPlug2::WebView INTERFACE
      ${IPLUG_DIR}/Extras/WebView
      ${DEPS_DIR}/Extras/nlohmann
    )
    target_compile_definitions(iPlug2::WebView INTERFACE
      WEBVIEW_EDITOR_DELEGATE
      NO_IGRAPHICS
    )
    target_link_libraries(iPlug2::WebView INTERFACE
      iPlug2::IPlug
      PkgConfig::WEBKIT2GTK
      PkgConfig::GTK3
    )
  endif()
endif()

# ── Include iPlug2 ──────────────────────────────────────────────────────────
include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 QUIET)

# ── Linux post-find_package setup ───────────────────────────────────────────
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # Add missing VST3 SDK timer.cpp
  if(TARGET iPlug2::VST3)
    target_sources(iPlug2::VST3 INTERFACE
      ${IPLUG_DEPS_DIR}/VST3_SDK/base/source/timer.cpp
    )
  endif()
endif()

# Disable deploy on Linux
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(IPLUG_DEPLOY_PLUGINS OFF)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3 REQUIRED fftw3)
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

set(PLATFORM_SOURCES "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND PLATFORM_SOURCES src/linux/IPlugPaths_linux.cpp)
endif()

# ── Generate embedded UI resource from web files ──────────────────────────
set(WEB_DIR "${CMAKE_SOURCE_DIR}/resources/web")
set(UI_RESOURCE_SRC "${CMAKE_BINARY_DIR}/generated/UIResource.cpp")

# Read and inline all web resources into a C++ source file
file(READ "${WEB_DIR}/index.html" HTML_CONTENT)

file(READ "${WEB_DIR}/css/style.css" CSS_CONTENT)
string(REPLACE "<link rel=\"stylesheet\" href=\"css/style.css\">"
  "<style>\n${CSS_CONTENT}\n</style>" HTML_CONTENT "${HTML_CONTENT}")

foreach(JS_FILE js/utils.js js/dnaMap.js js/dnaAnalyzer.js js/dnaTransfer.js
               js/dnaCompare.js js/dnaMorph.js js/dnaBrowser.js js/app.js)
  file(READ "${WEB_DIR}/${JS_FILE}" JS_CONTENT)
  string(REPLACE "<script src=\"${JS_FILE}\"></script>"
    "<script>\n${JS_CONTENT}\n</script>" HTML_CONTENT "${HTML_CONTENT}")
endforeach()

file(WRITE "${UI_RESOURCE_SRC}"
  "// Auto-generated UI resource file. Do not edit.\n"
  "#include \"IPlugPlatform.h\"\n\n"
  "BEGIN_IPLUG_NAMESPACE\n\n"
  "const char* kSoundDNAUIHTML = R\"NRUI_END(\n"
  "${HTML_CONTENT}\n"
  ")NRUI_END\";\n\n"
  "END_IPLUG_NAMESPACE\n")

list(APPEND SOURCES ${UI_RESOURCE_SRC})

# Re-run generation when any web file changes
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${WEB_DIR}/index.html ${WEB_DIR}/css/style.css ${WEB_DIR}/js/utils.js
  ${WEB_DIR}/js/dnaMap.js ${WEB_DIR}/js/dnaAnalyzer.js ${WEB_DIR}/js/dnaTransfer.js
  ${WEB_DIR}/js/dnaCompare.js ${WEB_DIR}/js/dnaMorph.js ${WEB_DIR}/js/dnaBrowser.js
  ${WEB_DIR}/js/app.js)

iplug_add_plugin(${PROJECT_NAME}
  SOURCES ${SOURCES} ${PLATFORM_SOURCES}
  WEB_RESOURCES ${WEB_RESOURCES}
  UI WEBVIEW
  EXCLUDE_FORMATS WAM APP
  LINK ${FFTW3_LIBRARIES} ${SNDFILE_LIBRARIES}
  DEFINES USE_FFTW3
)

set_source_files_properties(${UI_RESOURCE_SRC} PROPERTIES GENERATED TRUE)

target_include_directories(_${PROJECT_NAME}-base INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${FFTW3_INCLUDE_DIRS}
  ${SNDFILE_INCLUDE_DIRS}
)

# ── Linux post-build: VST3 bundle + deploy ─────────────────────────────────
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET ${PROJECT_NAME}-vst3)
  set(VST3_BUNDLE_DIR "${CMAKE_BINARY_DIR}/out/${PROJECT_NAME}.vst3")
  set(VST3_ARCH_DIR "${VST3_BUNDLE_DIR}/Contents/x86_64-linux")
  set(VST3_DEPLOY_DIR "$ENV{HOME}/.vst3/${PROJECT_NAME}.vst3")
  add_custom_command(TARGET ${PROJECT_NAME}-vst3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${VST3_ARCH_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy
      "$<TARGET_FILE:${PROJECT_NAME}-vst3>"
      "${VST3_ARCH_DIR}/${PROJECT_NAME}.so"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_SOURCE_DIR}/resources/web"
      "${VST3_BUNDLE_DIR}/Contents/Resources/web"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${VST3_DEPLOY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${VST3_BUNDLE_DIR}" "${VST3_DEPLOY_DIR}"
    COMMENT "Creating VST3 bundle at ${VST3_BUNDLE_DIR}, deploying to ${VST3_DEPLOY_DIR}"
  )
endif()

# ── Linux post-build: deploy CLAP ──────────────────────────────────────────
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET ${PROJECT_NAME}-clap)
  set(CLAP_DEPLOY_DIR "$ENV{HOME}/.clap")
  add_custom_command(TARGET ${PROJECT_NAME}-clap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CLAP_DEPLOY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy
      "$<TARGET_FILE:${PROJECT_NAME}-clap>"
      "${CLAP_DEPLOY_DIR}/${PROJECT_NAME}.clap"
    COMMENT "Deploying CLAP to ${CLAP_DEPLOY_DIR}"
  )
endif()
