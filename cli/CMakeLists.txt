# ------------------------------------------------------------------------------
# HeadsetControl CLI
# ------------------------------------------------------------------------------

set(CLI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/dev.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/output/output.cpp
)

# Export to parent scope
set(CLI_SOURCES ${CLI_SOURCES} PARENT_SCOPE)
