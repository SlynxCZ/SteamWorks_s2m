# The install layout -- CMake twin of makefiles/ambuild/package.ambuild: a
# Source2Mod plugin drops into addons/source2mod/plugins, no metamod .vdf.
set(PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/addons/source2mod/plugins")

set_target_properties(${PROJECT_NAME} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${PLUGIN_OUTPUT_DIR}
        # The .dll on Windows is a RUNTIME artifact.
        RUNTIME_OUTPUT_DIRECTORY ${PLUGIN_OUTPUT_DIR}
)

if (EXISTS ${CMAKE_SOURCE_DIR}/configs)
    add_custom_command(
            TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/configs ${CMAKE_BINARY_DIR}
    )
endif ()
