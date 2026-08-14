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
#define XidiUrl          "https://github.com/samuelgr/Xidi/releases/download/" + XidiVersion + "/Xidi-" + XidiVersion + ".zip"
#define XidiSha256       "41b6d23692d7e8043deef032ae5e619ee96aaef1586f58e4a541d4c15429b8cd"
#define XidiUnpackedSize 1779900
#define XidiTmp          "{tmp}\xidi"
#define XidiRoot         XidiTmp + "\Xidi-" + XidiVersion

; The Microsoft Visual C++ 2015-2022 runtime for 32-bit programs. Both Xidi files import MSVCP140
; and VCRUNTIME140, and the failure without them is the worst kind this installer can produce: the
; wrapper is loaded through a static import, so the game does not start at all and Windows names a
; missing DLL rather than anything to do with controllers.
;
; THIS IS THE ONE DOWNLOAD HERE WITHOUT A HASH, and it is deliberate. Microsoft serves the current
; build at this address and replaces it, so a pinned hash would fail on the day they do, which for
; the player is the same outcome as no check at all. What stands in its place: it is only fetched
; when the runtime is genuinely absent, it comes from the vendor's own host over TLS, and it is run
; as an ordinary installer whose signature Windows checks before it elevates.
#define VcRedistUrl "https://aka.ms/vs/17/release/vc_redist.x86.exe"

[Components]
; Child of the patch because Xidi.ini comes out of the patch archive, and Inno cannot tick a child
; without its parent, so the row that downloads that archive cannot be skipped.
Name: "patch\xidi"; Description: "{cm:CompXidi}"; Types: custom

[Files]
Source: "{#XidiUrl}"; DestDir: "{#XidiTmp}"; DestName: "xidi.zip"; \
    Hash: "{#XidiSha256}"; ExternalSize: {#XidiUnpackedSize}; \
    Components: patch\xidi; \
    Flags: external download extractarchive recursesubdirs ignoreversion

; winmm.dll is only the entry point, 78 KB of it. It loads Xidi.32.dll from the same folder under a
; name it builds at run time, so that dependency is in no import table and installing the entry
; point on its own gives a wrapper that finds nothing and says nothing.
;
; Renamed on the way in. The name is what Windows redirects, and the bridge below loads this file by
; full path, so nothing here answers to a system name and no other program's winmm.dll is displaced.
Source: "{#XidiRoot}\Win32\winmm.dll"; DestDir: "{app}"; DestName: "xidi_winmm.dll"; \
    Components: patch\xidi; Flags: external ignoreversion
Source: "{#XidiRoot}\Win32\Xidi.32.dll"; DestDir: "{app}"; \
    Components: patch\xidi; Flags: external ignoreversion

; The bridge, out of the patch archive rather than Xidi's. It is not a separate choice: without it
; the wrapper above is never reached, and without the wrapper it installs, says so and does nothing.
Source: "{#PatchTmp}\mods\xidi_bridge.dll"; DestDir: "{app}\mods"; \
    Components: patch\xidi; Flags: external ignoreversion

; BSD 3-Clause, so the notice travels with the binary. The three notices under ThirdParty\ do not:
; two are the Boost licence, which exempts copies in machine-executable form, and Hookshot covers the
; hook module form of Xidi, which is not installed here.
Source: "{#XidiRoot}\LICENSE"; DestDir: "{app}"; DestName: "Xidi-License.txt"; \
    Components: patch\xidi; Flags: external ignoreversion

; Our mapping for this game: the camera on the right stick, the menu keys on Back and Start. Xidi
; runs without it on its own defaults, which know nothing about this game.
;
; The one file this installer carries besides the extractor, and the only local source here, so Inno
; checks it when the script is compiled. Replaced on every installation with the previous one kept
; beside it, like every other configuration.
Source: "dist\Xidi.ini"; DestDir: "{app}"; \
    Components: patch\xidi; Flags: ignoreversion

; Fetched only when the runtime is missing, which VcRuntimeMissing in the main script decides.
;
; ExternalSize is required with the download flag, and this is the only figure in this tree that is
; expected to go stale: Microsoft replaces the file at that address, so it is the size of whatever
; build was current when this line was written, measured with a HEAD request. Being wrong costs an
; inaccurate progress bar for one download and nothing else, which is why the row is here rather
; than pinned to a version that would eventually 404.
Source: "{#VcRedistUrl}"; DestDir: "{tmp}"; DestName: "vc_redist.x86.exe"; \
    ExternalSize: 13953392; \
    Components: patch\xidi; Check: VcRuntimeMissing; \
    Flags: external download ignoreversion deleteafterinstall
