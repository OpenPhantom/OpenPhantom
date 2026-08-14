; Xidi, so an XInput controller works with the game.

; The game reads controllers through WinMM: joyGetPosEx, joyGetNumDevs and joyGetDevCapsA are in its
; import table, while DINPUT.dll gives it one function only, the interface creation for keyboard and
; mouse. So the WinMM form is the one to install and the DirectInput forms would sit in front of the
; wrong device.
;
; winmm.dll is not on the KnownDLLs list, so a static import resolves out of the executable's own
; directory before System32 and nothing about WMAIN.EXE has to change. Patching the import string to
; WINM2.dll and shipping the result would work too, and is what some community builds do, but it
; leaves a game executable that no longer matches the disc.

#define XidiVersion      "v5.0.0"
#define XidiUrl          "https://github.com/samuelgr/Xidi/releases/download/" + XidiVersion + "/Xidi-" + XidiVersion + ".zip"
#define XidiSha256       "41b6d23692d7e8043deef032ae5e619ee96aaef1586f58e4a541d4c15429b8cd"
#define XidiUnpackedSize 1779900
#define XidiTmp          "{tmp}\xidi"
#define XidiRoot         XidiTmp + "\Xidi-" + XidiVersion

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
; name it builds at run time, so that dependency is in no import table and installing winmm.dll on
; its own gives a wrapper that finds nothing and says nothing.
;
; A stranger holding the winmm.dll name is moved aside in [Code] before these rows run.
Source: "{#XidiRoot}\Win32\winmm.dll"; DestDir: "{app}"; \
    Components: patch\xidi; Flags: external ignoreversion
Source: "{#XidiRoot}\Win32\Xidi.32.dll"; DestDir: "{app}"; \
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
