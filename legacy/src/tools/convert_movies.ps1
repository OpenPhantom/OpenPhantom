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
    possible here without RAD's own SDK. Not a manual install: if FFmpeg is not already on PATH,
    this downloads a portable copy for you automatically (see Resolve-FFmpegExecutable below) and
    caches it in tools\ffmpeg\, so it only happens once.

    Output filenames are flattened (GAMEDATA\MOVIE\ARENA.BIK -> arena.mp4) to match how
    fmv_player.c looks them up: for a retail movie name like "movie\arena" it checks
    <MovieDirectory>\arena.<Extension>, no subfolder beyond MovieDirectory itself.

.PARAMETER GameDirectory
    The folder WMAIN.EXE lives in. Movies are read from <GameDirectory>\GAMEDATA\MOVIE\*.BIK. If
    omitted, the script asks for it interactively - this is also what happens if you launch
    "Convert Movies.bat" (next to this script) by double-clicking it, or by dragging your game
    folder onto it.

.PARAMETER Interactive
    Always passed by "Convert Movies.bat". Prompts for anything not already given on the command
    line - the game folder (if -GameDirectory was not given), the target video size (if
    -TargetWidth/-TargetHeight were not given), and whether to letterbox or stretch (if -Stretch
    was not given, and only if a size was actually chosen to scale to) - instead of silently
    falling back to defaults for all of them. Implied automatically whenever -GameDirectory is
    omitted too, so this only needs to be passed explicitly to get prompted while ALSO passing
    -GameDirectory (which is exactly what happens when a folder is dragged onto
    "Convert Movies.bat").

.PARAMETER OutputDirectory
    Where the converted files go. Defaults to <GameDirectory>\movies\_hd, matching this DLL's own
    default MovieDirectory=movies\_hd setting in engine_fixes.ini.

.PARAMETER TargetWidth
    Upscale to this width. Defaults to 1920 (paired with TargetHeight's default of 1080): the
    source is roughly 640x405 at native size, and FFmpeg's own Lanczos scaler run once here,
    offline, generally beats scaling the same frame every time it is shown at playback - which is
    what would otherwise happen anyway, just repeated on every frame instead of once. 4K source
    material was considered and deliberately not used as the default: fmv_player's overlay window
    scales whatever resolution it is given to fill the screen either way, and this game's Bink 1
    movies were never rendered with anywhere near 1080p of real detail to begin with, so encoding
    past 1080p roughly quadruples file size for a difference nobody will actually see. Set both
    TargetWidth and TargetHeight to 0 to keep the source resolution untouched instead.

.PARAMETER TargetHeight
    See TargetWidth. Both must be set together (or both left at their defaults) - one without the
    other is not used.

.PARAMETER TargetFps
    Defaults to 60. Worth being honest about what this does and does not do: the source is ~15 fps,
    and re-encoding at 60 without motion interpolation does not invent motion - each source frame
    is simply repeated 4 times, the same picture shown more often rather than a smoother one. What
    it buys instead is playback smoothness: a constant, standard 60 fps is a cadence video players
    and displays schedule reliably, where an odd native rate like ~15 fps can judder on monitors
    whose refresh rate is not a clean multiple of it. Set to 0 to keep the source's own frame rate
    untouched instead.

.PARAMETER Stretch
    The source is roughly 4:3. Off by default, which letterboxes/pillarboxes instead (black bars):
    fmv_player's overlay window generally is not 4:3, and libVLC already fits the video into
    whatever window it is given without distortion. Pass this to force the picture to fill the
    frame instead, at the cost of visible stretching on anything that isn't a 4:3 display. Asked
    interactively, when a scale is actually happening at all, the same way -TargetWidth/-TargetHeight
    are - see -Interactive.

.PARAMETER VideoCodec
    Passed to ffmpeg's -c:v. Defaults to libx264 (H.264 High Profile), which has the broadest
    hardware decode support of any option here.

.PARAMETER Crf
    Passed to ffmpeg's -crf (x264 quality, lower is higher quality and larger). Default 18,
    visually lossless for this kind of source.

.PARAMETER Force
    Re-encode even if the destination file already exists.

.EXAMPLE
    .\convert_movies.ps1 -GameDirectory "C:\Games\The Phantom Menace"
    Converts everything, upscaled to 1080p at a constant 60 fps (the defaults). No prompts, since
    -GameDirectory was given and -Interactive was not.

.EXAMPLE
    .\convert_movies.ps1 -GameDirectory "C:\Games\The Phantom Menace" -TargetWidth 0 -TargetHeight 0 -TargetFps 0
    Converts everything at the source's own native resolution and frame rate instead, untouched.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string]$GameDirectory = "",

    # Always passed by "Convert Movies.bat", on both its double-click and drag-and-drop paths - a
    # dragged folder already answers the GameDirectory prompt below, but that alone must not skip
    # the size prompt too, which is why this is its own separate switch rather than reusing
    # GameDirectory's blankness as the only interactive signal.
    [switch]$Interactive,

    [string]$OutputDirectory,

    [int]$TargetWidth = 1920,
    [int]$TargetHeight = 1080,
    [int]$TargetFps = 60,
    [switch]$Stretch,

    [string]$VideoCodec = "libx264",

    [int]$Crf = 18,

    [switch]$Force
)

$ErrorActionPreference = "Stop"

# Finds ffmpeg.exe, in order: already on PATH; a copy this function already downloaded on a
# previous run (cached in tools\ffmpeg\, next to this script - a non-technical user should never
# see the same download twice); otherwise downloads one. FFmpeg itself does not publish official
# Windows binaries, so the download comes from gyan.dev, one of the two build sources FFmpeg's own
# site names for Windows (https://ffmpeg.org/download.html#build-windows) - the "essentials" build
# specifically, which is smaller than the "full" one and already includes everything this script
# actually asks for (libx264, aac). There is no fixed checksum to pin here: that URL always points
# at whatever the current release is, by design, so a hash baked into this script would go stale
# the moment gyan.dev published a new FFmpeg version - the download happening over HTTPS, from the
# source FFmpeg's own project links to, is the trust boundary this accepts instead.
function Resolve-FFmpegExecutable {
    $onPath = Get-Command ffmpeg -ErrorAction SilentlyContinue
    if ($onPath) {
        return $onPath.Source
    }

    $localRoot = Join-Path $PSScriptRoot "ffmpeg"
    $cached = Get-ChildItem -LiteralPath $localRoot -Filter "ffmpeg.exe" -Recurse -File `
        -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cached) {
        return $cached.FullName
    }

    Write-Output "FFmpeg was not found, so this will download a portable copy (about 80 MB, one"
    Write-Output "time only - it is cached in tools\ffmpeg\ for every run after this) from"
    Write-Output "gyan.dev, one of the Windows build sources FFmpeg's own site recommends."
    Write-Output ""

    $zipPath = Join-Path ([System.IO.Path]::GetTempPath()) "ffmpeg-release-essentials.zip"
    $originalProgressPreference = $ProgressPreference
    try {
        # The default progress bar makes Invoke-WebRequest dramatically slower on some connections
        # in Windows PowerShell 5.1 - this is a well-known workaround, not a cosmetic choice.
        $ProgressPreference = "SilentlyContinue"
        Invoke-WebRequest -Uri "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip" `
            -OutFile $zipPath -UseBasicParsing
    } catch {
        Write-Warning "Could not download FFmpeg: $($_.Exception.Message)"
        Write-Warning "Install it yourself from https://ffmpeg.org/ instead, and make sure it is on PATH."
        return $null
    } finally {
        $ProgressPreference = $originalProgressPreference
    }

    try {
        New-Item -ItemType Directory -Force -Path $localRoot | Out-Null
        Expand-Archive -LiteralPath $zipPath -DestinationPath $localRoot -Force
    } catch {
        Write-Warning "Could not extract FFmpeg: $($_.Exception.Message)"
        return $null
    } finally {
        Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue
    }

    $extracted = Get-ChildItem -LiteralPath $localRoot -Filter "ffmpeg.exe" -Recurse -File `
        -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $extracted) {
        Write-Warning "FFmpeg was downloaded but ffmpeg.exe could not be found inside the archive."
        return $null
    }

    Write-Output "FFmpeg is ready."
    Write-Output ""
    return $extracted.FullName
}

$ffmpegExe = Resolve-FFmpegExecutable
if (-not $ffmpegExe) {
    exit 1
}

# Captured before the prompt below can overwrite $GameDirectory. True whenever "Convert Movies.bat"
# is what launched this (it always passes -Interactive, on both its double-click and drag-and-drop
# paths) or when no GameDirectory was given at all (running the script bare, with no arguments, is
# treated the same friendly way). A scripted call that passes -GameDirectory and no -Interactive
# gets no prompts at all, only ever the values it was actually given.
$interactive = $Interactive.IsPresent -or [string]::IsNullOrWhiteSpace($GameDirectory)

# No -GameDirectory given. Loops rather than failing on the first bad entry: a typo here is the
# single most likely mistake a non-technical user makes, and re-running the whole script over a
# fixable typo is a worse experience than just asking again.
if ($interactive) {
    Write-Output "This converts Star Wars Episode I: The Phantom Menace's movies (.bik) to .mp4,"
    Write-Output "so fmv_player.dll can play them at full quality instead of the original Bink video."
    Write-Output ""
    while ([string]::IsNullOrWhiteSpace($GameDirectory)) {
        $candidate = (Read-Host "Enter the full path to your game folder (the one WMAIN.EXE is in)").Trim().Trim('"')
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }
        if (-not (Test-Path -LiteralPath (Join-Path $candidate "WMAIN.EXE") -PathType Leaf)) {
            Write-Warning "WMAIN.EXE was not found in '$candidate' - check the path and try again."
            continue
        }
        $GameDirectory = $candidate
    }
    Write-Output ""
}

# Same idea as the GameDirectory prompt above, and gated the same way: only when running
# interactively, and only when the caller did not already ask for a specific size on the command
# line (PSBoundParameters, not the values themselves, since 1920/1080 are also this script's own
# defaults and indistinguishable from "the user typed -TargetWidth 1920" otherwise).
if ($interactive -and -not $PSBoundParameters.ContainsKey('TargetWidth') -and
    -not $PSBoundParameters.ContainsKey('TargetHeight')) {
    Write-Output "Choose the output video size:"
    Write-Output "  1) 1920x1080 (1080p) - recommended; this source has nowhere near this much"
    Write-Output "     real detail, so anything larger mainly just costs file size"
    Write-Output "  2) 2560x1440 (1440p)"
    Write-Output "  3) 3840x2160 (4K)"
    Write-Output "  4) Keep the original resolution (smallest possible files)"
    Write-Output "  5) Enter a custom size"
    $sizeChoice = (Read-Host "Enter 1-5 (press Enter for 1)").Trim()
    switch ($sizeChoice) {
        "2" { $TargetWidth = 2560; $TargetHeight = 1440 }
        "3" { $TargetWidth = 3840; $TargetHeight = 2160 }
        "4" { $TargetWidth = 0;    $TargetHeight = 0 }
        "5" {
            $parsedWidth = 0
            while ($parsedWidth -le 0) {
                $widthAnswer = (Read-Host "Target width in pixels").Trim()
                if (-not [int]::TryParse($widthAnswer, [ref]$parsedWidth) -or $parsedWidth -le 0) {
                    Write-Warning "Enter a whole number greater than 0."
                    $parsedWidth = 0
                }
            }
            $parsedHeight = 0
            while ($parsedHeight -le 0) {
                $heightAnswer = (Read-Host "Target height in pixels").Trim()
                if (-not [int]::TryParse($heightAnswer, [ref]$parsedHeight) -or $parsedHeight -le 0) {
                    Write-Warning "Enter a whole number greater than 0."
                    $parsedHeight = 0
                }
            }
            $TargetWidth = $parsedWidth
            $TargetHeight = $parsedHeight
        }
        default { $TargetWidth = 1920; $TargetHeight = 1080 }
    }
    Write-Output ""
}

# Only meaningful when a scale is actually happening at all - "keep the original resolution" above
# (or -TargetWidth/-TargetHeight 0 0 on the command line) means there is no target box for Stretch
# to fill one way or the other, so this would otherwise be a pointless question in that case.
if ($interactive -and -not $PSBoundParameters.ContainsKey('Stretch') -and
    $TargetWidth -gt 0 -and $TargetHeight -gt 0) {
    Write-Output "The source video is roughly 4:3, which will not exactly fill a widescreen frame:"
    Write-Output "  1) Keep the original shape (black bars top/bottom or left/right) - recommended,"
    Write-Output "     no distortion"
    Write-Output "  2) Stretch to fill the screen (no black bars, but the picture is stretched)"
    $stretchChoice = (Read-Host "Enter 1-2 (press Enter for 1)").Trim()
    $Stretch = $stretchChoice -eq "2"
    Write-Output ""
}

if (-not (Test-Path -LiteralPath $GameDirectory -PathType Container)) {
    Write-Error "Game directory does not exist: $GameDirectory"
    exit 1
}

$movieSource = Join-Path $GameDirectory "GAMEDATA\MOVIE"
if (-not (Test-Path -LiteralPath $movieSource -PathType Container)) {
    Write-Error "No GAMEDATA\MOVIE folder under '$GameDirectory' - is this the game's own directory (the one WMAIN.EXE is in)?"
    exit 1
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $GameDirectory "movies\_hd"
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$bikFiles = @(Get-ChildItem -LiteralPath $movieSource -Filter "*.BIK" -File)
if ($bikFiles.Count -eq 0) {
    Write-Warning "No .BIK files found in '$movieSource'."
    exit 0
}

# Building the -vf chain: only scale if BOTH dimensions were given, only force fps if TargetFps
# was given. An empty filter list is valid and means "copy the source's own video geometry as is".
$videoFilters = @()
if ($TargetWidth -gt 0 -and $TargetHeight -gt 0) {
    if ($Stretch) {
        $videoFilters += "scale=${TargetWidth}:${TargetHeight}:flags=lanczos"
    } else {
        # Scale to fit inside the target box, then pad the rest with black: preserves the source's
        # own aspect ratio instead of distorting it, the same "Fit" behaviour fmv_scaling.dll (this
        # project's earlier, since-removed design) offered as its default for the same reason.
        $videoFilters += "scale=${TargetWidth}:${TargetHeight}:flags=lanczos:force_original_aspect_ratio=decrease,pad=${TargetWidth}:${TargetHeight}:(ow-iw)/2:(oh-ih)/2"
    }
}
if ($TargetFps -gt 0) {
    $videoFilters += "fps=$TargetFps"
}

Write-Output "Converting $($bikFiles.Count) movie(s) from '$movieSource' to '$OutputDirectory'..."
if ($videoFilters.Count -gt 0) {
    Write-Output "  filters: $($videoFilters -join ',')"
} else {
    Write-Output "  filters: none (source resolution and frame rate kept as is)"
}

$converted = 0
$skipped = 0
$failed = 0

foreach ($bik in $bikFiles) {
    $baseName = $bik.BaseName.ToLowerInvariant()
    $outputPath = Join-Path $OutputDirectory "$baseName.mp4"

    if ((Test-Path -LiteralPath $outputPath) -and -not $Force) {
        Write-Output "  skip   $($bik.Name) -> $baseName.mp4 (already exists, use -Force to re-encode)"
        $skipped++
        continue
    }

    Write-Output "  encode $($bik.Name) -> $baseName.mp4"

    $ffmpegArgs = @("-y", "-i", $bik.FullName)
    if ($videoFilters.Count -gt 0) {
        $ffmpegArgs += @("-vf", ($videoFilters -join ","))
    }
    $ffmpegArgs += @(
        "-c:v", $VideoCodec,
        "-preset", "slow",
        "-crf", $Crf,
        "-pix_fmt", "yuv420p",
        "-profile:v", "high",
        "-fps_mode", "cfr",
        "-c:a", "aac",
        "-b:a", "192k",
        "-movflags", "+faststart",
        $outputPath
    )

    # No output redirection here on purpose: ffmpeg writes its routine progress to stderr, and
    # with $ErrorActionPreference = "Stop" (above), piping that through `2>&1 | Out-Null` makes
    # PowerShell treat ffmpeg's ordinary progress lines as a terminating error and abort the whole
    # script on the very first file. Letting it print straight to the console is not just cosmetic.
    & $ffmpegExe @ffmpegArgs

    if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $outputPath)) {
        $converted++
    } else {
        Write-Warning "  FAILED: $($bik.Name) (ffmpeg exit code $LASTEXITCODE)"
        if (Test-Path -LiteralPath $outputPath) {
            Remove-Item -LiteralPath $outputPath -Force -ErrorAction SilentlyContinue
        }
        $failed++
    }
}

Write-Output ""
Write-Output "Done: $converted converted, $skipped skipped, $failed failed."
if ($failed -gt 0) {
    exit 1
}
