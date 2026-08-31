<#
.SYNOPSIS
    Upscales this game's own menu artwork so the menus can fill a modern screen.

.DESCRIPTION
    A tool, not content: this project ships no game assets, converted or otherwise. It reads the
    archives in your own legally owned copy of the game and writes bigger copies of the pictures
    that are already there, into a folder of their own. Nothing in the game folder is modified,
    big.lab and LOCALIZE.LAB are opened read-only and never written.

    Lives in tools\ - every standalone script this project ships (not a mod DLL, not game content)
    lives there, alongside WMAIN.EXE in an installed copy.

    WHY THIS IS NEEDED AT ALL. The menus are laid out on a 640x480 canvas and the engine's blitter
    copies one source pixel to one destination pixel: there is no stretching blit anywhere in the
    game. enhanced_resolution.dll makes the canvas bigger so the layout fills the screen, but the
    pictures drawn on it are whatever size their bitmap is, so without bigger bitmaps three quarters
    of the canvas would have nothing painting it. The two halves are matched by the DLL reading the
    size of splash3.BMP out of this folder and scaling the layout to agree with it, so the layout
    and the artwork can never disagree about the number.

    NEAREST NEIGHBOUR, DELIBERATELY. After the engine converts a bitmap to 16 bit, a pixel that is
    exactly zero is a SKIP, i.e. transparent. A smoothing filter invents near-black where black was
    transparent, and new exact-zero pixels where there were none, so it would put a halo around
    every button and punch holes in dark artwork. Whole pixel replication cannot invent a colour
    that was not already in the source, so it is exact on that rule by construction.

.PARAMETER GameDirectory
    The folder WMAIN.EXE lives in. If omitted, the script asks for it - which is also what happens
    if you double-click "Convert Menu Art.bat" next to this script, or drag your game folder onto
    it. Required with -Quiet, which never asks for anything.

.PARAMETER OutputDirectory
    Where the upscaled pictures go. Defaults to <GameDirectory>\menu_hd, matching
    enhanced_resolution's own default MenuArtDirectory. If you change one, change the other.

.PARAMETER Width
.PARAMETER Height
    The screen size to convert for. Both default to 0, meaning "ask", and in -Quiet mode meaning
    "this machine's primary display". Give them together or not at all.

.PARAMETER Fit
    Uniform keeps the artwork's 4:3 shape, so nothing is distorted and a wider screen gets black
    down the sides. Stretch fills the screen edge to edge, which suits the chrome and the panels
    and stretches the photographs. Defaults to Stretch, because the menus are mostly chrome and a
    menu that fills the screen is the point of the exercise.

.PARAMETER Interactive
    Always passed by "Convert Menu Art.bat". Prompts for anything not already given on the command
    line. Implied whenever -GameDirectory is omitted.

.PARAMETER Quiet
    Non-interactive, for an installer or another program driving this. Never prompts, prints no
    banner, and writes the same machine-readable lines convert_movies.ps1 does, so one parser
    serves both: TOTAL n first, then OK name WxH bytes or SKIP name per picture, then
    DONE written=n skipped=n. Requires -GameDirectory, and cannot be combined with -Interactive.

.PARAMETER Remove
    Deletes the files this tool wrote, using the manifest it leaves behind, and then the folder if
    it is empty. Nothing else is touched, so a file you put in that folder yourself survives.
#>
[CmdletBinding()]
param(
    [string] $GameDirectory,
    [string] $OutputDirectory,
    [int]    $Width  = 0,
    [int]    $Height = 0,
    [ValidateSet('Uniform', 'Stretch')]
    [string] $Fit = 'Stretch',
    [switch] $Interactive,
    [switch] $Quiet,
    [switch] $Remove
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

# The authored menu canvas. Every rectangle in the game's menus is in these units.
$CanvasWidth  = 640
$CanvasHeight = 480

# The run length encoder writes a literal control word as (run -band 0xfff) while advancing the
# output by the whole run, so a canvas wider than 4095 pixels corrupts the stream. 4095/640.
$MaxRatio = 4095.0 / $CanvasWidth

# The type tags whose members are the pictures the menus draw. Stored byte-reversed in the archive.
$PictureTags = @('MENU', 'BMPS')

$ManifestName = 'openphantom_menu_art.txt'

if ($Quiet -and $Interactive) {
    throw '-Quiet and -Interactive ask for opposite things; pass one or neither.'
}
if ($Quiet -and -not $GameDirectory) {
    throw '-Quiet requires -GameDirectory, because it must never stop and wait for somebody.'
}
if (-not $GameDirectory) { $Interactive = $true }

function Write-Line {
    param([string] $Text)
    if (-not $Quiet) { Write-Host $Text }
}

# --------------------------------------------------------------------------------------------
# The LAB archive: a 16 byte header, then one 16 byte record per member, then the name blob, then
# the member data uncompressed. Type tags are stored byte reversed, so 'MENU' is written 'UNEM'.
# --------------------------------------------------------------------------------------------
function Read-LabDirectory {
    param([string] $Path)

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $reader = New-Object System.IO.BinaryReader($stream)
        $magic  = [System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(4))
        if ($magic -ne 'LABN') { throw "$Path is not a LABN archive" }
        $null      = $reader.ReadUInt32()            # version
        $count     = $reader.ReadUInt32()
        $nameBytes = $reader.ReadUInt32()

        $records = New-Object 'System.Collections.Generic.List[object]'
        for ($i = 0; $i -lt $count; $i++) {
            $records.Add([pscustomobject]@{
                NameOffset = $reader.ReadUInt32()
                DataOffset = $reader.ReadUInt32()
                Size       = $reader.ReadUInt32()
                Tag        = [System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(4))
            })
        }
        $names = $reader.ReadBytes([int]$nameBytes)

        $out = New-Object 'System.Collections.Generic.List[object]'
        foreach ($record in $records) {
            # Byte reversed in the archive, so 'MENU' is stored 'UNEM'.
            $tag = -join ($record.Tag[3], $record.Tag[2], $record.Tag[1], $record.Tag[0])
            if ($PictureTags -notcontains $tag) { continue }

            $end = [int]$record.NameOffset
            while ($end -lt $names.Length -and $names[$end] -ne 0) { $end++ }
            $name = [System.Text.Encoding]::ASCII.GetString(
                        $names, [int]$record.NameOffset, $end - [int]$record.NameOffset)
            if (-not $name) { continue }

            $out.Add([pscustomobject]@{
                Name       = $name
                DataOffset = [long]$record.DataOffset
                Size       = [int]$record.Size
            })
        }
        return $out
    } finally {
        $stream.Dispose()
    }
}

# --------------------------------------------------------------------------------------------
# One member, upscaled. GDI+ rather than an external encoder: it is on every Windows machine, so
# this tool needs no download and no cached binary the way the movie converter does.
# --------------------------------------------------------------------------------------------
function Convert-Picture {
    param(
        [byte[]] $Data,
        [double] $RatioX,
        [double] $RatioY,
        [string] $Destination
    )

    $inputStream = New-Object System.IO.MemoryStream(,$Data)
    try {
        $source = [System.Drawing.Image]::FromStream($inputStream)
    } catch {
        return $null                              # not a picture this can read; caller counts it
    }

    try {
        $width  = [int][Math]::Round($source.Width  * $RatioX)
        $height = [int][Math]::Round($source.Height * $RatioY)
        if ($width -lt 1)  { $width = 1 }
        if ($height -lt 1) { $height = 1 }

        # 24 bit, which is what every one of these already is and what the engine's loader expects.
        $target = New-Object System.Drawing.Bitmap($width, $height,
                        [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($target)
            try {
                # NearestNeighbor for the transparency rule in the header. Half pixel offset so the
                # replicated block is centred on the source pixel rather than shifted up and left.
                $graphics.InterpolationMode  = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
                $graphics.PixelOffsetMode    = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
                $graphics.SmoothingMode      = [System.Drawing.Drawing2D.SmoothingMode]::None
                $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighSpeed
                $graphics.DrawImage($source, 0, 0, $width, $height)
            } finally {
                $graphics.Dispose()
            }
            $target.Save($Destination, [System.Drawing.Imaging.ImageFormat]::Bmp)

            # GDI+ writes biSizeImage as 0, which is legal for an uncompressed bitmap but leaves a
            # loader that sizes its buffer from that field with nothing to go on, and it stamps its
            # own 96 DPI over the source's. Both are put back so this file is byte for byte what
            # convert_menu.py writes from the same input: two implementations, one output, and no
            # question about which one a bug came from.
            $stride = (($width * 3) + 3) -band -4
            $handle = [System.IO.File]::Open($Destination, 'Open', 'ReadWrite')
            try {
                $null = $handle.Seek(34, 'Begin')
                $handle.Write([BitConverter]::GetBytes([uint32]($stride * $height)), 0, 4)
                $handle.Write($Data, 38, 8)          # the source's own X and Y pixels-per-metre
            } finally {
                $handle.Dispose()
            }
            return [pscustomobject]@{
                Width  = $width
                Height = $height
                Bytes  = (Get-Item $Destination).Length
            }
        } finally {
            $target.Dispose()
        }
    } finally {
        $source.Dispose()
        $inputStream.Dispose()
    }
}

# --------------------------------------------------------------------------------------------
function Get-PrimaryScreenSize {
    try {
        Add-Type -AssemblyName System.Windows.Forms -ErrorAction Stop
        $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
        if ($bounds.Width -gt 0 -and $bounds.Height -gt 0) {
            return @($bounds.Width, $bounds.Height)
        }
    } catch { }
    return @(1920, 1080)
}

function Read-GameDirectory {
    while ($true) {
        Write-Host ''
        $answer = Read-Host 'Where is the game installed? (the folder WMAIN.EXE is in)'
        $answer = $answer.Trim().Trim('"')
        if (-not $answer) { continue }
        if (Test-Path (Join-Path $answer 'WMAIN.EXE')) { return $answer }
        Write-Host "  There is no WMAIN.EXE in that folder. Try again, or close this window."
    }
}

function Read-TargetSize {
    $screen = Get-PrimaryScreenSize
    Write-Host ''
    Write-Host "What screen size should the menus be made for?"
    Write-Host "  Just press Enter for this machine's screen, $($screen[0])x$($screen[1])."
    Write-Host "  Or type a size like 2560x1440."
    while ($true) {
        $answer = (Read-Host 'Size').Trim()
        if (-not $answer) { return $screen }
        if ($answer -match '^\s*(\d{3,5})\s*[xX*, ]\s*(\d{3,5})\s*$') {
            return @([int]$Matches[1], [int]$Matches[2])
        }
        Write-Host '  Type it as WIDTHxHEIGHT, for example 3840x2160.'
    }
}

function Read-Fit {
    Write-Host ''
    Write-Host 'How should the 4:3 menu art fill a widescreen display?'
    Write-Host '  1. Stretch it edge to edge          (recommended, and the default)'
    Write-Host '  2. Keep its shape, black down the sides'
    while ($true) {
        $answer = (Read-Host 'Choice [1]').Trim()
        if (-not $answer -or $answer -eq '1') { return 'Stretch' }
        if ($answer -eq '2') { return 'Uniform' }
        Write-Host '  Type 1 or 2.'
    }
}

# --------------------------------------------------------------------------------------------
# Removal, from the manifest, so a file somebody put in that folder themselves is left alone.
# --------------------------------------------------------------------------------------------
function Remove-ConvertedArt {
    param([string] $Folder)

    $manifest = Join-Path $Folder $ManifestName
    if (-not (Test-Path $manifest)) {
        Write-Line "Nothing to remove: no $ManifestName in $Folder"
        return
    }
    $removed = 0
    foreach ($line in Get-Content $manifest) {
        $name = $line.Trim()
        if (-not $name -or $name.StartsWith('#')) { continue }
        $path = Join-Path $Folder $name
        if (Test-Path $path) { Remove-Item -LiteralPath $path -Force; $removed++ }
    }
    Remove-Item -LiteralPath $manifest -Force
    if (-not (Get-ChildItem -LiteralPath $Folder -Force)) {
        Remove-Item -LiteralPath $Folder -Force
        Write-Line "Removed $removed files and the folder $Folder"
    } else {
        Write-Line "Removed $removed files from $Folder; it was left in place because it is not empty"
    }
}

# --------------------------------------------------------------------------------------------
Add-Type -AssemblyName System.Drawing

if ($Interactive -and -not $Quiet) {
    Write-Host ''
    Write-Host 'OpenPhantom menu artwork converter'
    Write-Host '----------------------------------'
    Write-Host 'Makes bigger copies of the menu pictures already in your own game, so the menus can'
    Write-Host 'fill your screen. Your game files are read only and never changed.'
}

if (-not $GameDirectory) { $GameDirectory = Read-GameDirectory }
$GameDirectory = (Resolve-Path -LiteralPath $GameDirectory).Path
if (-not (Test-Path (Join-Path $GameDirectory 'WMAIN.EXE'))) {
    throw "There is no WMAIN.EXE in $GameDirectory, so that is not the game folder."
}
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $GameDirectory 'menu_hd' }

if ($Remove) {
    Remove-ConvertedArt -Folder $OutputDirectory
    return
}

if ($Width -le 0 -or $Height -le 0) {
    if ($Interactive) {
        $size = Read-TargetSize
    } else {
        $size = Get-PrimaryScreenSize
    }
    $Width  = $size[0]
    $Height = $size[1]
}
if ($Interactive -and -not $PSBoundParameters.ContainsKey('Fit')) { $Fit = Read-Fit }

if ($Width -lt $CanvasWidth -or $Height -lt $CanvasHeight) {
    throw "$($Width)x$($Height) is smaller than the menus' own $($CanvasWidth)x$($CanvasHeight); there is nothing to upscale to."
}

if ($Fit -eq 'Uniform') {
    $ratio  = [Math]::Min($Width / [double]$CanvasWidth, $Height / [double]$CanvasHeight)
    $ratioX = $ratio
    $ratioY = $ratio
} else {
    $ratioX = $Width  / [double]$CanvasWidth
    $ratioY = $Height / [double]$CanvasHeight
}
if ($ratioX -gt $MaxRatio) {
    Write-Line ("  A canvas wider than 4095 pixels breaks the engine's own run length encoder, so " +
                "the width is capped at {0:N3} times." -f $MaxRatio)
    $ratioX = $MaxRatio
}
if ($ratioY -gt $MaxRatio) { $ratioY = $MaxRatio }

$canvasW = [int][Math]::Round($CanvasWidth  * $ratioX)
$canvasH = [int][Math]::Round($CanvasHeight * $ratioY)

Write-Line ''
Write-Line ("Converting for {0}x{1} ({2}): {3:N3} times across, {4:N3} down, canvas {5}x{6}" -f `
            $Width, $Height, $Fit.ToLower(), $ratioX, $ratioY, $canvasW, $canvasH)
Write-Line "  reading  $GameDirectory"
Write-Line "  writing  $OutputDirectory"

if (-not (Test-Path $OutputDirectory)) {
    $null = New-Item -ItemType Directory -Path $OutputDirectory -Force
}

# The archives are read in full before anything is converted, so the count can be announced
# first. A caller driving this needs the total to size a progress bar, and it cannot get one
# afterwards because by then the bar is over.
$work = New-Object 'System.Collections.Generic.List[object]'
foreach ($archive in @('big.lab', 'LOCALIZE.LAB')) {
    $path = Join-Path $GameDirectory $archive
    if (-not (Test-Path $path)) {
        Write-Line "  $archive not present, skipped"
        continue
    }
    $members = Read-LabDirectory -Path $path
    Write-Line "  $archive - $($members.Count) pictures"
    foreach ($member in $members) {
        $work.Add([pscustomobject]@{ Archive = $path; Member = $member })
    }
}
if ($Quiet) { Write-Output ("TOTAL {0}" -f $work.Count) }

$written = 0
$skipped = 0
$total   = [long]0
$names   = New-Object 'System.Collections.Generic.List[string]'
$streams = @{}

try {
    foreach ($item in $work) {
        if (-not $streams.ContainsKey($item.Archive)) {
            $streams[$item.Archive] = [System.IO.File]::OpenRead($item.Archive)
        }
        $stream = $streams[$item.Archive]
        $member = $item.Member

        $stream.Position = $member.DataOffset
        $data = New-Object byte[] $member.Size
        $read = $stream.Read($data, 0, $member.Size)
        $result = $null
        if ($read -eq $member.Size) {
            $result = Convert-Picture -Data $data -RatioX $ratioX -RatioY $ratioY `
                                      -Destination (Join-Path $OutputDirectory $member.Name)
        }

        if ($null -eq $result) {
            $skipped++
            if ($Quiet) { Write-Output ("SKIP {0}" -f $member.Name) }
            continue
        }

        $written++
        $total += $result.Bytes
        $names.Add($member.Name)
        if ($Quiet) {
            Write-Output ("OK {0} {1}x{2} {3}" -f $member.Name, $result.Width, $result.Height,
                                                  $result.Bytes)
        }
    }
} finally {
    foreach ($stream in $streams.Values) { $stream.Dispose() }
}

# The manifest is what -Remove reads, so a file somebody else put in this folder is never deleted.
$manifest = New-Object 'System.Collections.Generic.List[string]'
$manifest.Add('# OpenPhantom menu artwork. Written by tools\convert_menu.ps1 from your own game.')
$manifest.Add(("# {0}x{1} {2}, canvas {3}x{4}, {5}" -f $Width, $Height, $Fit.ToLower(),
                                                       $canvasW, $canvasH,
                                                       (Get-Date -Format 'yyyy-MM-dd')))
$manifest.Add('# Delete this folder, or run: Convert Menu Art.bat -Remove')
foreach ($name in $names) { $manifest.Add($name) }
Set-Content -Path (Join-Path $OutputDirectory $ManifestName) -Value $manifest -Encoding ASCII

if ($Quiet) { Write-Output ("DONE written={0} skipped={1}" -f $written, $skipped) }

Write-Line ''
Write-Line ("{0} pictures written, {1} skipped, {2:N1} MB on disk" -f `
            $written, $skipped, ($total / 1MB))
if ($written -gt 0) {
    Write-Line ''
    Write-Line 'Done. Start the game and the menus will fill the screen.'
    Write-Line "To undo it, delete $OutputDirectory."
}
