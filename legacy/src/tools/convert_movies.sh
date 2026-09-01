#!/bin/sh
# convert_movies.sh: the Linux and Steam Deck entry point for convert_movies.py.
#
# The Windows entry point is "Convert Movies.bat" beside this, and it cannot be used here: it drives
# convert_movies.ps1, and Wine ships no PowerShell. convert_movies.py does the same job with Python 3
# and whatever ffmpeg the system has.
#
# Two ways to use this:
#   1. ./convert_movies.sh                  - it asks where the game is and what size to make them
#   2. ./convert_movies.sh /path/to/game    - for a Proton or Lutris install, usually somewhere like
#      ~/.steam/steam/steamapps/compatdata/<id>/pfx/drive_c/... or wherever you installed it
#
# Add --height 1080 to enlarge them, or leave it out to keep each film's own size, which is the
# recommendation: they are 640x405 and enlarging costs disc space rather than showing more.
#
# This exists rather than telling people to type "python3 convert_movies.py" because the file may not
# be executable after a zip download and because the right interpreter is not always called python3.

set -e

here=$(dirname "$0")
script="$here/convert_movies.py"

if [ ! -f "$script" ]; then
    echo "convert_movies.py is not next to this script; keep the tools folder together." >&2
    exit 1
fi

if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "Note: no ffmpeg on PATH. The converter will try the copy the installer left in" >&2
    echo "      mods/fmv through Wine. If that is not there either, install your" >&2
    echo "      distribution's ffmpeg package. On the Steam Deck it is already present." >&2
    echo "" >&2
fi

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
