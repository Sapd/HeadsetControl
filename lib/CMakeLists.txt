# ------------------------------------------------------------------------------
# HeadsetControl Library
# ------------------------------------------------------------------------------

set(LIBRARY_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/device_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/globals.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/headsetcontrol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/headsetcontrol_c.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hid_utility.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/result_types.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utility.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/devices/hid_device.cpp
)

set(LIBRARY_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/device.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/device_registry.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/feature_utils.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/headsetcontrol.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/headsetcontrol_c.h
    ${CMAKE_CURRENT_SOURCE_DIR}/hid_utility.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/result_types.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/string_utils.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utility.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/version.h
)

# Export to parent scope
set(LIBRARY_SOURCES ${LIBRARY_SOURCES} PARENT_SCOPE)
set(LIBRARY_HEADERS ${LIBRARY_HEADERS} PARENT_SCOPE)
