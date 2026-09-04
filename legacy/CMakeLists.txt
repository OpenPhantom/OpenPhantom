# Fixes for the retail engine of Star Wars Episode I: The Phantom Menace.
#
# One loader plus one DLL per independent fix. The shared code is a STATIC library linked into
# every DLL, so the artefacts stay independent: there is no runtime DLL that everything else needs,
# and deleting one feature cannot break another.
#
# 32 BIT ONLY. Every offset, pointer patch and operand repoint here assumes a 32 bit process, and
# src/common/engine_types.h asserts it at compile time. Configure with -A Win32.

cmake_minimum_required(VERSION 3.15)
project(engine_fixes C)

# RC is enabled for one reason: every shipped binary carries a version resource, so a log or a
# properties dialog can name the build it came from. Before this, a DLL reported nothing at all and
# a bug report could not say which patch it was against.
enable_language(RC)

# THE PROJECT'S VERSION, and the only copy of it in this tree.
#
# One number covers the patch and the installer that carries it. They have never been released
# apart: every patch release to date has an installer release on the same day. The first two digits
# move when the installer's own behaviour changes, the third when only the payload does.
#
# The installer keeps its own copy in AppVer, because Inno cannot read a CMake variable and wiring
# the build system into the installer would couple two things that are otherwise independent. The
# comment at AppVer says the same, and the two are kept in step by hand.
set(OPENPHANTOM_VERSION_MAJOR 1)
set(OPENPHANTOM_VERSION_MINOR 4)
set(OPENPHANTOM_VERSION_PATCH 1)
set(OPENPHANTOM_VERSION
    "${OPENPHANTOM_VERSION_MAJOR}.${OPENPHANTOM_VERSION_MINOR}.${OPENPHANTOM_VERSION_PATCH}")

# Gives one target the project's version resource. The module name goes into the description and
# the original file name, so a properties dialog names the file it is actually looking at rather
# than the project. Called by add_engine_fix and by the loader, which is the whole set of shipped
# binaries; the unit tests are console programs that ship nowhere and do not get one.
function(add_version_resource target)
    set(OP_MODULE_NAME ${target})
    if(ARGC GREATER 1)
        set(OP_MODULE_ROLE ${ARGV1})
    else()
        set(OP_MODULE_ROLE "OpenPhantom engine fix")
    endif()
    set(generated ${CMAKE_BINARY_DIR}/generated/${target}_version.rc)
    configure_file(${CMAKE_SOURCE_DIR}/src/common/version.rc.in ${generated} @ONLY)
    target_sources(${target} PRIVATE ${generated})
endfunction()

# Generated into the build tree so no generated file is ever committed, and reached as
# "common/version.h" like every other shared header.
configure_file(
    ${CMAKE_SOURCE_DIR}/src/common/version.h.in
    ${CMAKE_BINARY_DIR}/generated/common/version.h
    @ONLY)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "This project targets a 32-bit game. Configure with: cmake -S . -B build -A Win32")
endif()

# The C11 requirement is not decoration: _Static_assert carries the binary layout checks that
# stop a structure from silently disagreeing with the engine's.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    # Warnings are errors. Integer truncation, pointer truncation and signed/unsigned conversions
    # are precisely the mistakes that stay invisible here until the game crashes.
    add_compile_options(/W4 /WX /GS- /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
    # Static CRT: these DLLs are dropped into a 1999 game folder and must not need a redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Where the built artefacts land, laid out the way they are installed: the loader beside the
# executable, the fixes in mods\, the standalone scripts in tools\.
set(ENGINE_FIX_OUTPUT ${CMAKE_BINARY_DIR}/dist)

# The output directory is emptied at configure time, and that is a safety property rather than
# tidiness. CMake never deletes the output of a target that has stopped existing, so after a fix
# is renamed or removed its old DLL simply stays in dist\mods, gets installed alongside the new
# one, and the loader then applies two generations of the same patch to the same engine cells.
# The same reasoning covers tools\: a renamed script would otherwise ship twice.
file(REMOVE_RECURSE ${ENGINE_FIX_OUTPUT}/mods)
file(REMOVE_RECURSE ${ENGINE_FIX_OUTPUT}/tools)

# src/tools has no build step, so its source IS what ships. It is copied here anyway, because a
# release assembled partly by CMake and partly by remembering to copy a folder is a release that
# eventually goes out without that folder. The installer reads tools\ out of this same layout.
#
# The directory is removed before every copy, not only at configure time. copy_directory adds and
# overwrites but never deletes, so a renamed script would otherwise leave its old name sitting
# beside the new one in an incremental build and both would ship - the same class of accident the
# configure-time wipe above exists to prevent for mods\, and it needs solving in the same place the
# copying happens rather than one step earlier.
add_custom_target(tools ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${ENGINE_FIX_OUTPUT}/tools
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/src/tools ${ENGINE_FIX_OUTPUT}/tools
    COMMENT "Copying src/tools into dist/tools"
    VERBATIM
)

enable_testing()

add_subdirectory(src/common)
add_subdirectory(src/loader)
add_subdirectory(src/mods)
add_subdirectory(src/convert)
add_subdirectory(unittests)
