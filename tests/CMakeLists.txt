# ------------------------------------------------------------------------------
# HeadsetControl Tests
# ------------------------------------------------------------------------------

set(TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/test_runner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_output_formats.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_cli_output.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_library_linkage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_utilities.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_edge_cases.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_hid_interface.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_device_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_string_escaping.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_library_api.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_protocols.cpp
)

# Export to parent scope
set(TEST_SOURCES ${TEST_SOURCES} PARENT_SCOPE)
