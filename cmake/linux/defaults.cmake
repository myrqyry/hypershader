# CMake Linux defaults module

include_guard(GLOBAL)

# Set default installation directories
include(GNUInstallDirs)

if(CMAKE_INSTALL_LIBDIR MATCHES "(CMAKE_SYSTEM_PROCESSOR)")
  string(REPLACE "CMAKE_SYSTEM_PROCESSOR" "${CMAKE_SYSTEM_PROCESSOR}" CMAKE_INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
endif()

# Enable find_package targets to become globally available targets
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL TRUE)

option(HYPERSHADER_BUILD_OBS_FROM_SOURCE "Build the pinned OBS SDK from source" OFF)

if(HYPERSHADER_BUILD_OBS_FROM_SOURCE)
  include(buildspec)
  set(_hypershader_pinned_libobs_dir "${CMAKE_CURRENT_SOURCE_DIR}/.deps/lib/cmake/libobs")

  # Preserve an explicit CMake or environment override. Otherwise source mode
  # must consume the SDK it just built rather than a host-selected libobs.
  if(NOT libobs_DIR AND DEFINED ENV{libobs_DIR} AND NOT "$ENV{libobs_DIR}" STREQUAL "")
    set(libobs_DIR "$ENV{libobs_DIR}" CACHE PATH "Explicit libobs CMake package" FORCE)
  elseif(NOT libobs_DIR)
    set(
      libobs_DIR
      "${_hypershader_pinned_libobs_dir}"
      CACHE PATH
      "Path to pinned source-built libobsConfig.cmake"
      FORCE
    )
  endif()
endif()

set(CPACK_PACKAGE_NAME "${CMAKE_PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${CMAKE_PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_C_LIBRARY_ARCHITECTURE}")

set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${PLUGIN_AUTHOR}")
if(PLUGIN_EMAIL)
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${PLUGIN_AUTHOR} <${PLUGIN_EMAIL}>")
endif()
set(CPACK_SET_DESTDIR ON)

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25.0 OR NOT CMAKE_CROSSCOMPILING)
  set(CPACK_DEBIAN_DEBUGINFO_PACKAGE ON)
endif()

set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/release")

set(CPACK_SOURCE_GENERATOR "TXZ")
set(
  CPACK_SOURCE_IGNORE_FILES
  ".*~$"
  \\.git/
  \\.github/
  \\.gitignore
  \\.ccache/
  \\.deps/
  \\.meristem/local/
  build_.*
  cmake/\\.CMakeBuildNumber
  release/
)

set(CPACK_VERBATIM_VARIABLES YES)
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-source")
set(CPACK_ARCHIVE_THREADS 0)

include(CPack)

function(_hypershader_set_libobs_from_pkgconfig)
  find_package(PkgConfig QUIET)
  if(NOT PKG_CONFIG_FOUND)
    return()
  endif()

  pkg_check_modules(LIBOBS QUIET IMPORTED_TARGET libobs)
  if(NOT LIBOBS_FOUND)
    return()
  endif()

  if(NOT TARGET OBS::libobs)
    add_library(OBS::libobs INTERFACE IMPORTED)
    target_link_libraries(OBS::libobs INTERFACE PkgConfig::LIBOBS)
  endif()
endfunction()

function(_hypershader_prepare_libobs_dir)
  if(DEFINED libobs_DIR AND libobs_DIR)
    get_filename_component(_libobs_prefix "${libobs_DIR}" DIRECTORY)
    get_filename_component(_libobs_prefix "${_libobs_prefix}" DIRECTORY)
    file(GLOB _libobs_shared_libs "${_libobs_prefix}/libobs.so*")
    if(NOT _libobs_shared_libs)
      message(STATUS "Ignoring stale libobs_DIR cache entry: ${libobs_DIR}")
      unset(libobs_DIR CACHE)
    endif()
  endif()

  if(HYPERSHADER_BUILD_OBS_FROM_SOURCE AND (NOT DEFINED libobs_DIR OR NOT libobs_DIR))
    set(
      libobs_DIR
      "${_hypershader_pinned_libobs_dir}"
      CACHE PATH
      "Path to pinned source-built libobsConfig.cmake"
      FORCE
    )
  elseif(NOT DEFINED libobs_DIR OR NOT libobs_DIR)
    set(_libobs_candidates)
    if(CMAKE_LIBRARY_ARCHITECTURE)
      list(APPEND _libobs_candidates "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake/libobs")
    endif()
    list(APPEND _libobs_candidates "/usr/lib/cmake/libobs" "/usr/local/lib/cmake/libobs")

    foreach(_candidate IN LISTS _libobs_candidates)
      if(EXISTS "${_candidate}/libobsConfig.cmake")
        set(libobs_DIR "${_candidate}" CACHE PATH "Path to the libobs CMake package" FORCE)
        break()
      endif()
    endforeach()
  endif()
endfunction()

_hypershader_prepare_libobs_dir()

find_package(libobs QUIET CONFIG)

if(NOT TARGET OBS::libobs)
  find_package(LibObs QUIET)
endif()

if(NOT TARGET OBS::libobs)
  _hypershader_set_libobs_from_pkgconfig()
endif()

if(NOT TARGET OBS::libobs)
  find_package(LibObs REQUIRED)
  add_library(OBS::libobs ALIAS libobs)

  if(ENABLE_FRONTEND_API)
    find_path(
      obs-frontend-api_INCLUDE_DIR
      NAMES obs-frontend-api.h
      PATHS /usr/include /usr/local/include
      PATH_SUFFIXES obs
    )

    find_library(obs-frontend-api_LIBRARY NAMES obs-frontend-api PATHS /usr/lib /usr/local/lib)

    if(obs-frontend-api_LIBRARY)
      if(NOT TARGET OBS::obs-frontend-api)
        if(IS_ABSOLUTE "${obs-frontend-api_LIBRARY}")
          add_library(OBS::obs-frontend-api UNKNOWN IMPORTED)
          set_property(TARGET OBS::obs-frontend-api PROPERTY IMPORTED_LOCATION "${obs-frontend-api_LIBRARY}")
        else()
          add_library(OBS::obs-frontend-api INTERFACE IMPORTED)
          set_property(TARGET OBS::obs-frontend-api PROPERTY IMPORTED_LIBNAME "${obs-frontend-api_LIBRARY}")
        endif()

        set_target_properties(
          OBS::obs-frontend-api
          PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${obs-frontend-api_INCLUDE_DIR}"
        )
      endif()
    endif()
  endif()

  macro(find_package)
    if(NOT "${ARGV0}" STREQUAL libobs AND NOT "${ARGV0}" STREQUAL obs-frontend-api)
      _find_package(${ARGV})
    endif()
  endmacro()
endif()
