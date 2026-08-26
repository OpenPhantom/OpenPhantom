; Xidi, so an XInput controller works with the game.

; The game reads controllers through WinMM: joyGetPosEx, joyGetNumDevs and joyGetDevCapsA are in its
; import table, while DINPUT.dll gives it one function only, the interface creation for keyboard and
; mouse. So the WinMM form is the one to install and the DirectInput forms would sit in front of the
; wrong device.
;
; It is NOT installed as winmm.dll, and that is the whole reason there is a patch DLL beside it.
; Placing a stand-in beside the executable is the ordinary way to wrap a system library, and for
; every other contested name in the folder it works: ddraw, dinput and dsound all come from the game
; folder. For this executable Windows hands out the system winmm.dll anyway, so the wrapper was
; never loaded and the game reported that no controller was connected. That was measured in the
; running game, and the compatibility engine is in the process while it happens.
;
; So the wrapper is installed under a name nothing redirects, and xidi_bridge.dll points the game's
; three joystick imports at it. The other six WinMM names the game imports, and the 42 the audio
; engine imports, keep reaching the system library directly, which is the better arrangement anyway:
; a controller wrapper has no business in front of Miles.

#define XidiVersion      "v5.0.0"
#define XidiSrc          "dist\xidi"

; The Microsoft Visual C++ 2015-2022 runtime for 32-bit programs. Both Xidi files import MSVCP140
; and VCRUNTIME140, and the failure without them is the worst kind this installer can produce: the
; wrapper is loaded through a static import, so the game does not start at all and Windows names a
; missing DLL rather than anything to do with controllers.
;
; Carried here rather than fetched, like everything else. This is Microsoft's redistributable
; package unmodified, which their redistribution terms allow, and it is run only when the runtime is
; genuinely absent. Windows checks its signature before it elevates.
#define VcRedistSrc "dist\vc_redist.x86.exe"

[Components]
; Child of the patch because Xidi.ini comes out of the patch archive, and Inno cannot tick a child
; without its parent, so the row that installs the patch cannot be skipped.
Name: "patch\xidi"; Description: "{cm:CompXidi}"; Types: everything custom

[Files]
; winmm.dll is only the entry point, 78 KB of it. It loads Xidi.32.dll from the same folder under a
; name it builds at run time, so that dependency is in no import table and installing the entry
; point on its own gives a wrapper that finds nothing and says nothing.
;
; Renamed on the way in. The name is what Windows redirects, and the bridge below loads this file by
; full path, so nothing here answers to a system name and no other program's winmm.dll is displaced.
Source: "{#XidiSrc}\Win32\winmm.dll"; DestDir: "{app}"; DestName: "xidi_winmm.dll"; \
    Components: patch\xidi; Flags: ignoreversion
Source: "{#XidiSrc}\Win32\Xidi.32.dll"; DestDir: "{app}"; \
    Components: patch\xidi; Flags: ignoreversion

; The bridge, out of the patch archive rather than Xidi's. It is not a separate choice: without it
; the wrapper above is never reached, and without the wrapper it installs, says so and does nothing.
Source: "{#PatchSrc}\mods\xidi_bridge.dll"; DestDir: "{app}\mods"; \
    Components: patch\xidi; Flags: ignoreversion

; BSD 3-Clause, so the notice travels with the binary. The three notices under ThirdParty\ do not:
; two are the Boost licence, which exempts copies in machine-executable form, and Hookshot covers the
; hook module form of Xidi, which is not installed here.
Source: "{#XidiSrc}\LICENSE"; DestDir: "{app}"; DestName: "Xidi-License.txt"; \
    Components: patch\xidi; Flags: ignoreversion

; Our mapping for this game: the camera on the right stick, the menu keys on Back and Start. Xidi
; runs without it on its own defaults, which know nothing about this game.
;
; The one file this installer carries besides the extractor, and the only local source here, so Inno
; checks it when the script is compiled. Replaced on every installation with the previous one kept
; beside it, like every other configuration.
Source: "dist\Xidi.ini"; DestDir: "{app}"; \
    Components: patch\xidi; Flags: ignoreversion

; Unpacked into the temporary folder and run only when the runtime is missing, which
; VcRuntimeMissing in the main script decides. Removed when Setup finishes.
Source: "{#VcRedistSrc}"; DestDir: "{tmp}"; DestName: "vc_redist.x86.exe"; \
    Components: patch\xidi; Check: VcRuntimeMissing; \
    Flags: ignoreversion deleteafterinstall
