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
# executable, the fixes in mods\.
set(ENGINE_FIX_OUTPUT ${CMAKE_BINARY_DIR}/dist)

# The output directory is emptied at configure time, and that is a safety property rather than
# tidiness. CMake never deletes the output of a target that has stopped existing, so after a fix
# is renamed or removed its old DLL simply stays in dist\mods, gets installed alongside the new
# one, and the loader then applies two generations of the same patch to the same engine cells.
file(REMOVE_RECURSE ${ENGINE_FIX_OUTPUT}/mods)

enable_testing()

add_subdirectory(src/common)
add_subdirectory(src/loader)
add_subdirectory(src/mods)
add_subdirectory(unittests)
