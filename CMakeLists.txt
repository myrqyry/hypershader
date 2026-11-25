cmake_minimum_required(VERSION 3.28)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

option(USE_ONNX_RUNTIME "Enable ONNX Runtime for LLM and MediaPipe" OFF)
option(USE_LITERT "Enable native LiteRT (TFLite) support" OFF)
option(AUTO_ENABLE_ONNX_RUNTIME "Auto-enable ONNX Runtime support when available" ON)

option(HYPERSHADER_FORCE_OBS_MODULE_VER "Force OBS module version for local ABI mismatch" OFF)
option(HYPERSHADER_DEBUG_ACTIVE_MARKER "Emit DEBUG_ACTIVE marker at module load" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

if(HYPERSHADER_FORCE_OBS_MODULE_VER)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HYPERSHADER_FORCE_OBS_MODULE_VER)
endif()

if(HYPERSHADER_DEBUG_ACTIVE_MARKER)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HYPERSHADER_DEBUG_ACTIVE_MARKER)
endif()

set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

# plugin-support is provided by obs-plugintemplate helpers (when src/plugin-support.c.in exists).
# Ensure it sees OBS headers + bmalloc/bfree declarations.
if(TARGET plugin-support)
  target_link_libraries(plugin-support PRIVATE OBS::libobs)
endif()

target_include_directories(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src
    vendor/entt/single_include
    vendor/kissfft
)

set(
  PLUGIN_SOURCES
  src/filter-hypershader.c
  src/interop/data_driver.cpp
  src/channel_image_compat.c
  src/shader/shader_metadata.c
  src/shader/shader_visibility.c
  src/jsmn.c
  src/hyper_ui.c
  src/audio/audio_processor.c
    src/audio/music_timing.c
  src/hyper_audio.c
  src/imgproc/canny_edge.c
  src/llm/llm_engine.cpp
  src/llm/tokenizer.c
  src/mediapipe/mp_wrapper.cpp
  src/mediapipe/face_surface.c
  src/scene/scene_bridge.cpp
  vendor/kissfft/kiss_fftr.c
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c ${PLUGIN_SOURCES})

if(MSVC)
  # Visual Studio reliably serializes experimental C11 atomics as per-source
  # AdditionalOptions. Directory/cache C flags were accepted by CMake but
  # dropped from the generated .vcxproj on hosted MSVC 19.44.
  set(
    HYPERSHADER_MSVC_C_SOURCES
    src/plugin-main.c
    src/filter-hypershader.c
    src/channel_image_compat.c
    src/shader/shader_visibility.c
    src/shader/shader_metadata.c
    src/jsmn.c
    src/hyper_ui.c
    src/audio/audio_processor.c
    src/audio/music_timing.c
    src/hyper_audio.c
    src/imgproc/canny_edge.c
    src/mediapipe/face_surface.c
    src/llm/tokenizer.c
    vendor/kissfft/kiss_fftr.c
  )
  set_source_files_properties(
    ${HYPERSHADER_MSVC_C_SOURCES}
    PROPERTIES COMPILE_OPTIONS "/std:c17;/experimental:c11atomics"
  )

  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      /wd4267
  )

  if(TARGET plugin-support)
    target_compile_options(
      plugin-support
      PRIVATE
        /wd4267
    )
  endif()
endif()

find_package(Threads REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Threads::Threads)

if(OS_LINUX)
  find_package(X11 QUIET)
  if(X11_FOUND)
    message(STATUS "X11 found — enabling global cursor support")
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${X11_LIBRARIES})
  else()
    message(STATUS "X11 not found — global cursor support disabled")
  endif()
endif()

find_package(CURL QUIET)
if(CURL_FOUND)
  message(STATUS "libcurl found — enabling URL import")
  target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CURL_INCLUDE_DIRS})
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${CURL_LIBRARIES})
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_LIBCURL)
else()
  message(STATUS "libcurl not found — URL import disabled")
endif()

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)

  set(OBS_FRONTEND_API_COMPAT_ALIAS "")
  foreach(_config RELWITHDEBINFO RELEASE DEBUG MINSIZEREL)
    get_target_property(_frontend_api_location OBS::obs-frontend-api IMPORTED_LOCATION_${_config})
    if(_frontend_api_location)
      set(OBS_FRONTEND_API_COMPAT_ALIAS "${_frontend_api_location}")
      break()
    endif()
  endforeach()

  if(NOT OBS_FRONTEND_API_COMPAT_ALIAS)
    get_target_property(OBS_FRONTEND_API_COMPAT_ALIAS OBS::obs-frontend-api IMPORTED_LOCATION)
  endif()
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets QUIET)
  if(NOT Qt6_FOUND)
    message(FATAL_ERROR "ENABLE_QT is ON, but Qt6 was not found")
  endif()

  target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      src/ui/shader_builder_dock.cpp
      src/ui/shader_builder_dock.hpp
      src/ui/shader_performance_model.hpp
      src/ui/shader_performance_panel.hpp
  )

  target_link_libraries(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      Qt6::Widgets
  )

  set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_SHADER_BUILDER_DOCK)
endif()

set(ONNXRUNTIME_FOUND FALSE)
set(ENABLE_ONNX_RUNTIME FALSE)

if(USE_ONNX_RUNTIME OR AUTO_ENABLE_ONNX_RUNTIME)
  set(ONNXRUNTIME_FOUND FALSE)
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(ONNXRUNTIME_PKG QUIET libonnxruntime)
    if(ONNXRUNTIME_PKG_FOUND)
      set(ONNXRUNTIME_FOUND TRUE)
      target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${ONNXRUNTIME_PKG_INCLUDE_DIRS})
      target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE ${ONNXRUNTIME_PKG_LIBRARY_DIRS})
      target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${ONNXRUNTIME_PKG_LIBRARIES})
    endif()
  endif()

  if(NOT ONNXRUNTIME_FOUND)
    find_library(
      ONNXRUNTIME_LIB
      NAMES onnxruntime
      PATHS /usr/lib /usr/local/lib /opt/onnxruntime/lib
      PATH_SUFFIXES lib
    )
    find_path(
      ONNXRUNTIME_INCLUDE
      NAMES onnxruntime_cxx_api.h
      PATHS /usr/include/onnxruntime /usr/local/include/onnxruntime /opt/onnxruntime/include
      PATH_SUFFIXES include
    )

    if(ONNXRUNTIME_LIB AND ONNXRUNTIME_INCLUDE)
      set(ONNXRUNTIME_FOUND TRUE)
      target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${ONNXRUNTIME_INCLUDE})
      target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${ONNXRUNTIME_LIB})
    endif()
  endif()

  if(ONNXRUNTIME_FOUND)
    set(ENABLE_ONNX_RUNTIME TRUE)
  elseif(USE_ONNX_RUNTIME)
    message(FATAL_ERROR "USE_ONNX_RUNTIME is ON but ONNX Runtime was not found. Real MediaPipe inference requires explicit ONNXRUNTIME_INCLUDE and ONNXRUNTIME_LIB or a valid libonnxruntime package.")
  endif()
endif()

if(ENABLE_ONNX_RUNTIME)
  message(STATUS "ONNX Runtime found — enabling real MediaPipe inference")
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE USE_ONNX_RUNTIME USE_ONNX_MEDIAPIPE)
endif()

if(USE_LITERT)
  if(NOT DEFINED LITERT_SDK_ROOT)
    set(LITERT_SDK_ROOT "" CACHE PATH "Path to the litert_cc_sdk directory (contains CMakeLists.txt and libLiteRt.so)")
  endif()

  if(NOT LITERT_SDK_ROOT OR NOT EXISTS "${LITERT_SDK_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "USE_LITERT is ON but LITERT_SDK_ROOT is not set to a valid litert_cc_sdk directory. Download from https://github.com/google-ai-edge/LiteRT/releases/download/v2.1.6/litert_cc_sdk.zip and pass -DLITERT_SDK_ROOT=<path>.")
  endif()

  if(NOT DEFINED LITERT_RUNTIME_DIR)
    set(LITERT_RUNTIME_DIR "${LITERT_SDK_ROOT}" CACHE PATH "Directory containing libLiteRt.so")
  endif()

  if(NOT EXISTS "${LITERT_RUNTIME_DIR}/libLiteRt.so")
    message(FATAL_ERROR "USE_LITERT is ON but libLiteRt.so was not found in LITERT_RUNTIME_DIR=${LITERT_RUNTIME_DIR}. Download it from https://storage.googleapis.com/litert/binaries/2.1.6/linux_x86_64/libLiteRt.so")
  endif()

  message(STATUS "LiteRT 2.x found — enabling CompiledModel API (SDK: ${LITERT_SDK_ROOT})")
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE USE_LITERT)
  # The litert/c headers include a generated build_config.h; produce it here
  # (the SDK normally does this inside its own add_subdirectory, which we
  # bypass to avoid building its abseil/flatbuffers dependency tree).
  set(LITERT_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/include")
  configure_file(
    "${LITERT_SDK_ROOT}/litert/build_common/build_config.h.in"
    "${LITERT_GENERATED_INCLUDE_DIR}/litert/build_common/build_config.h"
  )
  target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE
    ${LITERT_SDK_ROOT}
    ${LITERT_GENERATED_INCLUDE_DIR}
  )
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "${LITERT_RUNTIME_DIR}/libLiteRt.so")
  # libLiteRt.so installs beside the plugin, so $ORIGIN resolves it at runtime.
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES
      INSTALL_RPATH "$ORIGIN"
      BUILD_WITH_INSTALL_RPATH ON
  )
  install(FILES "${LITERT_RUNTIME_DIR}/libLiteRt.so" DESTINATION ${OBS_PLUGIN_DESTINATION})
endif()

if(OS_MACOS OR OS_WINDOWS)
  set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
else()
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES
      VERSION ${PLUGIN_VERSION}
      SOVERSION ${PLUGIN_VERSION_MAJOR}
      PREFIX ""
      OUTPUT_NAME ${_name}
      # OBS 32 loads bin/64bit, while OBS 33 loads the plugin-root copy.
      # Both layouts share bundled ONNX/LiteRT runtime libraries in bin/64bit.
      # Keep the root copy loadable without duplicating the libraries or
      # depending on developer-machine absolute paths.
      INSTALL_RPATH "$ORIGIN;$ORIGIN/bin/64bit"
  )

  add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>"
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
      "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>"
    COMMENT "Copy ${CMAKE_PROJECT_NAME} to rundir"
    VERBATIM
  )

  if(NOT DEFINED OBS_PLUGIN_DESTINATION)
    set(OBS_PLUGIN_DESTINATION "bin/64bit")
  endif()

  if(NOT DEFINED OBS_DATA_DESTINATION)
    set(OBS_DATA_DESTINATION "data")
  endif()

  message(STATUS "OBS plugin install destination: ${OBS_PLUGIN_DESTINATION}")
  message(STATUS "OBS data install destination: ${OBS_DATA_DESTINATION}")

  install(
    TARGETS ${CMAKE_PROJECT_NAME}
    LIBRARY DESTINATION ${OBS_PLUGIN_DESTINATION}
    RUNTIME DESTINATION ${OBS_PLUGIN_DESTINATION}
  )

  # OBS 33+ third-party layout loads the module from the plugin root. Keep
  # bin/64bit for OBS 32 and older, and keep both copies synchronized when
  # installing to a per-user/plugin tree.
  if(OBS_PLUGIN_DESTINATION MATCHES "bin[/\\]64bit$")
    set(_hypershader_modern_plugin_destination "${OBS_PLUGIN_DESTINATION}")
    cmake_path(GET _hypershader_modern_plugin_destination PARENT_PATH _hypershader_modern_plugin_destination)
    cmake_path(GET _hypershader_modern_plugin_destination PARENT_PATH _hypershader_modern_plugin_destination)
    if(_hypershader_modern_plugin_destination STREQUAL "")
      set(_hypershader_modern_plugin_destination ".")
    endif()
    install(
      TARGETS ${CMAKE_PROJECT_NAME}
      LIBRARY DESTINATION "${_hypershader_modern_plugin_destination}"
      RUNTIME DESTINATION "${_hypershader_modern_plugin_destination}"
    )
  endif()

  if(ENABLE_FRONTEND_API AND OBS_FRONTEND_API_COMPAT_ALIAS AND EXISTS "${OBS_FRONTEND_API_COMPAT_ALIAS}")
    install(FILES "${OBS_FRONTEND_API_COMPAT_ALIAS}" DESTINATION ${OBS_PLUGIN_DESTINATION} RENAME obs-frontend-api.so)
  endif()

  install(
    DIRECTORY data/
    DESTINATION "${OBS_DATA_DESTINATION}"
    USE_SOURCE_PERMISSIONS
    PATTERN "platform" EXCLUDE
    PATTERN "transitions" EXCLUDE
  )

  if(USE_ONNX_RUNTIME AND ONNXRUNTIME_LIB AND IS_ABSOLUTE "${ONNXRUNTIME_LIB}")
    get_filename_component(ONNXRUNTIME_LIB_DIR "${ONNXRUNTIME_LIB}" DIRECTORY)
    file(
      GLOB
      ONNXRUNTIME_RUNTIME_LIBS
      "${ONNXRUNTIME_LIB_DIR}/libonnxruntime.so*"
      "${ONNXRUNTIME_LIB_DIR}/libonnxruntime_providers_*.so"
    )
    if(ONNXRUNTIME_RUNTIME_LIBS)
      list(REMOVE_DUPLICATES ONNXRUNTIME_RUNTIME_LIBS)
      install(FILES ${ONNXRUNTIME_RUNTIME_LIBS} DESTINATION ${OBS_PLUGIN_DESTINATION})
    endif()
  endif()
endif()
