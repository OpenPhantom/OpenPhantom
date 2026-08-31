; The OpenPhantom patch and the libVLC runtime the movie player needs.
;
; Both are compiled into the installer, out of dist\patch and dist\vlc, and nothing is fetched at
; install time. What sits in dist is already only the pieces these rows install: the patch as it
; comes out of its release archive, and 33 files of the 851 in VideoLAN's zip.

; PatchVersion records which release dist\patch was taken from, and is the only thing to change when
; it is refreshed. Nothing derives a URL from it any more, so the tag-versus-version trap that used
; to live here is gone with the download.
;
; To refresh: take the files out of OpenPhantom-patch-vX.Y.Z.zip into dist\patch, keeping the folder
; layout, since every row below names a path inside it.
#define PatchVersion       "v0.2.1"
#define PatchSrc           "dist\patch"

; dxwrapper is DirectDraw-to-Direct3D translation from a separate upstream project, not part of the
; patch, so it is kept apart from it: dist\patch is refreshed wholesale out of a patch release and
; would otherwise take dxwrapper with it, including the edit recorded below.
#define DxWrapperSrc       "dist\dxwrapper"

; The converter is run from here rather than from the copy installed beside the game. See
; ConvertMovies: the game folder is writable by ordinary users on purpose and this runs with Setup's
; rights, so the script it executes must not sit somewhere a user could have replaced it.
#define PatchTmp           "{tmp}\openphantom"

; libVLC, from VideoLAN, under the GPL v2 in its COPYING.txt. VlcVersion records which build the
; files in dist\vlc came out of. Refreshing it means taking the 33 paths below out of the new
; vlc-X.Y.Z-win32.zip again, because a plugin that moved between versions fails silently.
#define VlcVersion      "3.0.23"
#define VlcSrc          "dist\vlc"

; FFmpeg, for the cutscene converter. It is the only thing in this installer that used to be fetched
; after installation rather than during it: convert_movies.ps1 downloads a pinned 106 MB build on
; first use and caches it. Carrying it is what makes the whole thing work with no network at all.
;
; This is the exact build the script pins, so it is what would have been downloaded anyway:
; ffmpeg-9.0-essentials_build.zip, sha256 e6b54767a6065919048f1a098eb27211ca4e12b4348a05d88777a5855d0b6e71
;
; Only ffmpeg.exe is taken. The script never calls ffprobe or ffplay, and those are 100 MB each.
;
; It is a GPL v3 build rather than LGPL, because gyan.dev's "essentials" links x264 statically.
; That is why x264's source has to travel with a release alongside FFmpeg's.
#define FFmpegSrc       "dist\ffmpeg"

; libVLC finds a plugin by scanning the tree, so the folder under mods\fmv\plugins has to be the same
; word as the folder in the archive. Deriving the destination from the source is what stops the two
; drifting apart across thirty rows; a plugin one folder over is not found and the only symptom
; is libvlc_new refusing.
;
; This is the only generated row set here. Everywhere else a literal row is checked when this file is
; compiled, and these are checked the same way now that they are ordinary files in dist, so there is
; no check to lose.
#define VlcPlugin(Str Folder, Str Name) \
    "Source: """ + VlcSrc + "\plugins\" + Folder + "\" + Name + """; " + \
    "DestDir: ""{app}\mods\fmv\plugins\" + Folder + """; " + \
    "Components: patch\fmv_player\runtime; Flags: ignoreversion"

[Components]
; Three tiers, and the tier is the Types and Flags rather than a label anywhere. Every row below
; also carries "everything", the type that leaves nothing unticked, so what separates the tiers is
; whether "full" takes them.
;
;   fixed                   part of the patch. Ticking the patch installs it and it cannot be
;                           unticked.
;   everything full custom  a fix or an improvement, wanted on any machine. "full" takes it.
;   everything custom       an extra: a mod, a cheat, a large download, or something that only
;                           pays off with particular hardware. "full" leaves it out, which is
;                           what its description promises.
;
; Unticking the parent takes all of them with it, including the loader, which alone patches nothing.
Name: "patch"; Description: "{cm:CompPatch}"; Types: everything full custom

; ---- part of the patch ---------------------------------------------------------------------------
; The wrapper is here because the game does not reach a working Direct3D without it, and its ini was
; derived from the engine, so the two are one component. The next three repair crashes. crash_report
; does nothing until the process dies and is then the only thing that says what happened. The last
; two repair audio faults that have no upside to keeping.
Name: "patch\wrapper";             Description: "{cm:CompWrapper}";     Types: everything full custom; Flags: fixed
Name: "patch\crt_copy_fix";        Description: "{cm:CompCrtCopy}";     Types: everything full custom; Flags: fixed
Name: "patch\render_guard";        Description: "{cm:CompRenderGuard}"; Types: everything full custom; Flags: fixed
Name: "patch\crash_report";        Description: "{cm:CompCrashRep}";    Types: everything full custom; Flags: fixed
Name: "patch\sound_lifetime_fix";  Description: "{cm:CompSoundLife}";   Types: everything full custom; Flags: fixed
Name: "patch\imuse_fix";           Description: "{cm:CompAudio}";       Types: everything full custom; Flags: fixed
Name: "patch\sfx_volume_save_fix"; Description: "{cm:CompSfxVolume}";   Types: everything full custom; Flags: fixed

; ---- recommended -----------------------------------------------------------------------------------
Name: "patch\ground_clip_fix";    Description: "{cm:CompGroundClip}";  Types: everything full custom
Name: "patch\enhanced_resolution"; Description: "{cm:CompResolution}";  Types: everything full custom
Name: "patch\framerate_fix";       Description: "{cm:CompFramerate}";   Types: everything full custom
Name: "patch\variable_fov";        Description: "{cm:CompFov}";         Types: everything full custom
Name: "patch\hud_ratio_scaling";   Description: "{cm:CompHud}";         Types: everything full custom
Name: "patch\decal_fix";           Description: "{cm:CompDecal}";       Types: everything full custom
Name: "patch\dialogue_anim_fix";   Description: "{cm:CompDialogueAnim}"; Types: everything full custom
Name: "patch\view_distance_fix";   Description: "{cm:CompViewDist}";    Types: everything full custom

; ---- offered ---------------------------------------------------------------------------------------
Name: "patch\large_textures";      Description: "{cm:CompLargeTex}";    Types: everything custom
Name: "patch\enhanced_input";      Description: "{cm:CompInput}";       Types: everything custom
Name: "patch\controller_input";    Description: "{cm:CompController}";  Types: everything custom
Name: "patch\dismemberment";       Description: "{cm:CompDismember}";   Types: everything custom
Name: "patch\dev_overlay";         Description: "{cm:CompDevOverlay}";  Types: everything custom
Name: "patch\diagnostics";         Description: "{cm:CompDiag}";        Types: everything custom

Name: "patch\fmv_player";          Description: "{cm:CompFmvPlayer}";   Types: everything custom

; Part of the player rather than a choice beside it. Without a decoder the DLL installs, finds
; nothing, and every movie goes on playing as Bink, so the only thing the separate line buys is
; naming what the decoder costs before it is installed.
Name: "patch\fmv_player\runtime";  Description: "{cm:CompFmvRuntime}";  Types: everything custom; Flags: fixed

[Files]
; A stranger holding the dinput.dll name is moved aside in [Code] before this row runs.
Source: "{#PatchSrc}\dinput.dll"; DestDir: "{app}"; \
    Components: patch; Flags: ignoreversion

; Replaced on every installation. This is not a preference file: it names one section per DLL, and
; DLLs are added, renamed and removed between releases, so a file kept from an older one carries dead
; sections and lacks the new keys, which looks configured and behaves like defaults. The previous file
; is copied to engine_fixes.ini.previous first, on both installation paths.
Source: "{#PatchSrc}\engine_fixes.ini"; DestDir: "{app}"; \
    Components: patch; Flags: ignoreversion

Source: "{#PatchSrc}\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; \
    Components: patch; Flags: ignoreversion

; The file above came out of the patch archive and is accurate about that archive: the patch and
; DxWrapper, and nothing else. It says of libVLC and FFmpeg that neither "is redistributed here",
; which was true while the installer downloaded them and is not any more.
;
; Rather than edit it, which would make it wrong about the archive it describes and would be undone
; the next time dist\patch is refreshed, this second file covers what the installer adds and opens
; by correcting that one paragraph. Installed with the patch, because everything it names is
; installed with the patch or under it.
Source: "dist\THIRD-PARTY-NOTICES-Installer.txt"; DestDir: "{app}"; \
    Components: patch; Flags: ignoreversion

Source: "{#DxWrapperSrc}\ddraw.dll"; DestDir: "{app}"; \
    Components: patch\wrapper; Flags: ignoreversion
Source: "{#DxWrapperSrc}\dxwrapper.dll"; DestDir: "{app}"; \
    Components: patch\wrapper; Flags: ignoreversion
Source: "{#DxWrapperSrc}\dxwrapper-License.txt"; DestDir: "{app}"; \
    Components: patch\wrapper; Flags: ignoreversion

; Same rule as engine_fixes.ini, and preserved as dxwrapper.ini.previous the same way. An older or
; foreign one is the difference between the wrapper this release was tested with and one nobody has
; run.
;
; This copy is not upstream's untouched: six settings are tuned for this game, and they are listed
; in THIRD-PARTY-NOTICES.md. Re-apply them when dist\dxwrapper is refreshed, by diffing this file
; against the dxwrapper.ini inside the release archive rather than by working from that list.
Source: "{#DxWrapperSrc}\dxwrapper.ini"; DestDir: "{app}"; \
    Components: patch\wrapper; Flags: ignoreversion

; One row per DLL, and the component name matches the file name so a rename is reviewable. The fixes
; are independent of each other, which is why there is no dependency between the rows.
Source: "{#PatchSrc}\mods\crt_copy_fix.dll";        DestDir: "{app}\mods"; \
    Components: patch\crt_copy_fix;        Flags: ignoreversion
Source: "{#PatchSrc}\mods\ground_clip_fix.dll";     DestDir: "{app}\mods"; \
    Components: patch\ground_clip_fix;     Flags: ignoreversion
Source: "{#PatchSrc}\mods\enhanced_resolution.dll"; DestDir: "{app}\mods"; \
    Components: patch\enhanced_resolution; Flags: ignoreversion

; The menu artwork converter. Like the movie one it carries no content: it makes bigger copies of
; the pictures already inside the player's own big.lab and LOCALIZE.LAB, into a menu_hd folder the
; DLL mounts and reads the scale out of. Without it MenuScale finds no converted artwork and leaves
; the menus exactly as they shipped, so the two halves install together or the feature is absent.
;
; FOUR FILES BECAUSE OF LINUX. "Convert Menu Art.bat" drives convert_menu.ps1, which resamples with
; GDI+; neither half works under Proton, where Wine ships no PowerShell and System.Drawing.Common is
; Windows-only on .NET Core. convert_menu.sh drives convert_menu.py, which needs nothing but Python
; 3 and is already on the Steam Deck. The two produce byte-identical output from the same input.
Source: "{#PatchSrc}\tools\convert_menu.ps1";      DestDir: "{app}\tools"; \
    Components: patch\enhanced_resolution; Flags: ignoreversion
Source: "{#PatchSrc}\tools\Convert Menu Art.bat";  DestDir: "{app}\tools"; \
    Components: patch\enhanced_resolution; Flags: ignoreversion
Source: "{#PatchSrc}\tools\convert_menu.py";       DestDir: "{app}\tools"; \
    Components: patch\enhanced_resolution; Flags: ignoreversion
Source: "{#PatchSrc}\tools\convert_menu.sh";       DestDir: "{app}\tools"; \
    Components: patch\enhanced_resolution; Flags: ignoreversion
Source: "{#PatchSrc}\mods\framerate_fix.dll";       DestDir: "{app}\mods"; \
    Components: patch\framerate_fix;       Flags: ignoreversion
Source: "{#PatchSrc}\mods\variable_fov.dll";        DestDir: "{app}\mods"; \
    Components: patch\variable_fov;        Flags: ignoreversion
Source: "{#PatchSrc}\mods\hud_ratio_scaling.dll";   DestDir: "{app}\mods"; \
    Components: patch\hud_ratio_scaling;   Flags: ignoreversion
Source: "{#PatchSrc}\mods\enhanced_input.dll";      DestDir: "{app}\mods"; \
    Components: patch\enhanced_input;      Flags: ignoreversion
Source: "{#PatchSrc}\mods\controller_input.dll";    DestDir: "{app}\mods"; \
    Components: patch\controller_input;    Flags: ignoreversion
Source: "{#PatchSrc}\mods\imuse_fix.dll";           DestDir: "{app}\mods"; \
    Components: patch\imuse_fix;           Flags: ignoreversion
Source: "{#PatchSrc}\mods\sfx_volume_save_fix.dll"; DestDir: "{app}\mods"; \
    Components: patch\sfx_volume_save_fix; Flags: ignoreversion
Source: "{#PatchSrc}\mods\decal_fix.dll";           DestDir: "{app}\mods"; \
    Components: patch\decal_fix;           Flags: ignoreversion
Source: "{#PatchSrc}\mods\dialogue_anim_fix.dll";   DestDir: "{app}\mods"; \
    Components: patch\dialogue_anim_fix;   Flags: ignoreversion
Source: "{#PatchSrc}\mods\render_guard.dll";        DestDir: "{app}\mods"; \
    Components: patch\render_guard;        Flags: ignoreversion
; effect_clock is part of framerate_fix rather than a choice of its own: at 30 fps it changes nothing
; visible, and it only does anything once the frame rate is free.
Source: "{#PatchSrc}\mods\effect_clock.dll";        DestDir: "{app}\mods"; \
    Components: patch\framerate_fix;       Flags: ignoreversion
Source: "{#PatchSrc}\mods\large_textures.dll";      DestDir: "{app}\mods"; \
    Components: patch\large_textures;      Flags: ignoreversion
Source: "{#PatchSrc}\mods\view_distance_fix.dll";   DestDir: "{app}\mods"; \
    Components: patch\view_distance_fix;   Flags: ignoreversion
Source: "{#PatchSrc}\mods\dismemberment.dll";       DestDir: "{app}\mods"; \
    Components: patch\dismemberment;       Flags: ignoreversion
Source: "{#PatchSrc}\mods\crash_report.dll";        DestDir: "{app}\mods"; \
    Components: patch\crash_report;        Flags: ignoreversion
Source: "{#PatchSrc}\mods\sound_lifetime_fix.dll";  DestDir: "{app}\mods"; \
    Components: patch\sound_lifetime_fix;  Flags: ignoreversion
Source: "{#PatchSrc}\mods\dev_overlay.dll";         DestDir: "{app}\mods"; \
    Components: patch\dev_overlay;         Flags: ignoreversion
Source: "{#PatchSrc}\mods\diagnostics.dll";         DestDir: "{app}\mods"; \
    Components: patch\diagnostics;         Flags: ignoreversion

Source: "{#PatchSrc}\mods\fmv_player.dll";          DestDir: "{app}\mods"; \
    Components: patch\fmv_player;          Flags: ignoreversion

; The conversion tools. They work on movies the player already has and carry no content of their own,
; so they install with the DLL rather than being a choice. "Convert Movies.bat" is the entry point
; and is spelled the way it is because a player reads it in Explorer.
Source: "{#PatchSrc}\tools\convert_movies.ps1"; DestDir: "{app}\tools"; \
    Components: patch\fmv_player; Flags: ignoreversion
Source: "{#PatchSrc}\tools\Convert Movies.bat"; DestDir: "{app}\tools"; \
    Components: patch\fmv_player; Flags: ignoreversion

; A second copy of the converter, in the temporary folder, and it is the one ConvertMovies runs.
; The copy above is for the player to run later; this one runs during installation with Setup's
; rights, and {tmp} is the only one of the two that an ordinary user cannot write to first.
; Removed when Setup finishes.
Source: "{#PatchSrc}\tools\convert_movies.ps1"; DestDir: "{#PatchTmp}\tools"; \
    Components: patch\fmv_player; Flags: ignoreversion deleteafterinstall

; Installed beside libVLC, which is where the rest of the cutscene feature keeps its binaries, and
; put on PATH by the two things that run the converter. convert_movies.ps1 looks in its own cache,
; then on PATH, then downloads; being on PATH is what stops it reaching the third.
;
; It used to go to the local application data folder, where the script caches its own download.
; That worked, but it is a per-user folder written by an elevated installer, so it landed in
; whichever account elevated rather than the one that plays the game, and Inno warns about
; exactly that. Here it travels with the installation instead, and every account finds it.
Source: "{#FFmpegSrc}\ffmpeg.exe"; DestDir: "{app}\mods\fmv"; \
    Components: patch\fmv_player; Flags: ignoreversion

; Beside the binary it covers, next to the licence text libVLC ships with.
Source: "{#FFmpegSrc}\ffmpeg-License.txt"; DestDir: "{app}\mods\fmv"; \
    Components: patch\fmv_player; Flags: ignoreversion
; VideoLAN's zip unpacks to about 177 MB; what follows is the 23.4 MB of it that playing one H.264
; and AAC file into a window needs, and dist\vlc holds exactly those. All 33 paths have to be read
; out of the zip again when VlcVersion changes.
;
; No row here carries a flag that forgives a missing source. An incomplete plugin set does not fail
; loudly, it fails as libvlc_new refusing, and the player reads that as the feature not working.
Source: "{#VlcSrc}\libvlc.dll"; DestDir: "{app}\mods\fmv"; \
    Components: patch\fmv_player\runtime; Flags: ignoreversion
Source: "{#VlcSrc}\libvlccore.dll"; DestDir: "{app}\mods\fmv"; \
    Components: patch\fmv_player\runtime; Flags: ignoreversion

; LGPL, so the licence travels with the binaries.
Source: "{#VlcSrc}\COPYING.txt"; DestDir: "{app}\mods\fmv"; DestName: "vlc-License.txt"; \
    Components: patch\fmv_player\runtime; Flags: ignoreversion

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
