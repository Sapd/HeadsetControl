cmake_minimum_required(VERSION 3.12...3.19)
project(headsetcontrol LANGUAGES C CXX)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")
set(CLANG_FORMAT_EXCLUDE_PATTERNS  "build/")

find_package(hidapi REQUIRED)

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

add_subdirectory(lib)
add_subdirectory(cli)
add_subdirectory(tests)

# ------------------------------------------------------------------------------
# C++ Standard
# ------------------------------------------------------------------------------

# Enable C++20 for all targets
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Check minimum compiler versions for C++20 support
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0")
        message(FATAL_ERROR "GCC 13 or higher is required for C++20 support. Found: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "16.0")
        message(FATAL_ERROR "Clang 16 or higher is required for C++20 support. Found: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "15.0")
        message(FATAL_ERROR "Apple Clang 15 or higher is required for C++20 support. Found: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "19.29")
        message(FATAL_ERROR "MSVC 2019 16.10 or higher is required for C++20 support. Found: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
endif()

IF (WIN32)
	set(CMAKE_C_STANDARD_LIBRARIES "-lsetupapi -static-libgcc -static-libstdc++ -lwsock32 -lws2_32 ${CMAKE_CXX_STANDARD_LIBRARIES}")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive")
ENDIF()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------------------------------------------------------------
# Git version
# ------------------------------------------------------------------------------

execute_process(
  COMMAND git describe --tags --dirty=-modified
  OUTPUT_VARIABLE GIT_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
  RESULT_VARIABLE GIT_RESULT
)

# Fallback if git describe fails (no tags, shallow clone, etc.)
if(NOT GIT_VERSION OR NOT GIT_RESULT EQUAL 0)
  execute_process(
    COMMAND git rev-parse --short HEAD
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(GIT_HASH)
    set(GIT_VERSION "0.0.0-${GIT_HASH}")
  else()
    set(GIT_VERSION "0.0.0-unknown")
  endif()
endif()

message(STATUS "HeadsetControl version: ${GIT_VERSION}")

# Configure a header file to pass the version number to the source code
configure_file(
  "${PROJECT_SOURCE_DIR}/lib/version.h.in"
  "${PROJECT_SOURCE_DIR}/lib/version.h"
  @ONLY
)

# ------------------------------------------------------------------------------
# Clang format
# ------------------------------------------------------------------------------

if(ENABLE_CLANG_FORMAT)

    # Try to find clang-format-18 first to match CI version
    find_program(CLANG_FORMAT_BIN
        NAMES clang-format-18 clang-format
        PATHS /opt/homebrew/opt/llvm@18/bin /usr/local/opt/llvm@18/bin
    )

    if(CLANG_FORMAT_BIN STREQUAL "CLANG_FORMAT_BIN-NOTFOUND")
        message(FATAL_ERROR "unable to locate clang-format (try: brew install llvm@18)")
    endif()

    # Warn if not using version 18
    execute_process(
        COMMAND ${CLANG_FORMAT_BIN} --version
        OUTPUT_VARIABLE CLANG_FORMAT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT CLANG_FORMAT_VERSION MATCHES "version 18")
        message(WARNING "Using ${CLANG_FORMAT_VERSION}, but CI uses version 18. Install llvm@18 for consistency.")
    endif()

    file(GLOB_RECURSE ALL_SOURCE_FILES *.c *.cpp *.h *.cxx *.hxx *.hpp *.cc *.ipp)

    # Don't include some common build folders
    set(CLANG_FORMAT_EXCLUDE_PATTERNS ${CLANG_FORMAT_EXCLUDE_PATTERNS} "/CMakeFiles/" "cmake")

    # get all project files file
    foreach (SOURCE_FILE ${ALL_SOURCE_FILES})
        foreach (EXCLUDE_PATTERN ${CLANG_FORMAT_EXCLUDE_PATTERNS})
            string(FIND ${SOURCE_FILE} ${EXCLUDE_PATTERN} EXCLUDE_FOUND)
            if (NOT ${EXCLUDE_FOUND} EQUAL -1)
                list(REMOVE_ITEM ALL_SOURCE_FILES ${SOURCE_FILE})
            endif ()
        endforeach ()
    endforeach ()

    list(APPEND CLANG_FORMAT_BIN_ARGS
        -i
        ${ALL_SOURCE_FILES}
    )

    add_custom_target(
        format
        COMMAND ${CLANG_FORMAT_BIN} ${CLANG_FORMAT_BIN_ARGS}
        COMMENT "formatting code by running clang format"
    )

endif()

# ------------------------------------------------------------------------------
# Clang Tidy
# ------------------------------------------------------------------------------

if(ENABLE_CLANG_TIDY)

    find_program(CLANG_TIDY_BIN NAMES clang-tidy-9 clang-tidy)
    find_program(RUN_CLANG_TIDY_BIN NAMES run-clang-tidy-9.py run-clang-tidy.py)

    if(CLANG_TIDY_BIN STREQUAL "CLANG_TIDY_BIN-NOTFOUND")
        message(FATAL_ERROR "unable to locate clang-tidy")
    endif()

    if(RUN_CLANG_TIDY_BIN STREQUAL "RUN_CLANG_TIDY_BIN-NOTFOUND")
        message(FATAL_ERROR "unable to locate run-clang-tidy.py")
    endif()

    list(APPEND RUN_CLANG_TIDY_BIN_ARGS
        -clang-tidy-binary ${CLANG_TIDY_BIN}
        -header-filter=.*
    )

    add_custom_target(
        tidy
        COMMAND ${RUN_CLANG_TIDY_BIN} ${RUN_CLANG_TIDY_BIN_ARGS}
        COMMENT "running clang tidy"
    )

endif()

# ------------------------------------------------------------------------------
# Library Target
# ------------------------------------------------------------------------------

# Option to build shared library in addition to static
option(BUILD_SHARED_LIBRARY "Build shared library for FFI bindings (Python, etc.)" OFF)

# Create the HeadsetControl static library
add_library(headsetcontrol_lib STATIC ${LIBRARY_SOURCES} ${LIBRARY_HEADERS})

# Set library properties
set_target_properties(headsetcontrol_lib PROPERTIES
    OUTPUT_NAME "headsetcontrol"
    POSITION_INDEPENDENT_CODE ON
)

# Library include directories
target_include_directories(headsetcontrol_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/devices
    ${HIDAPI_INCLUDE_DIRS}
)

# Library dependencies
target_link_libraries(headsetcontrol_lib PUBLIC m ${HIDAPI_LIBRARIES})

# Optionally build shared library for FFI bindings
if(BUILD_SHARED_LIBRARY)
    add_library(headsetcontrol_shared SHARED ${LIBRARY_SOURCES} ${LIBRARY_HEADERS})

    set_target_properties(headsetcontrol_shared PROPERTIES
        OUTPUT_NAME "headsetcontrol"
        VERSION "${GIT_VERSION}"
        SOVERSION 1
    )

    target_include_directories(headsetcontrol_shared PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/devices
        ${HIDAPI_INCLUDE_DIRS}
    )

    target_link_libraries(headsetcontrol_shared PUBLIC m ${HIDAPI_LIBRARIES})

    # Export symbols for the C API on Windows
    if(WIN32)
        target_compile_definitions(headsetcontrol_shared PRIVATE HSC_BUILDING_DLL)
    endif()
endif()

# ------------------------------------------------------------------------------
# CLI Executable
# ------------------------------------------------------------------------------

# Create the CLI executable that links against the library
add_executable(headsetcontrol ${CLI_SOURCES})
target_link_libraries(headsetcontrol PRIVATE headsetcontrol_lib)
target_include_directories(headsetcontrol PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cli)

# ------------------------------------------------------------------------------
# Installation
# ------------------------------------------------------------------------------

install(TARGETS headsetcontrol DESTINATION bin)
install(TARGETS headsetcontrol_lib DESTINATION lib)

# Install shared library if built
if(BUILD_SHARED_LIBRARY)
    install(TARGETS headsetcontrol_shared DESTINATION lib)
endif()

# Install public headers
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/headsetcontrol.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/headsetcontrol_c.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/device.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/result_types.hpp
    DESTINATION include/headsetcontrol
)

# install udev files on linux
if(UNIX AND NOT APPLE AND NOT ${CMAKE_HOST_SYSTEM_NAME} MATCHES "FreeBSD")
    set(rules_file 70-headsets.rules)
    set(udev_rules_dir lib/udev/rules.d/
        CACHE PATH "Path to the directory where udev rules should be installed")
    add_custom_command(
        OUTPUT ${rules_file}
        COMMAND headsetcontrol -u > ${rules_file}
        DEPENDS headsetcontrol)
    add_custom_target(udevrules ALL DEPENDS ${rules_file})
    install(
        FILES ${CMAKE_CURRENT_BINARY_DIR}/${rules_file}
        DESTINATION ${udev_rules_dir})
endif()

# ------------------------------------------------------------------------------
# Testing
# ------------------------------------------------------------------------------

include(CTest)
enable_testing()

# Integration Test: Basic application run
add_test(NAME integration_basic_run
         COMMAND headsetcontrol
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(integration_basic_run
    PROPERTIES PASS_REGULAR_EXPRESSION "No supported device found;Found")

# Test targets
add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
                  DEPENDS headsetcontrol)

add_custom_target(test-verbose
    COMMAND ${CMAKE_CTEST_COMMAND} --verbose --output-on-failure
    DEPENDS headsetcontrol
    COMMENT "Running tests with verbose output"
)

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

option(BUILD_UNIT_TESTS "Build unit tests with mock HID interface" ON)

if(BUILD_UNIT_TESTS)
    add_executable(headsetcontrol_tests ${TEST_SOURCES})
    target_link_libraries(headsetcontrol_tests PRIVATE headsetcontrol_lib)
    target_include_directories(headsetcontrol_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cli)

    add_test(NAME unit_mock_devices
             COMMAND headsetcontrol_tests
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    add_dependencies(check headsetcontrol_tests)
endif()
