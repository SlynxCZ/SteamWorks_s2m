# Settings every platform shares -- CMake twin of makefiles/ambuild/shared.ambuild.
# The per-platform ones are in linux.cmake / windows.cmake; the compiler flags
# come from the SDK's add_source2mod_plugin().

# plugin-metadata.json is the one place the plugin's name, author and version
# live. tools/version_gen.py turns it into version_gen.h, and the AMBuild build
# calls the same script, so the two cannot drift. The version is the release
# tag and the commit: SEMVER and GITHUB_SHA_SHORT come from the CI, a local
# build says "Local".
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(VERSION_GEN_DIR "${CMAKE_BINARY_DIR}/versioning")

execute_process(
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tools/version_gen.py
                --metadata ${CMAKE_SOURCE_DIR}/plugin-metadata.json
                --output ${VERSION_GEN_DIR}/version_gen.h
        RESULT_VARIABLE VERSION_GEN_RESULT
)

if (NOT VERSION_GEN_RESULT EQUAL 0)
    message(FATAL_ERROR "version_gen.py failed (${VERSION_GEN_RESULT})")
endif ()

# Re-run cmake when the metadata changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/plugin-metadata.json)
