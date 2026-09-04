# The shared layer. A STATIC library on purpose: each feature DLL links its own copy, so nothing
# at run time depends on anything else being loaded.
#
# The include directory is src/ rather than src/common/, which is why every file in the tree
# writes `#include "common/logging.h"` and no file counts dots to find a header.

add_library(engine_fixes_common STATIC
    host_image.c
    platform.c
    logging.c
    ini.c
    memory.c
    patch.c
    detour.c
    signature.c
    frame_hook.c
    menu_patcher.c
    cinematic_gate.c
)

target_include_directories(engine_fixes_common PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(engine_fixes_common PUBLIC kernel32 user32)
