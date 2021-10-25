cmake_minimum_required(VERSION 2.8...3.19)
project(headsetcontrol LANGUAGES C)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")
set(CLANG_FORMAT_EXCLUDE_PATTERNS  "build/")

find_package(hidapi REQUIRED)

# ------------------------------------------------------------------------------
# Includes
# ------------------------------------------------------------------------------

include_directories(${HIDAPI_INCLUDE_DIRS})

add_subdirectory(src)
add_subdirectory(src/devices)

# ------------------------------------------------------------------------------
# C Flags
# ------------------------------------------------------------------------------

macro(use_c99)
    if (CMAKE_VERSION VERSION_LESS "3.1")
        if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
            set (CMAKE_C_FLAGS "--std=gnu99 ${CMAKE_C_FLAGS}")
        endif ()
    else ()
        set (CMAKE_C_STANDARD 99)
    endif ()
endmacro(use_c99)

use_c99()

IF (WIN32)
	set(CMAKE_C_STANDARD_LIBRARIES "-lsetupapi -static-libgcc -static-libstdc++ -lwsock32 -lws2_32 ${CMAKE_CXX_STANDARD_LIBRARIES}")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive")
ENDIF()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------------------------------------------------------------
# Clang format
# ------------------------------------------------------------------------------

if(ENABLE_CLANG_FORMAT)

    find_program(CLANG_FORMAT_BIN clang-format)

    if(CLANG_FORMAT_BIN STREQUAL "CLANG_FORMAT_BIN-NOTFOUND")
        message(FATAL_ERROR "unable to locate clang-format")
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
# Executables
# ------------------------------------------------------------------------------

add_executable(headsetcontrol ${SOURCE_FILES})
target_link_libraries(headsetcontrol m ${HIDAPI_LIBRARIES})

install(TARGETS headsetcontrol DESTINATION bin)

# install udev files on linux
if(UNIX AND NOT APPLE)
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

include (CTest)

## Simple test wether we can run the application (should basic hidapi functions, like enumerate, work)
enable_testing()
add_test(run_test headsetcontrol)
set_tests_properties(run_test PROPERTIES PASS_REGULAR_EXPRESSION "No supported headset found")
# use make check to compile+test
add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND}
                  DEPENDS headsetcontrol)
