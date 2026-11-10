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

# Half-screen windows never count as full screen, so the taskbar stays on top unless told otherwise.
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Taskbar {
    [ComImport, Guid("602D4995-B13A-429b-A66E-1935E44F4317"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface ITaskbarList2 {
        void HrInit(); void AddTab(IntPtr h); void DeleteTab(IntPtr h); void ActivateTab(IntPtr h); void SetActiveAlt(IntPtr h);
        void MarkFullscreenWindow(IntPtr h, [MarshalAs(UnmanagedType.Bool)] bool full);
    }
    [ComImport, Guid("56FDF344-FD6D-11d0-958A-006097C9A090")] class TaskbarList {}
    static readonly ITaskbarList2 list = (ITaskbarList2)new TaskbarList();
    static Taskbar() { list.HrInit(); }
    public static void MarkFullscreen(IntPtr window) { list.MarkFullscreenWindow(window, true); }
}
'@
foreach ($player in $first, $second) { [Taskbar]::MarkFullscreen($player.MainWindowHandle) }

# Player 2 starts last and takes the focus; the keyboard belongs to player 1's window.
[void](New-Object -ComObject WScript.Shell).AppActivate($first.Id)
"player 1 PID $($first.Id), player 2 PID $($second.Id)"
