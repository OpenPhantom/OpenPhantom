; The OpenPhantom patch and the libVLC runtime the movie player needs.
;
; Nothing here is compiled into the installer. Both archives are downloaded during installation,
; checked against their hash, unpacked into the temporary folder, and only the ticked pieces are
; copied into the game.

; The tag and the version are two different strings, so read both off the release page. v0.2.0 was
; tagged "v0.2.0"; 0.2.1 is tagged "0.2.1" and still carries OpenPhantom-patch-v0.2.1.zip. Deriving
; one from the other gives a folder that does not exist, and GitHub answers that with a 404, which
; stops the installation instead of failing a hash. Nothing catches it at compile time.
;
;     certutil -hashfile OpenPhantom-patch-vX.Y.Z.zip SHA256
;
; PatchUnpackedSize is the total of the files inside the archive, not the size of the archive. An
; extracting row has to declare the uncompressed total, otherwise the progress bar reports the step
; finished while it is still writing files.
;
;     Add-Type -AssemblyName System.IO.Compression.FileSystem
;     ([IO.Compression.ZipFile]::OpenRead('OpenPhantom-patch-vX.Y.Z.zip')).Entries |
;         Measure-Object -Property Length -Sum
#define PatchTag           "0.2.1"
#define PatchVersion       "v0.2.1"
#define PatchUrl           "https://github.com/OpenPhantom/OpenPhantom/releases/download/" + PatchTag + "/OpenPhantom-patch-" + PatchVersion + ".zip"
#define PatchSha256        "6bf30e1f9e3b931bbe31ae804c7f765927a22f1b7985c66f5e24eabf66063c4f"
#define PatchUnpackedSize  11473349

; Every row below reads out of here and none of them names the download, so a version bump stays in
; the lines above. Inno removes the folder when the installer finishes.
#define PatchTmp           "{tmp}\openphantom"

; libVLC, from VideoLAN. The version drives the URL and the folder name inside the archive. The hash
; and the size have to be measured again with every version; VideoLAN publishes a .sha256 beside the
; archive and VlcSha256 is that value.
#define VlcVersion      "3.0.23"
#define VlcUrl          "https://get.videolan.org/vlc/" + VlcVersion + "/win32/vlc-" + VlcVersion + "-win32.zip"
#define VlcSha256       "8a12a33416ecb319ee06569fd7b5319af7362dfb6ef680df74b4a2c49af6515a"
#define VlcUnpackedSize 185639150
#define VlcTmp          "{tmp}\vlc"
#define VlcRoot         VlcTmp + "\vlc-" + VlcVersion

; libVLC finds a plugin by scanning the tree, so the folder under mods\fmv\plugins has to be the same
; word as the folder in the archive. Deriving the destination from the source is what stops the two
; drifting apart across thirty rows; a plugin one folder over is not found and the only symptom
; is libvlc_new refusing.
;
; This is the only generated row set here. Everywhere else a literal row is checked when this file is
; compiled, but these sources are external and inside an archive that does not exist yet, so there is
; no check to lose.
#define VlcPlugin(Str Folder, Str Name) \
    "Source: """ + VlcRoot + "\plugins\" + Folder + "\" + Name + """; " + \
    "DestDir: ""{app}\mods\fmv\plugins\" + Folder + """; " + \
    "Components: patch\fmv_player\runtime; Flags: external ignoreversion"

[Components]
; Three tiers, and the tier is the Types and Flags rather than a label anywhere.
;
;   fixed          part of the patch. Ticking the patch installs it and it cannot be unticked.
;   full custom    recommended. A full installation takes it, a custom one can leave it out.
;   custom         offered. A full installation does not take it.
;
; Unticking the parent takes all of them with it, including the loader, which alone patches nothing.
Name: "patch"; Description: "{cm:CompPatch}"; Types: full custom

; ---- part of the patch ---------------------------------------------------------------------------
; The wrapper is here because the game does not reach a working Direct3D without it, and its ini was
; derived from the engine, so the two are one component. The next two repair crashes. crash_report
; does nothing until the process dies and is then the only thing that says what happened. The last
; two repair audio faults that have no upside to keeping.
Name: "patch\wrapper";             Description: "{cm:CompWrapper}";     Types: full custom; Flags: fixed
Name: "patch\crt_copy_fix";        Description: "{cm:CompCrtCopy}";     Types: full custom; Flags: fixed
Name: "patch\render_guard";        Description: "{cm:CompRenderGuard}"; Types: full custom; Flags: fixed
Name: "patch\crash_report";        Description: "{cm:CompCrashRep}";    Types: full custom; Flags: fixed
Name: "patch\imuse_fix";           Description: "{cm:CompAudio}";       Types: full custom; Flags: fixed
Name: "patch\sfx_volume_save_fix"; Description: "{cm:CompSfxVolume}";   Types: full custom; Flags: fixed

; ---- recommended -----------------------------------------------------------------------------------
Name: "patch\enhanced_resolution"; Description: "{cm:CompResolution}";  Types: full custom
Name: "patch\framerate_fix";       Description: "{cm:CompFramerate}";   Types: full custom
Name: "patch\variable_fov";        Description: "{cm:CompFov}";         Types: full custom
Name: "patch\hud_ratio_scaling";   Description: "{cm:CompHud}";         Types: full custom
Name: "patch\decal_fix";           Description: "{cm:CompDecal}";       Types: full custom
Name: "patch\view_distance_fix";   Description: "{cm:CompViewDist}";    Types: full custom

; ---- offered ---------------------------------------------------------------------------------------
Name: "patch\large_textures";      Description: "{cm:CompLargeTex}";    Types: custom
Name: "patch\enhanced_input";      Description: "{cm:CompInput}";       Types: custom
Name: "patch\dismemberment";       Description: "{cm:CompDismember}";   Types: custom
Name: "patch\dev_overlay";         Description: "{cm:CompDevOverlay}";  Types: custom
Name: "patch\diagnostics";         Description: "{cm:CompDiag}";        Types: custom

Name: "patch\fmv_player";          Description: "{cm:CompFmvPlayer}";   Types: custom

; Part of the player rather than a choice beside it. Without a decoder the DLL installs, finds
; nothing, and every movie goes on playing as Bink, so the only thing the separate line buys is
; naming the download size before it happens.
Name: "patch\fmv_player\runtime";  Description: "{cm:CompFmvRuntime}";  Types: custom; Flags: fixed

[Files]
; Rows are processed in the order listed, so this has to come before everything that reads out of the
; unpacked folder.
Source: "{#PatchUrl}"; DestDir: "{#PatchTmp}"; DestName: "openphantom-patch.zip"; \
    {#HashParam(PatchSha256)}ExternalSize: {#PatchUnpackedSize}; \
    Components: patch; Flags: external download extractarchive recursesubdirs ignoreversion

; A stranger holding the dinput.dll name is moved aside in [Code] before this row runs.
Source: "{#PatchTmp}\dinput.dll"; DestDir: "{app}"; \
    Components: patch; Flags: external ignoreversion

; Replaced on every installation. This is not a preference file: it names one section per DLL, and
; DLLs are added, renamed and removed between releases, so a file kept from an older one carries dead
; sections and lacks the new keys, which looks configured and behaves like defaults. The previous file
; is copied to engine_fixes.ini.previous first, on both installation paths.
Source: "{#PatchTmp}\engine_fixes.ini"; DestDir: "{app}"; \
    Components: patch; Flags: external ignoreversion

Source: "{#PatchTmp}\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; \
    Components: patch; Flags: external ignoreversion

Source: "{#PatchTmp}\ddraw.dll"; DestDir: "{app}"; \
    Components: patch\wrapper; Flags: external ignoreversion
Source: "{#PatchTmp}\dxwrapper.dll"; DestDir: "{app}"; \
    Components: patch\wrapper; Flags: external ignoreversion
Source: "{#PatchTmp}\dxwrapper-License.txt"; DestDir: "{app}"; \
    Components: patch\wrapper; Flags: external ignoreversion

; Same rule as engine_fixes.ini, and preserved as dxwrapper.ini.previous the same way. An older or
; foreign one is the difference between the wrapper this release was tested with and one nobody has
; run.
Source: "{#PatchTmp}\dxwrapper.ini"; DestDir: "{app}"; \
    Components: patch\wrapper; Flags: external ignoreversion

; One row per DLL, and the component name matches the file name so a rename is reviewable. The fixes
; are independent of each other, which is why there is no dependency between the rows.
Source: "{#PatchTmp}\mods\crt_copy_fix.dll";        DestDir: "{app}\mods"; \
    Components: patch\crt_copy_fix;        Flags: external ignoreversion
Source: "{#PatchTmp}\mods\enhanced_resolution.dll"; DestDir: "{app}\mods"; \
    Components: patch\enhanced_resolution; Flags: external ignoreversion
Source: "{#PatchTmp}\mods\framerate_fix.dll";       DestDir: "{app}\mods"; \
    Components: patch\framerate_fix;       Flags: external ignoreversion
Source: "{#PatchTmp}\mods\variable_fov.dll";        DestDir: "{app}\mods"; \
    Components: patch\variable_fov;        Flags: external ignoreversion
Source: "{#PatchTmp}\mods\hud_ratio_scaling.dll";   DestDir: "{app}\mods"; \
    Components: patch\hud_ratio_scaling;   Flags: external ignoreversion
Source: "{#PatchTmp}\mods\enhanced_input.dll";      DestDir: "{app}\mods"; \
    Components: patch\enhanced_input;      Flags: external ignoreversion
Source: "{#PatchTmp}\mods\imuse_fix.dll";           DestDir: "{app}\mods"; \
    Components: patch\imuse_fix;           Flags: external ignoreversion
Source: "{#PatchTmp}\mods\sfx_volume_save_fix.dll"; DestDir: "{app}\mods"; \
    Components: patch\sfx_volume_save_fix; Flags: external ignoreversion
Source: "{#PatchTmp}\mods\decal_fix.dll";           DestDir: "{app}\mods"; \
    Components: patch\decal_fix;           Flags: external ignoreversion
Source: "{#PatchTmp}\mods\render_guard.dll";        DestDir: "{app}\mods"; \
    Components: patch\render_guard;        Flags: external ignoreversion
; effect_clock is part of framerate_fix rather than a choice of its own: at 30 fps it changes nothing
; visible, and it only does anything once the frame rate is free.
Source: "{#PatchTmp}\mods\effect_clock.dll";        DestDir: "{app}\mods"; \
    Components: patch\framerate_fix;       Flags: external ignoreversion
Source: "{#PatchTmp}\mods\large_textures.dll";      DestDir: "{app}\mods"; \
    Components: patch\large_textures;      Flags: external ignoreversion
Source: "{#PatchTmp}\mods\view_distance_fix.dll";   DestDir: "{app}\mods"; \
    Components: patch\view_distance_fix;   Flags: external ignoreversion
Source: "{#PatchTmp}\mods\dismemberment.dll";       DestDir: "{app}\mods"; \
    Components: patch\dismemberment;       Flags: external ignoreversion
Source: "{#PatchTmp}\mods\crash_report.dll";        DestDir: "{app}\mods"; \
    Components: patch\crash_report;        Flags: external ignoreversion
Source: "{#PatchTmp}\mods\dev_overlay.dll";         DestDir: "{app}\mods"; \
    Components: patch\dev_overlay;         Flags: external ignoreversion
Source: "{#PatchTmp}\mods\diagnostics.dll";         DestDir: "{app}\mods"; \
    Components: patch\diagnostics;         Flags: external ignoreversion

Source: "{#PatchTmp}\mods\fmv_player.dll";          DestDir: "{app}\mods"; \
    Components: patch\fmv_player;          Flags: external ignoreversion

; The conversion tools. They work on movies the player already has and carry no content of their own,
; so they install with the DLL rather than being a choice. "Convert Movies.bat" is the entry point
; and is spelled the way it is because a player reads it in Explorer.
Source: "{#PatchTmp}\tools\convert_movies.ps1"; DestDir: "{app}\tools"; \
    Components: patch\fmv_player; Flags: external ignoreversion
Source: "{#PatchTmp}\tools\Convert Movies.bat"; DestDir: "{app}\tools"; \
    Components: patch\fmv_player; Flags: external ignoreversion

; ExternalSize is what the archive expands to, not the 78 MB that comes down the wire.
Source: "{#VlcUrl}"; DestDir: "{#VlcTmp}"; DestName: "vlc-win32.zip"; \
    {#HashParam(VlcSha256)}ExternalSize: {#VlcUnpackedSize}; \
    Components: patch\fmv_player\runtime; \
    Flags: external download extractarchive recursesubdirs ignoreversion

; The archive unpacks to about 177 MB; what follows is the 23.4 MB of it that playing one H.264 and
; AAC file into a window needs. All 33 paths were read out of vlc-3.0.23-win32.zip, which has to be
; done again when VlcVersion changes: these are external, so a name wrong by one letter compiles and
; fails at the end of a 78 MB download.
;
; No row here carries a flag that forgives a missing source. An incomplete plugin set does not fail
; loudly, it fails as libvlc_new refusing, and the player reads that as the feature not working.
Source: "{#VlcRoot}\libvlc.dll"; DestDir: "{app}\mods\fmv"; \
    Components: patch\fmv_player\runtime; Flags: external ignoreversion
Source: "{#VlcRoot}\libvlccore.dll"; DestDir: "{app}\mods\fmv"; \
    Components: patch\fmv_player\runtime; Flags: external ignoreversion

; LGPL, so the licence travels with the binaries.
Source: "{#VlcRoot}\COPYING.txt"; DestDir: "{app}\mods\fmv"; DestName: "vlc-License.txt"; \
    Components: patch\fmv_player\runtime; Flags: external ignoreversion

; Reading the file off the disk.
#emit VlcPlugin("access", "libfilesystem_plugin.dll")

; The container: the MP4 reader and the raw elementary stream reader beside it.
#emit VlcPlugin("demux", "libmp4_plugin.dll")
#emit VlcPlugin("demux", "libes_plugin.dll")

; Decoding. libavcodec_plugin.dll is 16.9 MB of the 23.4 MB installed here. The two beside it hand
; H.264 to the GPU when the driver offers to take it.
#emit VlcPlugin("codec", "libavcodec_plugin.dll")
#emit VlcPlugin("codec", "libdxva2_plugin.dll")
#emit VlcPlugin("codec", "libd3d11va_plugin.dll")

; Cutting the compressed streams into frames before the decoder gets them.
#emit VlcPlugin("packetizer", "libpacketizer_h264_plugin.dll")
#emit VlcPlugin("packetizer", "libpacketizer_mpeg4audio_plugin.dll")
#emit VlcPlugin("packetizer", "libpacketizer_copy_plugin.dll")

; drawable is the module that accepts a window handle from outside libVLC, which is what
; libvlc_media_player_set_hwnd hands over. Without it the handle reaches nothing and libVLC opens a
; window of its own instead: the movie plays, in a bordered window in the middle of the screen,
; rather than in the borderless monitor-sized overlay. It fails silently because a machine with VLC
; installed has this plugin anyway, so only a machine relying on the bundled set ever sees it.
#emit VlcPlugin("video_output", "libdrawable_plugin.dll")

; Direct3D 11, then Direct3D 9, then GDI as the one that works everywhere.
;
; libdirectdraw_plugin.dll is in the archive and is not copied. With it present libVLC may pick a
; DirectDraw output, which would send the movie straight back through the translation layer this
; feature exists to avoid. Leaving it out of the tree is what forces Direct3D or GDI.
#emit VlcPlugin("video_output", "libdirect3d11_plugin.dll")
#emit VlcPlugin("video_output", "libdirect3d9_plugin.dll")
#emit VlcPlugin("video_output", "libwingdi_plugin.dll")

; Keeps the screen saver and the display timeout away for the length of a movie, which can be several
; minutes with no input at all.
#emit VlcPlugin("video_output", "libwinhibit_plugin.dll")

; The filter halves of the two Direct3D outputs, in folders of their own because the folder name is
; part of how they are found.
#emit VlcPlugin("d3d9", "libdirect3d9_filters_plugin.dll")
#emit VlcPlugin("d3d11", "libdirect3d11_filters_plugin.dll")

; Turning what the decoder produces into what the output accepts.
#emit VlcPlugin("video_chroma", "libswscale_plugin.dll")
#emit VlcPlugin("video_chroma", "libi420_rgb_sse2_plugin.dll")
#emit VlcPlugin("video_chroma", "libi420_nv12_plugin.dll")
#emit VlcPlugin("video_chroma", "libchain_plugin.dll")

; Three, because which one is available depends on the machine.
#emit VlcPlugin("audio_output", "libmmdevice_plugin.dll")
#emit VlcPlugin("audio_output", "libdirectsound_plugin.dll")
#emit VlcPlugin("audio_output", "libwaveout_plugin.dll")

; Sample format, channel layout and sample rate all have to agree before the output takes the stream.
#emit VlcPlugin("audio_filter", "libaudio_format_plugin.dll")
#emit VlcPlugin("audio_filter", "libtrivial_channel_mixer_plugin.dll")
#emit VlcPlugin("audio_filter", "libsimple_channel_mixer_plugin.dll")
#emit VlcPlugin("audio_filter", "libugly_resampler_plugin.dll")

; Mixing down to what the output was opened with.
#emit VlcPlugin("audio_mixer", "libfloat_mixer_plugin.dll")
#emit VlcPlugin("audio_mixer", "libinteger_mixer_plugin.dll")

; Without a logger plugin libVLC's account of why it would not play a file goes nowhere, and the only
; symptom left is a movie that does not start.
#emit VlcPlugin("logger", "libfile_logger_plugin.dll")

; There is no [Run] entry for the movie conversion. It is a wizard page in the main script's [Code]
; instead, because a [Run] entry cannot ask for a size, show a progress bar or report what happened.
; The tools are installed either way, so "Convert Movies.bat" by hand still works.
