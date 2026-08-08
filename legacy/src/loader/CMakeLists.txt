# The loader takes the dinput.dll name, so it is installed beside WMAIN.EXE rather than in mods\.
#
# It patches nothing itself. It writes five bytes over the host's entry point, and the stub puts
# them back, loads every DLL in mods\ and calls each one's engine_fix_install export.

add_library(dinput SHARED
    dll_main.c
    early_trigger.c
    dinput_proxy.c
    mod_loader.c
)

target_link_libraries(dinput PRIVATE engine_fixes_common)

set_target_properties(dinput PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${ENGINE_FIX_OUTPUT}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${ENGINE_FIX_OUTPUT}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${ENGINE_FIX_OUTPUT}
)
