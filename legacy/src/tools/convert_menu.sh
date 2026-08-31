#!/bin/sh
# convert_menu.sh: the Linux and Steam Deck entry point for convert_menu.py.
#
# The Windows entry point is "Convert Menu Art.bat" beside this, and it cannot be used here: it
# drives convert_menu.ps1, Wine ships no PowerShell, and that script's resampling is GDI+, which is
# Windows-only on .NET Core. convert_menu.py does the same job with nothing but Python 3.
#
# Two ways to use this:
#   1. ./convert_menu.sh                     - it asks where the game is
#   2. ./convert_menu.sh /path/to/game       - for a Proton install that is usually something like
#      ~/.steam/steam/steamapps/compatdata/<id>/pfx/drive_c/... or wherever you installed it
#
# Add --remove to undo a conversion, or just delete the menu_hd folder it wrote.
#
# This exists rather than telling people to type "python3 convert_menu.py" because the file may not
# be executable after a zip download and because the right interpreter is not always called python3.

set -e

here=$(dirname "$0")
script="$here/convert_menu.py"

if [ ! -f "$script" ]; then
    echo "convert_menu.py is not next to this script; keep the tools folder together." >&2
    exit 1
fi

# python3 on nearly everything, python on the few where 3 is the only one installed. The version
# test is what stops a Python 2 named "python" from getting a script it cannot parse.
for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1; then
        if "$candidate" -c 'import sys; sys.exit(0 if sys.version_info[0] >= 3 else 1)' 2>/dev/null
        then
            exec "$candidate" "$script" "$@"
        fi
    fi
done

echo "Python 3 was not found. On the Steam Deck it is already there; on a distribution that has" >&2
echo "trimmed it, install the 'python3' package and run this again." >&2
exit 1
