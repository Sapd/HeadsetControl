cmake_minimum_required(VERSION 2.6.2)
project(HeadsetControl)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")

find_package(hidapi REQUIRED)

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

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------------------------------------------------------------
# Clang Tidy
# ------------------------------------------------------------------------------

if(ENABLE_CLANG_TIDY)

    find_program(CLANG_TIDY_BIN clang-tidy)
    find_program(RUN_CLANG_TIDY_BIN run-clang-tidy.py)

    if(CLANG_TIDY_BIN STREQUAL "CLANG_TIDY_BIN-NOTFOUND")
        message(FATAL_ERROR "unable to locate clang-tidy")
    endif()

    if(RUN_CLANG_TIDY_BIN STREQUAL "RUN_CLANG_TIDY_BIN-NOTFOUND")
        message(FATAL_ERROR "unable to locate run-clang-tidy.py")
    endif()

    list(APPEND RUN_CLANG_TIDY_BIN_ARGS
        -clang-tidy-binary ${CLANG_TIDY_BIN}
        -header-filter=.*
        -quiet
    )

    add_custom_target(
        tidy
        COMMAND ${RUN_CLANG_TIDY_BIN} ${RUN_CLANG_TIDY_BIN_ARGS}
        COMMENT "running clang tidy"
    )

endif()

# ------------------------------------------------------------------------------
# Includes and Executables
# ------------------------------------------------------------------------------

include_directories(${HIDAPI_INCLUDE_DIRS})

add_subdirectory(src)
add_subdirectory(src/devices)

add_executable(HeadsetControl ${SOURCE_FILES})
target_link_libraries(HeadsetControl m ${HIDAPI_LIBRARIES})

install(TARGETS HeadsetControl DESTINATION bin)

# install udev files on linux
if(UNIX AND NOT APPLE)
    install(DIRECTORY udev/ DESTINATION /etc/udev/rules.d)
endif()


# ------------------------------------------------------------------------------
# Testing
# ------------------------------------------------------------------------------

## Simple test wether we can run the application (should basic hidapi functions, like enumerate, work)
enable_testing()
add_test(run_test HeadsetControl)
set_tests_properties(run_test PROPERTIES PASS_REGULAR_EXPRESSION "No supported headset found")
