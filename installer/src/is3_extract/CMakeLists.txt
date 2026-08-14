# The helper the installer runs to turn the disc's BIG.Z into big.lab.
#
# Built 32-bit to match every other binary this project ships, and with warnings as errors: this
# code walks a 1990s binary format with offsets read out of a real file, and a silent truncation
# warning there is exactly the kind of defect that would only show up on somebody else's disc.
cmake_minimum_required(VERSION 3.20)
project(is3_extract C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(is3_extract
    main.c
    is3_archive.c
    blast.c
)

if(MSVC)
    target_compile_options(is3_extract PRIVATE /W4 /WX)
    # The archive is read with fopen/fread rather than the Win32 API, so the CRT's "consider the
    # _s variants" advice has nothing safer to offer here and would only be silenced by casts.
    target_compile_definitions(is3_extract PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(is3_extract PRIVATE -Wall -Wextra -Werror)
endif()
