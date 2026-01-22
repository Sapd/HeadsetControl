cmake_minimum_required(VERSION 3.12...3.19)
project(headsetcontrol LANGUAGES C CXX)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")
set(CLANG_FORMAT_EXCLUDE_PATTERNS  "build/")

find_package(hidapi REQUIRED)

# ------------------------------------------------------------------------------
# Top Level Project Guard
# ------------------------------------------------------------------------------

if(CMAKE_VERSION VERSION_LESS "3.21" OR NOT DEFINED PROJECT_IS_TOP_LEVEL) # PROJECT_IS_TOP_LEVEL is available for CMake 3.21 or later.
    # Fallback: check if the path to the top level of the source tree is equal to the current project source dir.
    if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
        set(PROJECT_IS_TOP_LEVEL ON)
    else()
        set(PROJECT_IS_TOP_LEVEL OFF)
    endif()
endif()

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

add_subdirectory(lib)
add_subdirectory(cli)

if(PROJECT_IS_TOP_LEVEL)
    add_subdirectory(tests)
endif()

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

if(WIN32)
    if(MSVC)
        # MSVC-specific flags
        add_compile_options(/W4 /Zc:preprocessor)
        # Use static runtime to avoid VC++ Redistributable dependency
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        # MinGW/GCC flags - statically link runtime to avoid DLL dependencies
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive")
        set(CMAKE_C_STANDARD_LIBRARIES "-lsetupapi -lwsock32 -lws2_32 ${CMAKE_C_STANDARD_LIBRARIES}")
        set(CMAKE_CXX_STANDARD_LIBRARIES "-lsetupapi -lwsock32 -lws2_32 ${CMAKE_CXX_STANDARD_LIBRARIES}")
        add_compile_options(-Wall)
    endif()
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
endif()
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

if(ENABLE_CLANG_FORMAT AND PROJECT_IS_TOP_LEVEL)

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

if(ENABLE_CLANG_TIDY AND PROJECT_IS_TOP_LEVEL)

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
# On MSVC, use different name to avoid conflict with DLL import library
if(MSVC)
    set_target_properties(headsetcontrol_lib PROPERTIES
        OUTPUT_NAME "headsetcontrol_static"
        POSITION_INDEPENDENT_CODE ON
    )
else()
    set_target_properties(headsetcontrol_lib PROPERTIES
        OUTPUT_NAME "headsetcontrol"
        POSITION_INDEPENDENT_CODE ON
    )
endif()

# Library include directories
target_include_directories(headsetcontrol_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/devices
    ${HIDAPI_INCLUDE_DIRS}
)

# Library dependencies
if(MSVC)
    target_link_libraries(headsetcontrol_lib PUBLIC ${HIDAPI_LIBRARIES})
else()
    target_link_libraries(headsetcontrol_lib PUBLIC m ${HIDAPI_LIBRARIES})
endif()

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

    # Library dependencies (same conditional as static library)
    if(MSVC)
        target_link_libraries(headsetcontrol_shared PUBLIC ${HIDAPI_LIBRARIES})
    else()
        target_link_libraries(headsetcontrol_shared PUBLIC m ${HIDAPI_LIBRARIES})
    endif()

    # Export symbols for the C API on Windows
    if(WIN32)
        target_compile_definitions(headsetcontrol_shared PRIVATE HSC_BUILDING_DLL)
    endif()
endif()

# ------------------------------------------------------------------------------
# CLI Executable
# ------------------------------------------------------------------------------

if(PROJECT_IS_TOP_LEVEL)

    # Create the CLI executable that links against the library
    # On Windows, include the resource file for the application icon
    if(WIN32)
        set(WIN_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/assets/headsetcontrol.rc)
        # Ensure resource compiler can find the icon file
        set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I${CMAKE_CURRENT_SOURCE_DIR}/assets")
    endif()

    add_executable(headsetcontrol ${CLI_SOURCES} ${WIN_RESOURCES})
    target_link_libraries(headsetcontrol PRIVATE headsetcontrol_lib)
    target_include_directories(headsetcontrol PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cli)

    # On MSVC, we need getopt from vcpkg
    if(MSVC)
        find_package(getopt CONFIG REQUIRED)
        target_link_libraries(headsetcontrol PRIVATE $<IF:$<TARGET_EXISTS:getopt::getopt_shared>,getopt::getopt_shared,getopt::getopt_static>)
    endif()

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

endif()

# ------------------------------------------------------------------------------
# Testing
# ------------------------------------------------------------------------------

if(PROJECT_IS_TOP_LEVEL)

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

endif()

# ------------------------------------------------------------------------------
# Unit Tests
# ------------------------------------------------------------------------------

if(PROJECT_IS_TOP_LEVEL)

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

endif()

# ------------------------------------------------------------------------------
# CPack Package Generation
# ------------------------------------------------------------------------------

if(PROJECT_IS_TOP_LEVEL)

    set(CPACK_PACKAGE_NAME "headsetcontrol")
    set(CPACK_PACKAGE_VENDOR "Sapd")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Control USB headsets on Linux, macOS, and Windows")
    set(CPACK_PACKAGE_DESCRIPTION "HeadsetControl is a tool to control USB-connected headsets. \
    It supports setting sidetone, LED lights, equalizer, inactive time, and more for various \
    gaming headsets from Logitech, SteelSeries, Corsair, HyperX, Roccat, and Audeze.")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Sapd/HeadsetControl")
    set(CPACK_PACKAGE_CONTACT "https://github.com/Sapd/HeadsetControl")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
        set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    endif()

    # Extract version components from GIT_VERSION (e.g., "2.7.0" or "2.7.0-123-gabcdef")
    string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" VERSION_MATCH "${GIT_VERSION}")
    if(VERSION_MATCH)
        set(CPACK_PACKAGE_VERSION_MAJOR "${CMAKE_MATCH_1}")
        set(CPACK_PACKAGE_VERSION_MINOR "${CMAKE_MATCH_2}")
        set(CPACK_PACKAGE_VERSION_PATCH "${CMAKE_MATCH_3}")
        set(CPACK_PACKAGE_VERSION "${GIT_VERSION}")
    else()
        # Debian/RPM versions must start with a digit
        # If git version doesn't (e.g., "continuous-12-g91b4f06"), prefix with 0.0.0~
        set(CPACK_PACKAGE_VERSION_MAJOR "0")
        set(CPACK_PACKAGE_VERSION_MINOR "0")
        set(CPACK_PACKAGE_VERSION_PATCH "0")
        set(CPACK_PACKAGE_VERSION "0.0.0~${GIT_VERSION}")
    endif()

    # Debian package settings
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Denis Arnst <git@sapd.eu>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libhidapi-hidraw0 | libhidapi-libusb0")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/Sapd/HeadsetControl")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

    # RPM package settings
    set(CPACK_RPM_PACKAGE_LICENSE "GPLv3")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
    set(CPACK_RPM_PACKAGE_REQUIRES "hidapi")
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

    # NSIS (Windows installer) settings
    set(CPACK_NSIS_DISPLAY_NAME "HeadsetControl")
    set(CPACK_NSIS_PACKAGE_NAME "HeadsetControl")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/Sapd/HeadsetControl")
    set(CPACK_NSIS_HELP_LINK "https://github.com/Sapd/HeadsetControl/wiki")
    set(CPACK_NSIS_CONTACT "git@sapd.eu")
    set(CPACK_NSIS_MODIFY_PATH ON)
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/headsetcontrol.ico")
        set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/headsetcontrol.ico")
        set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/headsetcontrol.ico")
    endif()

    include(CPack)

endif()
