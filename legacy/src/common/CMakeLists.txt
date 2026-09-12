# The shared layer. A STATIC library on purpose: each feature DLL links its own copy, so nothing
# at run time depends on anything else being loaded.
#
# The include directory is src/ rather than src/common/, so the loader, every feature and every
# test write `#include "common/logging.h"` and no file counts dots to find a header. Inside this
# directory most sources include their neighbours by bare name, which the compiler finds beside
# the including file; three of them use the `common/` form as well.

add_library(engine_fixes_common STATIC
    host_image.c
    platform.c
    logging.c
    ini.c
    memory.c
    patch.c
    detour.c
    signature.c
    import_patch.c
    frame_hook.c
    menu_patcher.c
    cinematic_gate.c
    stick.c
)

target_include_directories(engine_fixes_common PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated)

target_link_libraries(engine_fixes_common PUBLIC kernel32 user32)
