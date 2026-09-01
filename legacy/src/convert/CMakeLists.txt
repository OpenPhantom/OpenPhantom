# The converter Setup runs, on Windows and under Wine alike. See openphantom_convert.c for why a
# Windows console executable is the answer to a Linux problem.
#
# It lands in dist/tools beside the scripts, because that is where every standalone tool this
# project ships lives and where the installer reads them from.
add_executable(openphantom_convert openphantom_convert.c)

# The tools target wipes dist/tools and copies src/tools into it. Without this dependency the wipe
# can run after the link and delete the executable that was just built, which is the sort of failure
# that appears only in a clean build on somebody else's machine.
add_dependencies(openphantom_convert tools)

set_target_properties(openphantom_convert PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         ${ENGINE_FIX_OUTPUT}/tools
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${ENGINE_FIX_OUTPUT}/tools
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${ENGINE_FIX_OUTPUT}/tools
)
