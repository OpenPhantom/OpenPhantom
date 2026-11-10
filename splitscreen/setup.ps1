# Prepares two side by side installations for one-machine split screen: the existing one becomes
# player 1 (host), and a second folder of hard links becomes player 2 (client). Files a player
# writes or configures are real copies, so the two never share an edit.
param(
    [Parameter(Mandatory)] [string] $Player1,
    [Parameter(Mandatory)] [string] $Player2
)
$ErrorActionPreference = 'Stop'

# Written by the game or the patch, or configured per player; everything else is a hard link.
$perPlayer = @('engine_fixes.ini', 'obi.ini', 'dxwrapper.ini', 'characters.ini')
$perPlayerDirs = @('save')

# Latin-1 round trips every byte, so comments and line endings come back exactly as they were.
$latin1 = [Text.Encoding]::GetEncoding(28591)

function Set-IniKey([string] $Path, [string] $Section, [string] $Key, [string] $Value) {
    $text = [IO.File]::ReadAllText($Path, $latin1)
    # The header without its line ending, so a missing key is inserted after the CRLF, not inside it.
    $header = [regex]::Match($text, "(?m)^\[$([regex]::Escape($Section))\](?=\r?$)")
    if (-not $header.Success) {
        $separator = if ($text.EndsWith("`r`n")) { '' } else { "`r`n" }
        [IO.File]::WriteAllText($Path, "$text$separator`r`n[$Section]`r`n$Key=$Value`r`n", $latin1)
        return
    }
    $next = [regex]::Match($text.Substring($header.Index + $header.Length), '(?m)^\[')
    $end = if ($next.Success) { $header.Index + $header.Length + $next.Index } else { $text.Length }
    $body = $text.Substring($header.Index, $end - $header.Index)
    $line = [regex]::Match($body, "(?m)^$([regex]::Escape($Key))=[^\r\n]*")
    if ($line.Success) {
        $body = $body.Remove($line.Index, $line.Length).Insert($line.Index, "$Key=$Value")
    } else {
        $body = $body.Insert($header.Length, "`r`n$Key=$Value")
    }
    [IO.File]::WriteAllText($Path, $text.Substring(0, $header.Index) + $body + $text.Substring($end), $latin1)
}

$Player1 = (Resolve-Path $Player1).Path
if (-not (Test-Path "$Player1\WMAIN.EXE")) { throw "no WMAIN.EXE in $Player1" }
if ((Split-Path -Qualifier $Player1) -ne (Split-Path -Qualifier $Player2)) { throw 'hard links need both folders on one drive' }

foreach ($file in Get-ChildItem $Player1 -Recurse -File) {
    $relative = $file.FullName.Substring($Player1.Length + 1)
    $target = Join-Path $Player2 $relative
    if (Test-Path $target) { continue }
    New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    $copy = ($perPlayer -contains $relative) -or ($perPlayerDirs -contains ($relative -split '\\')[0]) -or $file.Extension -eq '.log'
    if ($copy) { Copy-Item $file.FullName $target }
    else { New-Item -ItemType HardLink -Path $target -Target $file.FullName | Out-Null }
}

Add-Type -AssemblyName System.Windows.Forms
# Full height; launch.ps1 marks both windows full screen so the taskbar drops behind them.
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$width = [int]($screen.Width / 2)
$height = $screen.Height

foreach ($player in @(@{ Dir = $Player1; Pad = 0; Left = 0 }, @{ Dir = $Player2; Pad = 1; Left = $width })) {
    $ini = "$($player.Dir)\engine_fixes.ini"
    Set-IniKey $ini 'enhanced_resolution' 'WindowMode' '4'
    Set-IniKey $ini 'enhanced_resolution' 'WindowedPresent' '1'
    Set-IniKey $ini 'enhanced_resolution' 'WindowedWidth' "$width"
    Set-IniKey $ini 'enhanced_resolution' 'WindowedHeight' "$height"
    Set-IniKey $ini 'enhanced_resolution' 'WindowedLeft' "$($player.Left)"
    Set-IniKey $ini 'enhanced_resolution' 'WindowedTop' "$($screen.Top)"
    Set-IniKey $ini 'controller_input' 'ControllerIndex' "$($player.Pad)"
    # multi_instance.dll is outside the release, and a host refuses a joiner that loads one unless
    # it is named here; a machine that loads one cannot host either.
    Set-IniKey $ini 'multiplayer' 'AllowMods' 'multi_instance'
    # The pad the game's own WinMM joystick code reads; the log lists which ids answer.
    Set-IniKey $ini 'multi_instance' 'Joystick' "$($player.Pad)"
    # Right stick, Start and triggers for whichever window is not in front.
    Set-IniKey $ini 'controller_input' 'BackgroundInput' '1'
    # Windows gives an Xbox pad's WinMM input to the window in front only; XInput has no such gate.
    Set-IniKey $ini 'multi_instance' 'XInputSlot' "$($player.Pad)"
    $obi = "$($player.Dir)\obi.ini"
    Set-IniKey $obi 'options' 'screen_width' "$width"
    Set-IniKey $obi 'options' 'screen_height' "$height"
}
# One soundtrack, not two playing a few milliseconds apart. Effects stay on: player 1's copy places
# player 2's sounds by distance from player 1, so a muted player 2 loses his own fights when apart.
Set-IniKey "$Player2\obi.ini" 'options' 'MVOL' '0'

"player 1: $Player1`nplayer 2: $Player2`neach window ${width}x${height}"
