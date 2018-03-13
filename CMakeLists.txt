cmake_minimum_required(VERSION 2.6.2)
project(HeadsetControl)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")

find_package(hidapi REQUIRED)

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

include_directories(${HIDAPI_INCLUDE_DIRS})

add_subdirectory(src)
add_subdirectory(src/devices)

add_executable(HeadsetControl ${SOURCE_FILES})
target_link_libraries(HeadsetControl ${HIDAPI_LIBRARIES})

install(TARGETS HeadsetControl DESTINATION bin)

# install udev files on linux
if(UNIX AND NOT APPLE)
    install(DIRECTORY udev/ DESTINATION /etc/udev/rules.d)
endif()


