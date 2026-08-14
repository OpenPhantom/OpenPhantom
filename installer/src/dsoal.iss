; DSOAL, so the game's 3D sound and its reverb work again.

; The game asks for EAX: "EAX environment selection", "EAX reverb" and "val<EAX_ENVIRONMENT_COUNT"
; are strings in WMAIN.EXE. It gets there through a Miles provider plugin, and the plugins ship
; beside the executable, each naming itself in plain text inside the file:
;
;     MSSFAST.M3D    Miles Fast 2D Positional Audio                       no 3D, no EAX
;     MSSDS3DS.M3D   Microsoft DirectSound3D software emulation
;     MSSDS3DH.M3D   Microsoft DirectSound3D hardware support
;     MSSEAX.M3D     Microsoft DirectSound3D with Creative Labs EAX(TM)
;     MSSA3D.M3D     Aureal A3D Interactive(TM)
;
; obi.ini picks one under [options] Sound 3D Driver, by name, word for word, and a stock installation
; names the 2D one. So two things are wrong and fixing either alone changes nothing: nothing asks for
; EAX, and asking would fail anyway because Windows dropped hardware DirectSound3D in Vista.
;
; The download fixes the second. ApplySoundProvider in the main script writes the provider name for
; the first, one key in place, leaving the rest of the player's settings alone.

; DSOAL has no versioned releases. The "latest-master" tag is replaced with every new build, while
; "archive" keeps each build at an address of its own. Pinning latest-master would break the hash for
; everyone the next time the author pushes, so an archived revision is what is pinned.
;
; To move to a newer build take its revision out of the archive tag, then measure the hash and both
; sizes again.
#define DsoalRevision       "r694"
#define DsoalUrl            "https://github.com/kcat/dsoal/releases/download/archive/DSOAL_" + DsoalRevision + ".zip"
#define DsoalSha256         "5f355e59d4be049b968c79f47484677438846da25d4ed0b98092234a283f3f1f"

; The download is a zip holding one file, which is another zip. Each extracting row has to declare
; what it expands to, so both numbers are needed.
#define DsoalOuterSize      9981105
#define DsoalInnerSize      9972695

#define DsoalTmp            "{tmp}\dsoal"
#define DsoalFiles          DsoalTmp + "\files"

; The archive carries the same build twice, plain and with HRTF forced on. HRTF is a headphone
; technique and applies to everything once it is on, so the plain build is taken. The alsoft.ini that
; comes with it documents the switch for anyone who wants it.
#define DsoalWin32          DsoalFiles + "\DSOAL\Win32"
#define DsoalDocs           DsoalFiles + "\DSOAL\Documentation"

[Components]
Name: "patch\dsoal"; Description: "{cm:CompDsoal}"; Types: custom

[Files]
Source: "{#DsoalUrl}"; DestDir: "{#DsoalTmp}"; DestName: "dsoal-outer.zip"; \
    Hash: "{#DsoalSha256}"; ExternalSize: {#DsoalOuterSize}; \
    Components: patch\dsoal; \
    Flags: external download extractarchive recursesubdirs ignoreversion

; Extracts what the row above produced. Rows run in the order they are listed, so the source is there
; by the time this one is reached.
Source: "{#DsoalTmp}\DSOAL_r694.zip"; DestDir: "{#DsoalFiles}"; \
    ExternalSize: {#DsoalInnerSize}; \
    Components: patch\dsoal; \
    Flags: external extractarchive recursesubdirs ignoreversion

; dsound.dll looks for its OpenAL renderer beside itself under exactly this name, so the two travel
; together. A stranger holding the dsound.dll name is moved aside in [Code] before these rows run.
Source: "{#DsoalWin32}\dsound.dll"; DestDir: "{app}"; \
    Components: patch\dsoal; Flags: external ignoreversion
Source: "{#DsoalWin32}\dsoal-aldrv.dll"; DestDir: "{app}"; \
    Components: patch\dsoal; Flags: external ignoreversion

; Thirty kilobytes of commented defaults, installed so the switches are reachable with their
; explanations beside them.
Source: "{#DsoalWin32}\alsoft.ini"; DestDir: "{app}"; \
    Components: patch\dsoal; Flags: external ignoreversion

; log.cmd is in the archive and is not installed. It starts the game with logging variables set,
; which would put a second way to start the game in the game folder.

; Both binaries are LGPL, so the notices travel with them. In their own folder rather than loose in
; the game root, because there are six of them.
Source: "{#DsoalDocs}\DSOAL-License.txt"; DestDir: "{app}\DSOAL-Licenses"; \
    Components: patch\dsoal; Flags: external ignoreversion
Source: "{#DsoalDocs}\DSOAL-License_fmt.txt"; DestDir: "{app}\DSOAL-Licenses"; \
    Components: patch\dsoal; Flags: external ignoreversion
Source: "{#DsoalDocs}\OpenALSoft-License.txt"; DestDir: "{app}\DSOAL-Licenses"; \
    Components: patch\dsoal; Flags: external ignoreversion
Source: "{#DsoalDocs}\OpenALSoft-License_Apache-2.0.txt"; DestDir: "{app}\DSOAL-Licenses"; \
    Components: patch\dsoal; Flags: external ignoreversion
Source: "{#DsoalDocs}\OpenALSoft-License_BSD-3-Clause.txt"; DestDir: "{app}\DSOAL-Licenses"; \
    Components: patch\dsoal; Flags: external ignoreversion
Source: "{#DsoalDocs}\OpenALSoft-License_PFFFT.txt"; DestDir: "{app}\DSOAL-Licenses"; \
    Components: patch\dsoal; Flags: external ignoreversion
