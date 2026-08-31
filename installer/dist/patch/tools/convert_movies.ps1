<#
.SYNOPSIS
    Converts this game's own Bink (.bik) movies to a modern format fmv_player.dll can play.

.DESCRIPTION
    A tool, not content: this project ships no game assets, converted or otherwise. Run this
    against your own legally owned copy of the game, and it writes .mp4 files next to nothing you
    did not already have a license to.

    Lives in tools\ - every standalone script this project ships (not a mod DLL, not game content)
    lives there, alongside WMAIN.EXE in an installed copy, so there is one place to look regardless
    of which one you're after.

    Needs FFmpeg - its native Bink decoder (libavcodec/bink.c) is what makes reading .bik files
    possible here without RAD's own SDK. Not a manual install: if no FFmpeg can be found at all,
    this downloads a pinned portable copy (see Resolve-FFmpegExecutable below) and caches it in
    %LOCALAPPDATA%\OpenPhantom\ffmpeg, so it only happens once.

    Output filenames are flattened (GAMEDATA\MOVIE\ARENA.BIK -> arena.mp4) to match how
    fmv_player.c looks them up: for a retail movie name like "movie\arena" it checks
    <MovieDirectory>\arena.<Extension>, no subfolder beyond MovieDirectory itself.

    What this tool is for is getting out of Bink, not out of 1999. The source is 640x405, and the
    overlay window scales whatever it is given to fill the screen anyway, so the defaults hand the
    picture over at its own size and its own frame rate. The one thing they do change is a single
    row: ten of the eleven movies are an odd number of lines tall and H.264 cannot encode that.

.PARAMETER GameDirectory
    The folder WMAIN.EXE lives in. Movies are read from <GameDirectory>\GAMEDATA\MOVIE\*.BIK. If
    omitted, the script asks for it interactively - this is also what happens if you launch
    "Convert Movies.bat" (next to this script) by double-clicking it, or by dragging your game
    folder onto it. Required with -Quiet, which never asks for anything.

.PARAMETER Interactive
    Always passed by "Convert Movies.bat". Prompts for anything not already given on the command
    line - the game folder (if -GameDirectory was not given) and the output video size (if
    -TargetHeight was not given) - instead of silently falling back to defaults for both. Implied
    automatically whenever -GameDirectory is omitted too, so this only needs to be passed
    explicitly to get prompted while ALSO passing -GameDirectory (which is exactly what happens
    when a folder is dragged onto "Convert Movies.bat").

.PARAMETER Quiet
    Non-interactive mode, for an installer or any other program driving this. It never prompts, for
    anything, under any circumstance, prints no banner, and writes one machine-readable line per
    movie to stdout and nothing else there. It requires -GameDirectory, because the one thing it
    must not do is stop and wait for somebody. It cannot be combined with -Interactive: those two
    ask for opposite things, so passing both is an argument error rather than a guess about which
    one was meant.

.PARAMETER OutputDirectory
    Where the converted files go. Defaults to <GameDirectory>\movies_hd, matching fmv_player's own
    default MovieDirectory. An install made before that name was corrected may still carry
    MovieDirectory=movies\_hd in its engine_fixes.ini, and it keeps working, because the DLL reads
    the directory out of the ini rather than assuming this one - point -OutputDirectory at whatever
    your ini actually says, or correct the ini.

.PARAMETER TargetHeight
    Scale the picture to this many lines, with the width following the source's own shape by
    itself. Defaults to 0, which keeps the source resolution, and that is the honest default: the
    source is 640x405, Lanczos cannot put back detail that was never encoded, and most of the extra
    bitrate goes into rendering 1999 compression artefacts larger and sharper. The overlay scales
    whatever it is handed to fill the window either way, so upscaling here mostly buys file size.

    At the source resolution the picture loses at most one row and one column, because ten of the
    eleven retail movies have an odd height and H.264 4:2:0 cannot encode an odd dimension. Without
    that trim x264 refuses those ten outright. An odd value given here is rounded down to the next
    even one for the same reason.

.PARAMETER TargetFps
    Force this frame rate. Defaults to 0, which keeps the source's own rate. Worth being honest
    about what a higher number does and does not do: the source is ~15 fps, and re-encoding at 60
    without motion interpolation does not invent motion - each source frame is simply repeated four
    times, the same picture shown more often rather than a smoother one. What it can buy is
    playback cadence on a display whose refresh rate is not a clean multiple of the source rate,
    and that is the only reason to set it.

.PARAMETER VideoCodec
    Passed to ffmpeg's -c:v. Defaults to libx264 (H.264 High Profile), which has the broadest
    hardware decode support of any option here.

.PARAMETER Crf
    Passed to ffmpeg's -crf (x264 quality, lower is higher quality and larger). Default 18,
    visually lossless for this kind of source.

.PARAMETER Force
    Re-encode even if the destination file already exists. Without it, a file that is already there
    is left exactly as it is, whatever size it was encoded at.

.EXAMPLE
    .\convert_movies.ps1 -GameDirectory "C:\Games\The Phantom Menace"
    Converts everything at the source's own resolution and frame rate. No prompts, since
    -GameDirectory was given and -Interactive was not.

.EXAMPLE
    .\convert_movies.ps1 -GameDirectory "C:\Games\The Phantom Menace" -TargetHeight 1080
    Scales to 1080 lines tall, width following the source's shape, at the source's own frame rate.

.EXAMPLE
    .\convert_movies.ps1 -GameDirectory "C:\Games\The Phantom Menace" -Quiet
    What an installer runs. No prompts, no banner, one line per movie on stdout.

.NOTES
    Exit codes:
      0  everything either converted or was already there
      1  at least one movie failed to convert
      2  bad arguments, or an environment this cannot run in at all: no FFmpeg, no game folder, no
         GAMEDATA\MOVIE under it, or an output directory that cannot be written to

    Under -Quiet, stdout carries one line per movie and then one summary line, and nothing else:
      OK <source file>
      SKIP <source file>
      FAIL <source file> <code>   ffmpeg's exit code, or -1 when ffmpeg reported success but the
                                  finished file could not be put in place
      DONE converted=<n> skipped=<n> failed=<n>
      and, ahead of all of those, exactly one line naming the binary that will do the work:
        FFMPEG <full path>
      That line exists because a caller reading stdout cannot otherwise find out what it
      just ran. The installer writes it into its own log, so "the cutscenes came out wrong"
      can be answered with which FFmpeg produced them instead of a guess.
    Progress notes, warnings and errors go to stderr in that mode, so a caller reading stdout gets
    the transcript above and only that.
#>

# SIZE NOTE: a little over 600 lines, most of it the help above plus the reasoning at each
# decision. The one seam worth considering was measured and rejected: acquiring FFmpeg (find,
# download, verify, extract, cache) is a genuinely separate responsibility from converting, and it
# would split cleanly along that line. What stops it is delivery. This is a script a player runs by
# dragging a folder onto a batch file, so a second file has to be shipped with it, found by it and
# dot-sourced, and a player who ends up with only one of the two gets a failure that names neither.
# One file that is honest about its length beats two that can arrive apart.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string]$GameDirectory = "",

    # Always passed by "Convert Movies.bat", on both its double-click and drag-and-drop paths - a
    # dragged folder already answers the GameDirectory prompt below, but that alone must not skip
    # the size prompt too, which is why this is its own separate switch rather than reusing
    # GameDirectory's blankness as the only interactive signal.
    [switch]$Interactive,

    [switch]$Quiet,

    [string]$OutputDirectory,

    [int]$TargetHeight = 0,
    [int]$TargetFps = 0,

    [string]$VideoCodec = "libx264",

    [int]$Crf = 18,

    [switch]$Force,

    # The exact ffmpeg.exe to use, bypassing the search in Resolve-FFmpegExecutable entirely.
    # The installer passes the copy it carried; see that function for why naming it matters.
    [string]$FFmpegPath = "",

    # Forbids the download branch. The installer passes this because it runs this script from
    # its own elevated process.
    [switch]$NoDownload
)

$ErrorActionPreference = "Stop"

# The pinned FFmpeg build. A rolling "latest" URL cannot be hash checked, and it also means two
# people running this a month apart are not running the same program, so "it failed for me" stops
# being a reproducible statement. The pin is what makes a bug report reproducible: the FFmpeg
# version, its bytes and this script are then all known quantities. A hash that does not match
# refuses the archive - it is deleted, not extracted, because a download that is not the build we
# asked for is either damaged or not ours, and neither is something to unpack and execute.
# FFmpeg publishes no official Windows binaries, so this comes from gyan.dev, one of the two build
# sources FFmpeg's own site names for Windows (https://ffmpeg.org/download.html#build-windows) -
# the "essentials" build, which is smaller than "full" and already contains everything asked for
# here (the Bink decoder, libx264, aac). The size is checked first only because it is free and
# turns the common failure, a truncated or intercepted download, into a clear message instead of a
# hash mismatch that reads like tampering.
$FFMPEG_URL        = "https://github.com/GyanD/codexffmpeg/releases/download/9.0/ffmpeg-9.0-essentials_build.zip"
$FFMPEG_SHA256     = "e6b54767a6065919048f1a098eb27211ca4e12b4348a05d88777a5855d0b6e71"
$FFMPEG_SIZE_BYTES = 111167378

# Everything a person reads goes through these three, and none of them writes to the success
# stream. That is not a style preference: Write-Output inside a function does not print, it returns,
# so a note written that way from a function that also returns a value comes back joined to the
# value. Resolving FFmpeg that way handed the caller the message and the path as one array, and the
# encode line then tried to execute the message as a program.
#
# Under -Quiet stdout belongs to the machine transcript, so the same messages go to stderr instead
# of being dropped: a run still has to be able to say which branch it took, and a caller reading
# stdout still gets only the OK/SKIP/FAIL lines.
function Write-Note {
    param([string]$Message = "")

    if ($Quiet) {
        [Console]::Error.WriteLine($Message)
    } else {
        Write-Host $Message
    }
}

function Write-Problem {
    param([string]$Message)

    # Not Write-Warning under -Quiet: which handle the warning stream ends up on depends on how the
    # caller redirected the host, and stdout there has to stay parseable.
    if ($Quiet) {
        [Console]::Error.WriteLine("WARNING: $Message")
    } else {
        Write-Warning $Message
    }
}

# Fatal messages only - the caller does the exit, so the code is visible at the site that decided
# it. Write-Error is deliberately not used: $ErrorActionPreference = "Stop" makes it throw, which
# terminates the script before the following line runs and shows the user a raw PowerShell error
# record instead of the sentence written here.
function Write-Fatal {
    param([string]$Message)

    if ($Quiet) {
        [Console]::Error.WriteLine("ERROR: $Message")
    } else {
        Write-Host "ERROR: $Message"
    }
}

# Finds ffmpeg.exe, in this order:
#   1. a copy an earlier run of this script downloaded, under %LOCALAPPDATA%.
#   2. whatever is on PATH.
#   3. a fresh download, pinned and hash-checked.
#
# The cache lives under %LOCALAPPDATA% and not next to the script for two separate reasons, and the
# second one is why a folder beside the script is not searched at all.
#
# The first is ordinary: this script sits in the game folder, which may need elevation to write to,
# and a cache there fails AFTER the download rather than before it - the whole archive fetched,
# extraction refused, the download deleted, and the same thing again on every run.
#
# The second is that the game folder is deliberately made writable by ordinary users, because the
# game keeps its saved games inside its own folder and cannot run otherwise. An ffmpeg.exe found
# beside this script would therefore be an executable that any user of the machine can replace,
# and the installer offers to run this tool from its own elevated process. Searching there would
# turn "I can write to my own game folder" into "I can choose what runs as administrator". No
# version convenience is worth that, so that candidate does not exist. %LOCALAPPDATA% belongs to
# one user and is not writable by another.
function Resolve-FFmpegExecutable {
    # An explicit path beats every search below, and it is how the installer names the copy it
    # carried. It matters because the cache is consulted FIRST: without this, a copy left by an
    # earlier run is preferred over the version-pinned binary the installer shipped and just put
    # on PATH for exactly this purpose, and preferred inside an elevated process at that.
    #
    # Named but missing is an error and not a reason to go looking. The caller said which binary
    # it meant, and silently running a different one is precisely the outcome this prevents.
    if (-not [string]::IsNullOrWhiteSpace($FFmpegPath)) {
        if (Test-Path -LiteralPath $FFmpegPath -PathType Leaf) {
            Write-Note "FFmpeg: $FFmpegPath (named by the caller)"
            return $FFmpegPath
        }
        Write-Problem "The FFmpeg named on the command line does not exist: $FFmpegPath"
        return $null
    }

    $localAppData = $env:LOCALAPPDATA
    if ([string]::IsNullOrWhiteSpace($localAppData)) {
        $localAppData = [System.IO.Path]::GetTempPath()
        Write-Note "LOCALAPPDATA is not set, so the FFmpeg cache goes under '$localAppData' instead."
    }
    $cacheRoot = Join-Path $localAppData "OpenPhantom\ffmpeg"

    # Recursive: the archive unpacks into a versioned folder with bin\ inside it, and the exact name
    # changes with every release.
    $cached = Get-ChildItem -LiteralPath $cacheRoot -Filter "ffmpeg.exe" -Recurse -File `
        -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cached) {
        Write-Note "FFmpeg: $($cached.FullName) (downloaded by an earlier run)"
        return $cached.FullName
    }

    $onPath = Get-Command ffmpeg -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($onPath) {
        Write-Note "FFmpeg: $($onPath.Source) (found on PATH)"
        Write-Note "        This script does not know which version that is. If it is older than"
        Write-Note "        the Bink decoder, or was built without libx264, every file will fail."
        return $onPath.Source
    }

    # Refused rather than fetched. The pin and the SHA256 further down make the download itself
    # sound, so this is about WHEN it is allowed to happen rather than whether the file can be
    # trusted: an unattended run that holds administrator rights should not reach the network on
    # its own initiative. A player running the tool themselves afterwards still gets the download.
    if ($NoDownload) {
        Write-Problem "FFmpeg was not found, and this run is not permitted to download one."
        Write-Problem "Install the cutscene component, or put ffmpeg.exe on PATH, and run again."
        return $null
    }

    Write-Note "FFmpeg was not found, so this will download a portable copy (about 106 MB, once"
    Write-Note "only - it is cached in '$cacheRoot' for every run after this) from gyan.dev, one of"
    Write-Note "the Windows build sources FFmpeg's own site recommends."
    Write-Note ""

    # Created before the download, not after it, so that a directory this account cannot write to is
    # reported as a permission problem in the second it takes to find out, rather than after a
    # hundred megabytes have gone over the wire.
    try {
        New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
    } catch {
        Write-Problem "Could not create '$cacheRoot': $($_.Exception.Message)"
        Write-Problem "That is a permissions problem, not a download problem. Install FFmpeg"
        Write-Problem "yourself from https://ffmpeg.org/ and make sure it is on PATH."
        return $null
    }

    $zipPath = Join-Path $cacheRoot "ffmpeg-download.zip"
    $originalProgressPreference = $ProgressPreference
    try {
        # The default progress bar makes Invoke-WebRequest dramatically slower on some connections
        # in Windows PowerShell 5.1 - this is a well-known workaround, not a cosmetic choice.
        $ProgressPreference = "SilentlyContinue"
        Invoke-WebRequest -Uri $FFMPEG_URL -OutFile $zipPath -UseBasicParsing
    } catch {
        Write-Problem "Could not download FFmpeg: $($_.Exception.Message)"
        Write-Problem "Install it yourself from https://ffmpeg.org/ instead, and make sure it is on PATH."
        Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue
        return $null
    } finally {
        $ProgressPreference = $originalProgressPreference
    }

    try {
        $actualSize = (Get-Item -LiteralPath $zipPath).Length
        if ($actualSize -ne $FFMPEG_SIZE_BYTES) {
            Write-Problem "The download is $actualSize bytes, expected $FFMPEG_SIZE_BYTES - it is"
            Write-Problem "incomplete or it is not the build this script pins. Not extracting it."
            return $null
        }

        $actualHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
        if ($actualHash -ne $FFMPEG_SHA256) {
            Write-Problem "The download does not match the SHA256 this script pins. Not extracting it."
            Write-Problem "  expected $FFMPEG_SHA256"
            Write-Problem "  got      $actualHash"
            return $null
        }

        Write-Note "Download verified against the pinned SHA256."
        Expand-Archive -LiteralPath $zipPath -DestinationPath $cacheRoot -Force
    } catch {
        Write-Problem "Could not unpack FFmpeg into '$cacheRoot': $($_.Exception.Message)"
        return $null
    } finally {
        Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue
    }

    $extracted = Get-ChildItem -LiteralPath $cacheRoot -Filter "ffmpeg.exe" -Recurse -File `
        -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $extracted) {
        Write-Problem "FFmpeg was downloaded and verified, but ffmpeg.exe is not inside the archive."
        return $null
    }

    Write-Note "FFmpeg: $($extracted.FullName) (just downloaded)"
    Write-Note ""
    return $extracted.FullName
}

# Arguments are checked before anything is printed, prompted or downloaded, so a call that cannot
# work says so immediately instead of after the user has answered questions.
if ($Quiet -and $Interactive) {
    Write-Fatal "-Quiet and -Interactive contradict each other: one forbids prompts, the other asks for them."
    exit 2
}
if ($Quiet -and [string]::IsNullOrWhiteSpace($GameDirectory)) {
    Write-Fatal "-Quiet needs -GameDirectory: it is never allowed to stop and ask for one."
    exit 2
}
if ($TargetHeight -lt 0) {
    Write-Fatal "-TargetHeight cannot be negative (0 keeps the source's own size)."
    exit 2
}
if ($TargetFps -lt 0) {
    Write-Fatal "-TargetFps cannot be negative (0 keeps the source's own frame rate)."
    exit 2
}

# Captured before the prompt below can overwrite $GameDirectory. True whenever "Convert Movies.bat"
# is what launched this (it always passes -Interactive, on both its double-click and drag-and-drop
# paths) or when no GameDirectory was given at all (running the script bare, with no arguments, is
# treated the same friendly way). A scripted call that passes -GameDirectory and no -Interactive
# gets no prompts at all, only ever the values it was actually given.
$interactive = (-not $Quiet) -and ($Interactive.IsPresent -or [string]::IsNullOrWhiteSpace($GameDirectory))

# No -GameDirectory given. Loops rather than failing on the first bad entry: a typo here is the
# single most likely mistake a non-technical user makes, and re-running the whole script over a
# fixable typo is a worse experience than just asking again.
if ($interactive) {
    Write-Note "This converts Star Wars Episode I: The Phantom Menace's movies (.bik) to .mp4,"
    Write-Note "so fmv_player.dll can play them at full quality instead of the original Bink video."
    Write-Note ""
    while ([string]::IsNullOrWhiteSpace($GameDirectory)) {
        $candidate = (Read-Host "Enter the full path to your game folder (the one WMAIN.EXE is in)").Trim().Trim('"')
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }
        if (-not (Test-Path -LiteralPath (Join-Path $candidate "WMAIN.EXE") -PathType Leaf)) {
            Write-Problem "WMAIN.EXE was not found in '$candidate' - check the path and try again."
            continue
        }
        $GameDirectory = $candidate
    }
    Write-Note ""
}

# Same idea as the GameDirectory prompt above, and gated the same way: only when running
# interactively, and only when the caller did not already ask for a specific height on the command
# line (PSBoundParameters, not the value itself, since 0 is also this script's own default and
# indistinguishable from "the user typed -TargetHeight 0" otherwise).
if ($interactive -and -not $PSBoundParameters.ContainsKey('TargetHeight')) {
    Write-Note "Choose the output video size. The width follows the source's own shape in every"
    Write-Note "case, so nothing here distorts or crops the picture:"
    Write-Note "  1) Keep the original size - recommended; this source has nowhere near 1080p of"
    Write-Note "     real detail, so upscaling mostly enlarges its 1999 compression artefacts"
    Write-Note "  2) 1080 lines tall (1080p)"
    Write-Note "  3) 1440 lines tall (1440p)"
    Write-Note "  4) 2160 lines tall (4K)"
    Write-Note "  5) Enter a custom height"
    $sizeChoice = (Read-Host "Enter 1-5 (press Enter for 1)").Trim()
    if ([string]::IsNullOrWhiteSpace($sizeChoice)) {
        $sizeChoice = "1"
    }
    switch ($sizeChoice) {
        "1" { $TargetHeight = 0 }
        "2" { $TargetHeight = 1080 }
        "3" { $TargetHeight = 1440 }
        "4" { $TargetHeight = 2160 }
        "5" {
            $parsedHeight = 0
            while ($parsedHeight -le 0) {
                $heightAnswer = (Read-Host "Target height in pixels").Trim()
                if (-not [int]::TryParse($heightAnswer, [ref]$parsedHeight) -or $parsedHeight -le 0) {
                    Write-Problem "Enter a whole number greater than 0."
                    $parsedHeight = 0
                }
            }
            $TargetHeight = $parsedHeight
        }
        default {
            $TargetHeight = 0
            Write-Problem "'$sizeChoice' is not one of 1-5 - keeping the original size."
        }
    }
    Write-Note ""
}

# H.264 4:2:0 subsamples chroma two pixels at a time in both directions, so both dimensions have to
# be even. The scale filter below gets that for the width by itself; the height is whatever was
# asked for, so it is fixed here rather than letting x264 reject every single file.
if ($TargetHeight -gt 0 -and ($TargetHeight % 2) -ne 0) {
    $evenHeight = [Math]::Max(2, $TargetHeight - 1)
    Write-Note "Height $TargetHeight is odd, which H.264 4:2:0 cannot encode - using $evenHeight."
    $TargetHeight = $evenHeight
}

if (-not (Test-Path -LiteralPath $GameDirectory -PathType Container)) {
    Write-Fatal "Game directory does not exist: $GameDirectory"
    exit 2
}

$movieSource = Join-Path $GameDirectory "GAMEDATA\MOVIE"
if (-not (Test-Path -LiteralPath $movieSource -PathType Container)) {
    Write-Fatal "No GAMEDATA\MOVIE folder under '$GameDirectory' - is this the game's own directory (the one WMAIN.EXE is in)?"
    exit 2
}

$bikFiles = @(Get-ChildItem -LiteralPath $movieSource -Filter "*.BIK" -File)
if ($bikFiles.Count -eq 0) {
    Write-Problem "No .BIK files found in '$movieSource'."
    if ($Quiet) {
        Write-Output "DONE converted=0 skipped=0 failed=0"
    }
    exit 0
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $GameDirectory "movies_hd"
}
try {
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
} catch {
    Write-Fatal "Could not create the output directory '$OutputDirectory': $($_.Exception.Message)"
    Write-Fatal "This is usually a permissions problem - a game installed under Program Files needs"
    Write-Fatal "an elevated window, or pass -OutputDirectory pointing somewhere writable."
    exit 2
}

# Writability is proved here rather than discovered one file at a time, because the game folder is
# exactly the kind of place that allows creating a directory and then refuses the files in it. The
# process id keeps the probe unique, so a leftover from a killed run is never mistaken for a
# failure to write.
$probeFile = Join-Path $OutputDirectory ".openphantom-write-test-$PID"
try {
    New-Item -ItemType File -Path $probeFile | Out-Null
    Remove-Item -LiteralPath $probeFile -Force -ErrorAction SilentlyContinue
} catch {
    Write-Fatal "Cannot write into the output directory '$OutputDirectory': $($_.Exception.Message)"
    Write-Fatal "That is usually a permissions problem: run this from an elevated window, or pass"
    Write-Fatal "-OutputDirectory pointing somewhere writable."
    exit 2
}

$ffmpegExe = Resolve-FFmpegExecutable
if (-not $ffmpegExe) {
    Write-Fatal "No usable FFmpeg, so nothing can be converted."
    exit 2
}

# Announced on stdout under -Quiet, where the notes above went to stderr and a caller
# reading stdout would otherwise never learn which binary ran. Emitted here rather than at
# the four returns inside the resolver, so every route reports in one shape and none can be
# added later that forgets to.
if ($Quiet) {
    Write-Output "FFMPEG $ffmpegExe"
}

# Building the -vf chain: scale if a height was asked for, make the size encodable if not, and
# force fps only if a rate was given.
#
# scale=-2:<height> deliberately has no pad after it, and re-adding one would undo the point. The
# black bars a pad writes are real pixels in the file, which is a second, permanent aspect ratio
# baked over the source's own. libVLC then fits that already-padded picture into the overlay window
# a second time, letterboxing the letterbox, and on any window that is not the shape of the padded
# box the picture ends up in a small island in the middle of the screen. Handing over the source's
# own shape lets exactly one fit happen, at playback, against the window that is actually there.
# The -2 lets the width follow that shape while staying even, which H.264 4:2:0 requires.
#
# That same requirement is why the other branch is not empty, which it used to be. These movies are
# not a size H.264 can encode. Read out of the Bink headers of a retail pressing:
#
#     ARENA.BIK        640x405     SCENE1..SCENE8.BIK   640x405
#     BIGAPE.BIK       640x469     LOGO.BIK             640x272
#
# Ten of the eleven have an odd height, so at the source's own size x264 refuses ten of them and
# only the logo comes through. Asking for any target height hid it, because scale=-2 makes both
# sides even, which is exactly why this was reported as "conversion fails at the original size".
#
# crop rather than scale or pad: it drops at most one row and one column and leaves every remaining
# pixel exactly as it was decoded, where scaling 405 lines to 404 would resample the whole frame,
# and padding to 406 would bake in the black row this chain avoids everywhere else. The aspect
# ratio moves by a quarter of a percent, which is under a pixel across the width of any window.
# The expressions carry no comma on purpose, because the filters are joined with one.
# ONE MOVIE IS A DIFFERENT SHAPE FROM THE OTHER TEN, and asking for a tall picture is what makes
# that matter. Read out of the Bink headers of a retail pressing:
#
#     LOGO.BIK   640x272   2.35:1     ARENA / SCENE1..8   640x405     BIGAPE   640x469
#
# scale=-2:<height> fixes the height and lets the width follow the shape, so at 2160 lines the logo
# comes out 5082 pixels wide while everything else lands between 2948 and 3413. Most H.264 decoders
# refuse a frame wider than 4096: the file encodes without complaint, libVLC opens it, and the
# picture is BLACK. Reported from a real 4K install where every other movie played.
#
# So the height is capped per movie, at whatever keeps the derived width inside the limit. Only the
# logo is ever capped, and only when a large size was asked for. Capping the height rather than
# adding a width term to the filter keeps the expressions comma-free, which is what lets them be
# joined with a comma below.
$MaxEncodedWidth = 3840

# Bink stores its frame size as two little-endian dwords at offset 20 and 24, right after the magic,
# the file size and the frame count. Nothing else in the header is needed here.
function Get-BinkFrameSize {
    param([string] $Path)

    try {
        $stream = [System.IO.File]::OpenRead($Path)
        try {
            $header = New-Object byte[] 28
            if ($stream.Read($header, 0, 28) -ne 28) { return $null }
            if ([System.Text.Encoding]::ASCII.GetString($header, 0, 3) -ne 'BIK') { return $null }
            $w = [BitConverter]::ToUInt32($header, 20)
            $h = [BitConverter]::ToUInt32($header, 24)
            if ($w -lt 1 -or $h -lt 1 -or $w -gt 16384 -or $h -gt 16384) { return $null }
            return @([int]$w, [int]$h)
        } finally {
            $stream.Dispose()
        }
    } catch {
        return $null
    }
}

# The height to actually encode this one at: the height asked for, unless that would make it too
# wide. A source whose header cannot be read keeps the requested height, which is what happened
# before this existed.
function Get-EncodeHeight {
    param([string] $Path, [int] $Requested)

    if ($Requested -le 0) { return $Requested }
    $size = Get-BinkFrameSize -Path $Path
    if ($null -eq $size) { return $Requested }

    $widest = [math]::Floor($MaxEncodedWidth * $size[1] / $size[0])
    if ($widest -ge $Requested) { return $Requested }
    $capped = [int]([math]::Floor($widest / 2) * 2)      # H.264 4:2:0 needs both sides even
    if ($capped -lt 2) { return $Requested }
    return $capped
}

if ($TargetFps -gt 0) {
    $extraFilters = @("fps=$TargetFps")
} else {
    $extraFilters = @()
}

Write-Note "Converting $($bikFiles.Count) movie(s) from '$movieSource' to '$OutputDirectory'..."
if ($TargetHeight -gt 0) {
    Write-Note "  scaling to $TargetHeight lines, capped so nothing exceeds $MaxEncodedWidth wide"
} else {
    Write-Note "  keeping each movie's own size"
}

$converted = 0
$skipped = 0
$failed = 0

foreach ($bik in $bikFiles) {
    $baseName = $bik.BaseName.ToLowerInvariant()
    $outputPath = Join-Path $OutputDirectory "$baseName.mp4"

    if ((Test-Path -LiteralPath $outputPath) -and -not $Force) {
        if ($Quiet) {
            Write-Output "SKIP $($bik.Name)"
        } else {
            Write-Note "  skip   $($bik.Name) -> $baseName.mp4 (already exists, use -Force to re-encode)"
        }
        $skipped++
        continue
    }

    if (-not $Quiet) {
        Write-Note "  encode $($bik.Name) -> $baseName.mp4"
    }

    # Encoded under a working name and renamed only once ffmpeg has said it finished. A run stopped
    # with Ctrl-C part way through a file leaves a truncated mp4 behind, and +faststart guarantees
    # such a file is unplayable because the index it moves to the front is written last - under the
    # final name, the skip check above would then skip that unplayable file forever. The rename is
    # the only thing that ever produces the final name, so an interrupted run can leave rubbish but
    # never rubbish that looks finished.
    $tempPath = Join-Path $OutputDirectory "$baseName.converting.mp4"

    $ffmpegArgs = @("-y", "-i", $bik.FullName)
    # Per movie, because the cap depends on this one's shape.
    $videoFilters = @()
    if ($TargetHeight -gt 0) {
        $encodeHeight = Get-EncodeHeight -Path $bik.FullName -Requested $TargetHeight
        if ($encodeHeight -ne $TargetHeight) {
            Write-Note ("  {0}: {1} lines rather than {2}, so it stays inside {3} pixels wide" -f `
                        $bik.Name, $encodeHeight, $TargetHeight, $MaxEncodedWidth)
        }
        $videoFilters += "scale=-2:${encodeHeight}:flags=lanczos"
    } else {
        $videoFilters += "crop=trunc(iw/2)*2:trunc(ih/2)*2"
    }
    $videoFilters += $extraFilters

    if ($videoFilters.Count -gt 0) {
        $ffmpegArgs += @("-vf", ($videoFilters -join ","))
    }
    $ffmpegArgs += @(
        "-c:v", $VideoCodec,
        "-preset", "slow",
        "-crf", $Crf,
        "-pix_fmt", "yuv420p",
        "-profile:v", "high",
        "-c:a", "aac",
        "-b:a", "192k",
        "-movflags", "+faststart"
    )
    if ($Quiet) {
        # Silenced through ffmpeg's own flag, never through a PowerShell redirection. ffmpeg writes
        # its routine progress to stderr, and with $ErrorActionPreference = "Stop" a `2>&1` merges
        # that into PowerShell's error stream, where an ordinary progress line becomes a
        # terminating error and kills the script on the first file. Telling ffmpeg not to print is
        # the one safe way to get a quiet run, precisely because nothing is being merged.
        $ffmpegArgs += @("-loglevel", "quiet")
    }
    $ffmpegArgs += $tempPath

    & $ffmpegExe @ffmpegArgs
    $ffmpegExit = $LASTEXITCODE

    # 0 means finished, anything else is ffmpeg's own code, and -1 is reserved for "ffmpeg claimed
    # success but the file is not where it should be" so the two are never confused in a report.
    $failureCode = 0
    $failureReason = ""
    if ($ffmpegExit -ne 0) {
        $failureCode = $ffmpegExit
        $failureReason = "ffmpeg exit code $ffmpegExit"
    } elseif (-not (Test-Path -LiteralPath $tempPath -PathType Leaf)) {
        $failureCode = -1
        $failureReason = "ffmpeg reported success but wrote no file"
    } else {
        try {
            Move-Item -LiteralPath $tempPath -Destination $outputPath -Force
        } catch {
            $failureCode = -1
            $failureReason = "could not put the finished file in place: $($_.Exception.Message)"
        }
    }

    if ($failureCode -eq 0) {
        if ($Quiet) {
            Write-Output "OK $($bik.Name)"
        }
        $converted++
    } else {
        if ($Quiet) {
            Write-Output "FAIL $($bik.Name) $failureCode"
        }
        Write-Problem "  FAILED: $($bik.Name) ($failureReason)"
        Remove-Item -LiteralPath $tempPath -Force -ErrorAction SilentlyContinue
        $failed++
    }
}

if ($Quiet) {
    Write-Output "DONE converted=$converted skipped=$skipped failed=$failed"
} else {
    Write-Note ""
    Write-Note "Done: $converted converted, $skipped skipped, $failed failed."
}

# A skipped file keeps whatever it was encoded with, which is not necessarily what was asked for on
# this run. Saying so matters most in the case that looks like success: somebody picks 1080p,
# watches every file get skipped, and concludes the setting did nothing.
if ($skipped -gt 0 -and ($TargetHeight -gt 0 -or $TargetFps -gt 0)) {
    Write-Note ""
    Write-Note "$skipped file(s) already existed and were left exactly as they were, so the size or"
    Write-Note "frame rate chosen for this run was NOT applied to them. Re-run with -Force to"
    Write-Note "re-encode them."
}

if ($failed -gt 0) {
    exit 1
}
exit 0
