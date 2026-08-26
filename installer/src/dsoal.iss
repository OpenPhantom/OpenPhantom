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
; DSOAL fixes the second. ApplySoundProvider in the main script writes the provider name for
; the first, one key in place, leaving the rest of the player's settings alone.

; DSOAL has no versioned releases. The "latest-master" tag is replaced with every new build, while
; "archive" keeps each build at an address of its own, and DsoalRevision records which one the files
; in dist came out of. The archive is not immutable either: r694 was once replaced under its own
; name with different bytes, which is the reason to record the revision rather than trust the name.
;
; The build carries its own commit in DSOAL-Version.txt, and OpenAL Soft's in OpenALSoft-Version.txt.
; Those are what the source archives that ship with a release are pinned to.
#define DsoalRevision       "r694"
#define DsoalSrc            "dist\dsoal"

[Components]
Name: "patch\dsoal"; Description: "{cm:CompDsoal}"; Types: everything custom

[Files]

; dsound.dll looks for its OpenAL renderer beside itself under exactly this name, so the two travel
; together. A stranger holding the dsound.dll name is moved aside in [Code] before these rows run.
Source: "{#DsoalSrc}\dsound.dll"; DestDir: "{app}"; \
    Components: patch\dsoal; Flags: ignoreversion
Source: "{#DsoalSrc}\dsoal-aldrv.dll"; DestDir: "{app}"; \
    Components: patch\dsoal; Flags: ignoreversion

; Thirty kilobytes of commented defaults, installed so the switches are reachable with their
; explanations beside them.
Source: "{#DsoalSrc}\alsoft.ini"; DestDir: "{app}"; \
    Components: patch\dsoal; Flags: ignoreversion

; log.cmd is in the archive and is not installed. It starts the game with logging variables set,
; which would put a second way to start the game in the game folder.

; Both binaries are LGPL, so the notices travel with them. In their own folder rather than loose in
; the game root, because there are six of them.
Source: "{#DsoalSrc}\DSOAL-License.txt"; DestDir: "{app}\DSOAL-Licenses"; \
    Components: patch\dsoal; Flags: ignoreversion
Source: "{#DsoalSrc}\DSOAL-License_fmt.txt"; DestDir: "{app}\DSOAL-Licenses"; \
    Components: patch\dsoal; Flags: ignoreversion
Source: "{#DsoalSrc}\OpenALSoft-License.txt"; DestDir: "{app}\DSOAL-Licenses"; \
    Components: patch\dsoal; Flags: ignoreversion
Source: "{#DsoalSrc}\OpenALSoft-License_Apache-2.0.txt"; DestDir: "{app}\DSOAL-Licenses"; \
    Components: patch\dsoal; Flags: ignoreversion
Source: "{#DsoalSrc}\OpenALSoft-License_BSD-3-Clause.txt"; DestDir: "{app}\DSOAL-Licenses"; \
    Components: patch\dsoal; Flags: ignoreversion
Source: "{#DsoalSrc}\OpenALSoft-License_PFFFT.txt"; DestDir: "{app}\DSOAL-Licenses"; \
    Components: patch\dsoal; Flags: ignoreversion
