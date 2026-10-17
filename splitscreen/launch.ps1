# Starts player 1 and then player 2 on this machine; each window places itself from its own
# engine_fixes.ini. Run setup.ps1 once first. Host and join from the in-game menu (M on the title
# screen): player 1 hosts, player 2 joins 127.0.0.1:27960.
param(
    [Parameter(Mandatory)] [string] $Player1,
    [Parameter(Mandatory)] [string] $Player2,
    [int] $TimeoutSeconds = 30
)
$ErrorActionPreference = 'Stop'

# multi_instance.dll renames the second copy's window class only if the first copy's window
# already stands, so each copy is waited for before the next one starts.
function Start-Player([string] $Dir) {
    $process = Start-Process "$Dir\WMAIN.EXE" -WorkingDirectory $Dir -PassThru
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ($process.MainWindowHandle -eq [IntPtr]::Zero) {
        if ($process.HasExited) { throw "$Dir exited with code $($process.ExitCode)" }
        if ((Get-Date) -gt $deadline) { throw "$Dir opened no window in $TimeoutSeconds s" }
        Start-Sleep -Milliseconds 250
        $process.Refresh()
    }
    $process
}

$first = Start-Player $Player1
$second = Start-Player $Player2
"player 1 PID $($first.Id), player 2 PID $($second.Id)"
