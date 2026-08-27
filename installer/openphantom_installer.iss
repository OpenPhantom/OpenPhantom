; The retail game from the player's own disc.
;
; The disc: finding it, reading it, expanding its archive, the registry entries and the shortcuts.
; Everything optional is in the includes at the bottom.
;
; No game data is compiled in; the game comes off the player's own disc. Everything else this
; installs is carried inside it, in dist\, and nothing is fetched while it runs or afterwards. That
; includes FFmpeg, which the cutscene converter would otherwise download on first use.
;
; Executables carried here: the archive extractor built from src\is3_extract\, FFmpeg for the
; cutscene converter, and Microsoft's Visual C++ redistributable, which is run only when the
; runtime it installs is genuinely absent.
;
; The disc is copied with Flags: external rather than by a helper process, which is what lets Inno
; record every file it wrote. The uninstaller then removes exactly those and leaves the rest, so it
; never has to delete the whole directory with the saved games in it.

#define GameName "Star Wars Episode I: The Phantom Menace"

; The extractor that turns the disc's GAMEDATA\GOBS\BIG.Z into big.lab. Built from src\is3_extract\.
#define ExtractorExe "src\is3_extract\build\Release\is3_extract.exe"

; Written once because every row in [Registry] needs it verbatim. A typo in one of nine copies makes
; a second key the game never looks in, and nothing reports that.
#define GameKey "SOFTWARE\LucasArts Entertainment Company LLC\The Phantom Menace\v1.0"

[Setup]
; Never change AppId. It is what tells Windows that a later build is an upgrade of this program
; rather than a second copy of it.
AppId={{7C5AF842-8C19-4B17-CCA0-9AE3A49BD10E}
AppName={#GameName}
AppVersion=1.0

; AppName is the game, so these are the only place the installer says where it came from. All three
; links go to the project rather than to a personal page.
AppPublisher=J0nny and Chip-Biscuit
AppPublisherURL=https://github.com/OpenPhantom/OpenPhantom
AppSupportURL=https://github.com/OpenPhantom/OpenPhantom/issues
AppUpdatesURL=https://github.com/OpenPhantom/OpenPhantom/releases
DefaultDirName={commonpf32}\LucasArts\The Phantom Menace

; Always ask where to install, even on a reinstall.
;
; The default is auto, which hides the page whenever Setup finds a previous installation of this
; AppId and silently reuses the folder recorded in its uninstall key. That is the wrong default
; here for the same reason UsePreviousSetupType is: somebody rerunning this to update a patch gets
; no say in where it lands, and no indication that a folder was chosen for them.
;
; UsePreviousAppDir is left alone, so the page still opens on the previous folder. It is offered
; rather than imposed.
DisableDirPage=no
DefaultGroupName=LucasArts\The Phantom Menace
OutputBaseFilename=OpenPhantom_Installer

; The version resource of the produced installer. Without this Inno stamps its own compiler version
; on it, so the file properties would name Inno's release rather than ours.
VersionInfoVersion=1.0
; Built installers are kept out of the repository; they are rebuilt from this script.
OutputDir=output
UninstallDisplayIcon={app}\WMAIN.EXE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin

; Reinstalling starts from the first type again rather than from what was chosen last time.
;
; The default is yes, and it restores both the type and the ticked components out of the registry.
; That is the safer behaviour in general and it is being given up deliberately: this installer is
; how the patch is updated, so the same person runs it again after components have been added, and
; a restored selection silently leaves every new one unticked. Starting from "everything" each time
; is what makes an update install the whole patch.
;
; The cost is that somebody who deliberately took less than everything is offered everything again,
; including the cutscene player and its converter. They are still on the
; Back button, and nothing installs before the ready page.
UsePreviousSetupType=no

; The installer stays in 32-bit mode. Windows then redirects its HKLM\SOFTWARE writes into
; WOW6432Node by itself, which is where the 32-bit game looks for them.

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "german";  MessagesFile: "compiler:Languages\German.isl"

; Inno's own welcome text names only the program. This is the one page everybody sees.
[Messages]
english.WelcomeLabel1=The OpenPhantom installer
english.WelcomeLabel2=This installs Star Wars Episode I: The Phantom Menace from your own disc, and then the OpenPhantom patches that make it run properly on a current machine.%n%nBy J0nny and Chip-Biscuit.
german.WelcomeLabel1=Das OpenPhantom Installationsprogramm
german.WelcomeLabel2=Es installiert Star Wars Episode I: Die dunkle Bedrohung von Ihrer eigenen CD und danach die OpenPhantom-Patches, mit denen es auf einem heutigen Rechner sauber läuft.%n%nVon J0nny und Chip-Biscuit.

[Types]
; Listed first, so it is the preselected one. Every component row carries "everything", so
; this type is the one that leaves nothing unticked.
Name: "everything"; Description: "{cm:TypeEverything}"
Name: "full";   Description: "{cm:TypeFull}"
Name: "base";   Description: "{cm:TypeBase}"
Name: "custom"; Description: "{cm:TypeCustom}"; Flags: iscustom

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Dirs]
; The game writes its settings and saves inside its own folder and neither path is configurable, so
; in Program Files it cannot save at all unless these two are writable.
Name: "{app}";       Permissions: users-modify
Name: "{app}\Save";  Permissions: users-modify

[Files]
; dontcopy keeps this out of the installation: PrepareToInstall unpacks it into the temporary folder,
; runs it once, and it is gone.
Source: "{#ExtractorExe}"; Flags: dontcopy

; PrepareToInstall has already produced this from the disc, so by now it exists and Inno copies and
; records it like any other file.
Source: "{tmp}\big.lab"; DestDir: "{app}"; Flags: external ignoreversion

; Everything below is read off the disc at install time. The wizard has already checked that the five
; landmarks of a genuine disc are there.

; The launcher, at the root of the disc.
Source: "{code:DiscPath}\TPM.EXE"; DestDir: "{app}"; Flags: external ignoreversion

; The program itself and the audio and video runtimes beside it. They go into the root of the
; installation and not into a BIN folder, because that is where the shortcuts and the registry
; entry below expect WMAIN.EXE to be.
Source: "{code:DiscPath}\GAMEDATA\BIN\*"; DestDir: "{app}"; \
    Flags: external ignoreversion recursesubdirs createallsubdirs

; Cutscene audio and music, which the game reads from these two folder names.
Source: "{code:DiscPath}\GAMEDATA\JCG\*"; DestDir: "{app}\JCG"; \
    Flags: external ignoreversion recursesubdirs createallsubdirs
Source: "{code:DiscPath}\GAMEDATA\MUSIC\*"; DestDir: "{app}\MUSIC"; \
    Flags: external ignoreversion recursesubdirs createallsubdirs

; The localisation archive, at the root rather than in GOBS. VOICE.LAB is not on every pressing --
; some releases keep the spoken lines inside big.lab instead, so it is the one file here allowed
; to be absent, and this is the only place in the tree where that flag is correct.
Source: "{code:DiscPath}\GAMEDATA\GOBS\LOCALIZE.LAB"; DestDir: "{app}"; \
    Flags: external ignoreversion
Source: "{code:DiscPath}\GAMEDATA\GOBS\VOICE.LAB"; DestDir: "{app}"; \
    Flags: external ignoreversion skipifsourcedoesntexist

; The whole data tree a second time, under its own name. This is what lets the game run without
; the disc in the drive: the registry entry below points its CD path at the installation folder,
; and the game then finds GAMEDATA where it expects to find it on the disc.
;
; Two files are held back, and together they are about 747 MB of the disc:
;   BIG.Z       is 711 MB on the pressing although its archive is only the first 81 MB of that --
;               the rest is filler that exists to shape the disc, and the useful part has already
;               been expanded into big.lab.
;   MENACE.DAT  begins with byte-identical content to BIG.Z but stops short of the archive's end,
;               so it cannot be unpacked and nothing reads it.
Source: "{code:DiscPath}\GAMEDATA\*"; DestDir: "{app}\GAMEDATA"; \
    Excludes: "\GOBS\BIG.Z,\GOBS\MENACE.DAT"; \
    Flags: external ignoreversion recursesubdirs createallsubdirs

; ---- support files from the disc's INSTALL folder ----------------------------------------------
; Named one by one rather than copied wholesale, because that folder also holds the original
; 16-bit setup program, which must not end up in the installation. Which of them a given pressing
; carries varies, so each may be absent.
Source: "{code:DiscPath}\INSTALL\DSETUP.DLL";   DestDir: "{app}"; Flags: external ignoreversion skipifsourcedoesntexist
Source: "{code:DiscPath}\INSTALL\DSETUP16.DLL"; DestDir: "{app}"; Flags: external ignoreversion skipifsourcedoesntexist
Source: "{code:DiscPath}\INSTALL\DSETUP32.DLL"; DestDir: "{app}"; Flags: external ignoreversion skipifsourcedoesntexist
Source: "{code:DiscPath}\INSTALL\INSTDX50.EXE"; DestDir: "{app}"; Flags: external ignoreversion skipifsourcedoesntexist
Source: "{code:DiscPath}\INSTALL\LAUNCHER.INI"; DestDir: "{app}"; Flags: external ignoreversion skipifsourcedoesntexist
Source: "{code:DiscPath}\INSTALL\LICENSE.TXT";  DestDir: "{app}"; Flags: external ignoreversion skipifsourcedoesntexist
Source: "{code:DiscPath}\INSTALL\README.RTF";   DestDir: "{app}"; Flags: external ignoreversion skipifsourcedoesntexist
Source: "{code:DiscPath}\INSTALL\README.TXT";   DestDir: "{app}"; Flags: external ignoreversion skipifsourcedoesntexist isreadme
Source: "{code:DiscPath}\INSTALL\REGISTER.EXE"; DestDir: "{app}"; Flags: external ignoreversion skipifsourcedoesntexist
Source: "{code:DiscPath}\INSTALL\SYSCHECK.EXE"; DestDir: "{app}"; Flags: external ignoreversion skipifsourcedoesntexist
Source: "{code:DiscPath}\INSTALL\SYSCHECK.INI"; DestDir: "{app}"; Flags: external ignoreversion skipifsourcedoesntexist
Source: "{code:DiscPath}\INSTALL\WEBSITE.EXE";  DestDir: "{app}"; Flags: external ignoreversion skipifsourcedoesntexist

[Registry]
; What the game itself reads. The installer runs in 32-bit mode, so these land under WOW6432Node
; without the key path having to say so.
;
; "CD Path" is the one that matters: it is where the game looks for GAMEDATA, and pointing it at
; the installation folder is what removes the need for the disc in the drive.
;
; uninsdeletekey sits on the first row only. It removes the whole key, so a second one would not
; make it any more removed; putting it on the row that creates the key is what keeps that readable.
Root: HKLM; Subkey: "{#GameKey}"; \
    ValueType: string; ValueName: "Install Path"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "{#GameKey}"; \
    ValueType: string; ValueName: "CD Path";      ValueData: "{app}"
Root: HKLM; Subkey: "{#GameKey}"; \
    ValueType: string; ValueName: "Source Path";  ValueData: ".\"
Root: HKLM; Subkey: "{#GameKey}"; \
    ValueType: string; ValueName: "Source Dir";   ValueData: ".\"
Root: HKLM; Subkey: "{#GameKey}"; \
    ValueType: string; ValueName: "Executable";   ValueData: "{app}\WMAIN.EXE"
Root: HKLM; Subkey: "{#GameKey}"; \
    ValueType: string; ValueName: "Analyze Path"; ValueData: "{app}\SYSCHECK.EXE"
; A full installation, in the original installer's own numbering.
Root: HKLM; Subkey: "{#GameKey}"; \
    ValueType: dword;  ValueName: "InstallType";  ValueData: $00000001
Root: HKLM; Subkey: "{#GameKey}"; \
    ValueType: string; ValueName: "JoystickID";   ValueData: "1"
Root: HKLM; Subkey: "{#GameKey}"; \
    ValueType: string; ValueName: "SoundCard";    ValueData: "TRUE"

; So that the era's launchers and shortcuts can find TPM.EXE by name alone.
Root: HKLM; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\TPM.EXE"; \
    ValueType: string; ValueName: "";     ValueData: "{app}\TPM.EXE"; Flags: uninsdeletekey
Root: HKLM; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\TPM.EXE"; \
    ValueType: string; ValueName: "Path"; ValueData: "{app}"

; There is deliberately no HIGHDPIAWARE compatibility flag here, although the installer this
; replaces set one. Two reasons, and the first is decisive: the flag lives in a per-user key, this
; installer needs administrator rights, and when Windows elevates by asking for another account's
; credentials the flag is written into THAT account's registry, so it takes effect for a user who
; will never play the game. The compiler warns about exactly this. The second reason is that the
; setting changes the coordinate space a window is placed in, which is the same thing the
; resolution fix works in, and the two have never been tried together.

[Icons]
Name: "{group}\{#GameName}"; Filename: "{app}\WMAIN.EXE"; WorkingDir: "{app}"
Name: "{group}\{cm:UninstallProgram,{#GameName}}"; Filename: "{uninstallexe}"
Name: "{commondesktop}\{#GameName}"; Filename: "{app}\WMAIN.EXE"; WorkingDir: "{app}"; \
    Tasks: desktopicon

; There is deliberately no [UninstallDelete] section. Inno removes the files it installed; saved
; games, settings and anything a player added afterwards are not ours to delete. The folder stays
; behind with those in it, and that is the intended outcome.

; Nothing is downloaded during installation, so there is no download hash to check. Everything the
; installer places is compiled into it out of dist and is covered by Setup's own integrity check.

; One include per subject. DSOAL hangs off the patch component, so it comes before the saves,
; which does not, and the patch keeps one unbroken subtree in the component list.
#include "src\openphantom_patch.iss"
#include "src\dsoal.iss"
#include "src\complete_saves.iss"
#include "src\game_defaults.iss"

[CustomMessages]
english.DiscPageCaption=Find your game disc
english.DiscPageDescription=This installer contains no game data
english.DiscPageText=You need your own disc of Star Wars Episode I: The Phantom Menace, or an image of it.%n%nPick the top folder of the disc. That is the one holding TPM.EXE and GAMEDATA.
english.DiscPageLabel=Disc or mounted image:
english.NotADisc=This folder is not the game disc.%n%nThese are missing:%n%n%1%nPick the top folder of the disc, not a folder inside it.
english.TypeEverything=Full installation, everything
english.TypeFull=The patched game, without the extras
english.TypeBase=The original game only, without patches
english.TypeCustom=Choose what to install
english.CompPatch=The following OpenPhantom patches are installed
english.CompWrapper=Translates the old DirectDraw 7 graphics to Direct3D 9
english.CompCrtCopy=Fixes a copy loop bug
english.CompGroundClip=Stops characters being pushed through the floor
english.CompResolution=Patch for more native and higher resolutions in the game
english.CompFramerate=Frees the frame rate from 30, with camera, animation and effects adjusted for it
english.CompFov=Adjustable field of view, with a slider in the video options
english.CompHud=Display and text are no longer stretched on wide screens
english.CompInput=New input options, for example free look and sideways movement
english.CompController=Controller support: right stick looks, Start pauses, the triggers roll
english.CompDialogueAnim=Stops a talking character's head animation carrying into the next line
english.CompAudio=Audio bugfix
english.CompSfxVolume=Fixes the audio settings not being saved correctly
english.CompDecal=Brings back shadows, scorch marks and footprints
english.CompRenderGuard=Extra memory protection while rendering
english.CompLargeTex=Allows larger textures (only matters for later mods)
english.CompDevOverlay=New developer panel with the cheats
english.CompFmvPlayer=Plays the cutscenes as MP4 and installs the tools for it
english.CompFmvRuntime=The player needed for that. Carried in this installer
english.CompViewDist=Fog now hides the draw distance (nicer at a higher FOV)
english.CompDismember=Lightsaber dismemberment (mod)
english.CompCrashRep=Writes a crash report
english.CompDiag=Logs for fault finding. Everything off until you switch it on
english.CompDsoal=Restores the 3D sound audio option
english.DsoundTaken=A different dsound.dll is already in the game folder and could not be moved aside:%n%n      %1%n%nNothing was installed. Sound support needs that exact name, and this installer never deletes a file it did not put there.%n%nUsually the game is still running. Close it and start again, or go back and untick sound support.
english.OldWrapperFailed=An earlier version of this installer put the controller wrapper here, and it could not be moved aside:%n%n      %1%n%nEverything is installed and the game runs. On most machines that old file is never loaded and nothing is wrong.%n%nUsually the game is still running. If your controller behaves oddly, close the game and rename that file yourself.
english.CompSaves=Saved games, one at the start of each chapter
english.CompGameDefaults=Starting settings: a PlayStation-style controller layout and 1920x1080. Overwrites your own bindings if you have any
english.SavesFailed=Not all of the saved games could be copied into:%n%n      %1%n%nEverything else is installed and the game runs. Your own saved games were not touched.%n%nUsually the folder is write protected or the game is still running.
english.ReinstallCaption=The game is already installed here
english.ReinstallDescription=What should happen to it?
english.ReinstallText=There is already an installation in:%n%n      %1%n%nIt may hold patches from elsewhere. Those often use the same file names as ours. Then ours are installed but never run.%n%nA fresh folder is the only one where you know what is in it. Your saved games and settings are kept either way.
english.ReinstallClean=Delete it first, then install fresh (recommended)
english.ReinstallOver=Install over it and keep what is already there
english.ReinstallElsewhere=Install into a different folder instead
english.ReinstallBrowse=Choose the folder to install into
english.RemoveFailed=The installation in %1 could not be deleted.%n%nNothing was installed and your saved games are untouched.%n%nUsually the game is still running. Close it and start again.
english.ConfirmClean=Everything in this folder will be deleted:%n%n      %1%n%nKept and put back afterwards: your saved games, your settings in obi.ini, your converted cutscenes, and your current ini files under the ending .previous.%n%nEverything else is gone, including files other patches left there. This cannot be undone.%n%nDelete the folder and install fresh?
english.WrapperFound=The game folder already has a different graphics wrapper:%n%n%1%nTwo wrappers cannot work side by side. They use the same file names, so the game reaches only one of them and the other is never asked. Nothing reports this. You simply get settings that do nothing.%n%nDelete the installation first and install fresh? Your saved games, settings and converted cutscenes are kept.%n%nIf you choose No, nothing changes and our graphics wrapper cannot be installed. Untick it on the next page to keep yours.
english.WrapperBlocks=Our graphics wrapper cannot be installed over the one that is already there:%n%n%1%nReplacing it would break a working setup that is not ours. Nothing was installed.%n%nGo back one page and untick the graphics wrapper. Or close Setup, move that file away yourself, and start again.%n%nEverything else in the patch installs either way.
english.PreserveFailed=Your current ini files could not be backed up in:%n%n      %1%n%nNothing was installed. This version replaces them with its own and keeps yours under the ending .previous, so it stops rather than overwrite a file it could not back up first.%n%nUsually the folder is write protected or the game is still running.
english.MoviePageCaption=Cutscene quality
english.MoviePageDescription=How the cutscenes should be converted
english.MoviePageText=The cutscenes are stored in a video format from 1999. They are converted once, now, into a modern one that plays smoothly today.%n%nThe original size is recommended. The films never had more detail than that, so a larger size mostly costs disc space and shows you nothing extra.%n%nConverting takes a few minutes and needs no internet connection.
english.MovieOptOriginal=Keep the original size (recommended, smallest files)
english.MovieOpt1080=Enlarge to 1080p
english.MovieOpt1440=Enlarge to 1440p
english.MovieOpt2160=Enlarge to 4K (much larger files, no more detail)
english.MovieOptLater=Not now. I will run tools\Convert Movies.bat myself later
english.ScalingPageCaption=Cutscene shape
english.ScalingPageDescription=The films are almost square, your screen is not
english.ScalingPageText=One of the two has to give.%n%nYou can change this whenever you like. It is read while a film plays, so it also applies to films you converted earlier.%n%nYou find it later in engine_fixes.ini under [fmv_player] Scaling.
english.ScalingLetterbox=Keep the original shape, with black bars at the sides (recommended)
english.ScalingStretch=Fill the whole screen. Nothing is cut off, faces get a little wider
english.FpsPageCaption=Frame rate
english.FpsPageDescription=How many frames per second the game should draw
english.FpsPageText=Without a limit this engine draws many hundreds of frames per second. Your screen cannot show them, and it costs you a fully loaded processor core and a loud fan.%n%nA limit a little above your screen refresh rate looks just as smooth.%n%nYou find it later in engine_fixes.ini under [framerate_fix] TargetFps.
english.Fps100=100 frames per second (recommended)
english.FpsUnlimited=No limit, as fast as the machine manages
english.FpsOwn=Own limit
english.FpsBadValue=That is not a valid frame rate.%n%nEnter a whole number from 0 to 1000. 0 means no limit.
english.SettingsFailed=These settings could not be written into engine_fixes.ini:%n%n%1%nEverything is installed and the game runs. Every setting has a sensible default, so nothing is broken.%n%nYou can set them yourself in engine_fixes.ini next to WMAIN.EXE.
english.ConvertPageCaption=Converting the cutscenes
english.ConvertPageDescription=This happens once. After that the game plays the converted films.
english.ConvertPreparing=Getting ready...
english.ConvertWorking=Converting the cutscenes...
english.ConvertDone=The cutscenes were converted.%n%n%1%n%nNothing else is needed. The game uses them from now on.
english.ConvertKept=The cutscenes were left as they were.%n%n%1%n%nConverted films were already in the game folder, so nothing was encoded and the size you chose was not applied to them.%n%nTo redo them at that size, delete the movies_hd folder beside the game and run tools\Convert Movies.bat.
english.ConvertNoFilms=There were no cutscenes to convert.%n%n%1%n%nNo .BIK files were found beside the game, so nothing was encoded.%n%nThe game is installed and runs. If you expected films, check that GAMEDATA\MOVIE has them.
english.ConvertPartly=Not all cutscenes were converted.%n%n%1%n%nThe game is installed and runs. Films that were not converted play as before.%n%nYou can try again any time with tools\Convert Movies.bat in the game folder.
english.ConvertFailed=The cutscenes could not be converted. The converter stopped with code %1.%n%nThe game is installed and runs, and all films play as before.%n%nYou can try again any time with tools\Convert Movies.bat in the game folder.
english.ConvertNoScript=The conversion tool was not found:%n%n      %1%n%nThe game is installed and runs, and the films play as before.
english.RestoreFailed=Your saved games and settings were backed up, but not all of them could be copied back.%n%nThey are still here, and they are deleted when this window closes:%n%n      %1%n%nCopy the contents of that folder into the game folder before you close Setup.
english.CarryOverFailed=Your saved games, settings and converted films could not all be backed up to:%n%n      %1%n%nNothing was installed and nothing was deleted. Your installation is exactly as it was.%n%nUsually the drive with the temporary folder is full. Converted films are video and need space.%n%nFree some space, or choose to install over the existing files instead.
english.DinputTaken=A different dinput.dll is already in the game folder, and the name dinput_orig.dll is taken as well:%n%n      %1%n%nThe patch needs the first name and keeps the old file working under the second. Here it would have to delete one of the two.%n%nNothing was installed. Rename or move one of them, then start again.
english.DiscFound=The game disc was found. Click Next to install from it.
english.Expanding=Unpacking the game archive from the disc. That is about 80 MB and can take a minute from a disc drive.
english.ExpandFailed=The game archive GAMEDATA\GOBS\BIG.Z on the disc could not be unpacked. The unpacker stopped with code %1.%n%nNothing was installed. Usually the disc is scratched or unreadable.%n%nDetails:%n%2

german.DiscPageCaption=Spiel-CD suchen
german.DiscPageDescription=Dieses Installationsprogramm enthält keine Spieldaten
german.DiscPageText=Sie brauchen Ihre eigene CD von Star Wars Episode I: Die dunkle Bedrohung oder ein Abbild davon.%n%nWählen Sie das oberste Verzeichnis der CD. Das ist das mit TPM.EXE und GAMEDATA darin.
german.DiscPageLabel=CD oder eingebundenes Abbild:
german.NotADisc=Dieses Verzeichnis ist nicht die Spiel-CD.%n%nDas fehlt:%n%n%1%nWählen Sie das oberste Verzeichnis der CD, nicht eines darin.
german.TypeEverything=Vollständige Installation, alles
german.TypeFull=Das gepatchte Spiel, ohne die Extras
german.TypeBase=Nur das Originalspiel, ohne Patches
german.TypeCustom=Auswählen, was installiert wird
german.CompPatch=Folgende OpenPhantom-Patches werden installiert
german.CompWrapper=Übersetzt die alte DirectDraw-7-Grafik nach Direct3D 9
german.CompCrtCopy=Behebt einen Kopierschleifen-Bug
german.CompGroundClip=Verhindert, dass Charaktere durch den Boden gedrückt werden
german.CompResolution=Patch für mehr native und höhere Auflösungen im Spiel
german.CompFramerate=Löst die Bildrate von 30, mit angepasster Kamera, Animation und Effekten
german.CompFov=Einstellbares Sichtfeld, mit einem Regler in den Videooptionen
german.CompHud=Anzeige und Schrift werden auf breiten Bildschirmen nicht mehr verzerrt
german.CompInput=Neue Eingabeoptionen, z. B. freie Sicht und Seitwärtsbewegungen
german.CompController=Controller-Unterstützung: rechter Stick blickt, Start pausiert, die Trigger rollen
german.CompDialogueAnim=Beendet die Sprechanimation einer Figur, sobald die nächste Zeile beginnt
german.CompAudio=Audio-Bugfix
german.CompSfxVolume=Bugfix für die korrekte Speicherung der Audio-Einstellungen
german.CompDecal=Holt Schatten, Brandflecken und Fußspuren zurück
german.CompRenderGuard=Zusätzlicher Speicherschutz beim Rendern
german.CompLargeTex=Erlaubt größere Texturen (nur für spätere Mods wichtig)
german.CompDevOverlay=Neues Entwicklerfenster mit den Cheats
german.CompFmvPlayer=Spielt die Zwischensequenzen als MP4 und installiert die Werkzeuge dafür
german.CompFmvRuntime=Der dafür nötige Abspieler. In diesem Installationsprogramm enthalten
german.CompViewDist=Nebel verdeckt jetzt die Draw-Distance (bei höherem FOV schöner)
german.CompDismember=Lichtschwert-Amputation (Mod)
german.CompCrashRep=Schreibt einen Absturzbericht
german.CompDiag=Protokolle zur Fehlersuche. Alles aus, bis Sie es einschalten
german.CompDsoal=Wiederherstellung der 3D-Klang-Audio-Option
german.DsoundTaken=Im Spielverzeichnis liegt bereits eine fremde dsound.dll, die nicht beiseitegelegt werden konnte:%n%n      %1%n%nEs wurde nichts installiert. Die Klang-Unterstützung braucht genau diesen Namen, und dieses Installationsprogramm löscht keine Datei, die es nicht selbst angelegt hat.%n%nMeist läuft das Spiel noch. Beenden Sie es und starten Sie erneut, oder gehen Sie zurück und wählen Sie die Klang-Unterstützung ab.
german.OldWrapperFailed=Eine frühere Fassung dieses Installationsprogramms hat den Controller-Wrapper hier abgelegt, und er konnte nicht beiseitegelegt werden:%n%n      %1%n%nAlles ist installiert und das Spiel läuft. Auf den meisten Rechnern wird diese alte Datei nie geladen und es ist nichts kaputt.%n%nMeist läuft das Spiel noch. Falls sich Ihr Controller seltsam verhält, beenden Sie das Spiel und benennen Sie die Datei selbst um.
german.CompSaves=Spielstände, je einer zu Beginn jedes Kapitels
german.CompGameDefaults=Startwerte: eine Controller-Belegung im PlayStation-Stil und 1920x1080. Überschreibt vorhandene eigene Belegungen
german.SavesFailed=Es konnten nicht alle Spielstände kopiert werden nach:%n%n      %1%n%nAlles Übrige ist installiert und das Spiel läuft. Ihre eigenen Spielstände wurden nicht angetastet.%n%nMeist ist das Verzeichnis schreibgeschützt oder das Spiel läuft noch.
german.ReinstallCaption=Das Spiel ist hier bereits installiert
german.ReinstallDescription=Was soll damit geschehen?
german.ReinstallText=In diesem Verzeichnis liegt bereits eine Installation:%n%n      %1%n%nDort können Patches aus anderer Quelle liegen. Die benutzen oft dieselben Dateinamen wie unsere. Dann werden unsere zwar installiert, laufen aber nie.%n%nNur bei einem frischen Verzeichnis wissen Sie, was darin ist. Ihre Spielstände und Einstellungen bleiben in beiden Fällen erhalten.
german.ReinstallClean=Zuerst löschen, dann frisch installieren (empfohlen)
german.ReinstallOver=Darüber installieren und alles Vorhandene stehen lassen
german.ReinstallElsewhere=Stattdessen in ein anderes Verzeichnis installieren
german.ReinstallBrowse=Wählen Sie das Verzeichnis, in das installiert werden soll
german.RemoveFailed=Die Installation in %1 konnte nicht gelöscht werden.%n%nEs wurde nichts installiert und Ihre Spielstände sind unberührt.%n%nMeist läuft das Spiel noch. Beenden Sie es und starten Sie erneut.
german.ConfirmClean=Der gesamte Inhalt dieses Verzeichnisses wird gelöscht:%n%n      %1%n%nAufbewahrt und danach zurückgelegt: Ihre Spielstände, Ihre Einstellungen in der obi.ini, Ihre umgewandelten Zwischensequenzen und Ihre jetzigen ini-Dateien mit der Endung .previous.%n%nAlles Übrige ist weg, auch was andere Patches dort abgelegt haben. Das lässt sich nicht rückgängig machen.%n%nVerzeichnis löschen und frisch installieren?
german.WrapperFound=Im Spielverzeichnis liegt bereits ein anderer Grafik-Wrapper:%n%n%1%nZwei Wrapper können nicht nebeneinander arbeiten. Sie benutzen dieselben Dateinamen, das Spiel erreicht also nur einen von beiden und der andere wird nie gefragt. Gemeldet wird das nirgends. Sie bekommen einfach Einstellungen, die nichts bewirken.%n%nDie Installation zuerst löschen und frisch installieren? Ihre Spielstände, Einstellungen und umgewandelten Filme bleiben erhalten.%n%nBei Nein bleibt alles wie es ist, und unser Grafik-Wrapper kann nicht installiert werden. Wählen Sie ihn auf der nächsten Seite ab, um Ihren zu behalten.
german.WrapperBlocks=Unser Grafik-Wrapper kann nicht über den vorhandenen installiert werden:%n%n%1%nIhn zu ersetzen würde eine funktionierende Einrichtung zerstören, die nicht uns gehört. Es wurde nichts installiert.%n%nGehen Sie eine Seite zurück und wählen Sie den Grafik-Wrapper ab. Oder beenden Sie die Installation, schieben Sie die Datei selbst weg und starten Sie erneut.%n%nAlles Übrige des Patches wird in beiden Fällen installiert.
german.PreserveFailed=Ihre jetzigen ini-Dateien konnten nicht gesichert werden in:%n%n      %1%n%nEs wurde nichts installiert. Diese Fassung ersetzt sie durch ihre eigenen und legt Ihre mit der Endung .previous daneben. Deshalb bricht sie ab, statt eine Datei zu überschreiben, die sie vorher nicht sichern konnte.%n%nMeist ist das Verzeichnis schreibgeschützt oder das Spiel läuft noch.
german.MoviePageCaption=Qualität der Zwischensequenzen
german.MoviePageDescription=Wie die Zwischensequenzen umgewandelt werden sollen
german.MoviePageText=Die Zwischensequenzen liegen in einem Videoformat von 1999 vor. Sie werden jetzt einmalig in ein modernes umgewandelt, das heute flüssig läuft.%n%nEmpfohlen ist die Originalgröße. Mehr Details als diese hatten die Filme nie. Eine größere Auflösung kostet also vor allem Speicherplatz und zeigt Ihnen nichts Zusätzliches.%n%nDas Umwandeln dauert einige Minuten und braucht keine Internetverbindung.
german.MovieOptOriginal=Originalgröße behalten (empfohlen, kleinste Dateien)
german.MovieOpt1080=Auf 1080p vergrößern
german.MovieOpt1440=Auf 1440p vergrößern
german.MovieOpt2160=Auf 4K vergrößern (deutlich größere Dateien, nicht mehr Details)
german.MovieOptLater=Jetzt nicht. Ich starte tools\Convert Movies.bat später selbst
german.ScalingPageCaption=Form der Zwischensequenzen
german.ScalingPageDescription=Die Filme sind fast quadratisch, Ihr Bildschirm ist es nicht
german.ScalingPageText=Eines von beiden muss nachgeben.%n%nSie können das jederzeit ändern. Es wird beim Abspielen gelesen und gilt damit auch für Filme, die Sie schon umgewandelt haben.%n%nSpäter finden Sie es in der engine_fixes.ini unter [fmv_player] Scaling.
german.ScalingLetterbox=Ursprüngliche Form behalten, mit schwarzen Balken an den Seiten (empfohlen)
german.ScalingStretch=Den ganzen Bildschirm füllen. Es wird nichts abgeschnitten, Gesichter werden etwas breiter
german.FpsPageCaption=Bildrate
german.FpsPageDescription=Wie viele Bilder pro Sekunde das Spiel zeichnen soll
german.FpsPageText=Ohne Begrenzung zeichnet diese Engine viele Hundert Bilder pro Sekunde. Ihr Bildschirm kann sie nicht zeigen, und es kostet Sie einen voll ausgelasteten Prozessorkern und einen lauten Lüfter.%n%nEine Grenze etwas über der Bildwiederholrate Ihres Bildschirms sieht genauso flüssig aus.%n%nSpäter finden Sie es in der engine_fixes.ini unter [framerate_fix] TargetFps.
german.Fps100=100 Bilder pro Sekunde (empfohlen)
german.FpsUnlimited=Keine Begrenzung, so schnell der Rechner es schafft
german.FpsOwn=Eigene Begrenzung
german.FpsBadValue=Das ist keine gültige Bildrate.%n%nGeben Sie eine ganze Zahl von 0 bis 1000 ein. 0 bedeutet keine Begrenzung.
german.SettingsFailed=Diese Einstellungen konnten nicht in die engine_fixes.ini geschrieben werden:%n%n%1%nAlles ist installiert und das Spiel läuft. Jede Einstellung hat einen sinnvollen Standardwert, es ist also nichts kaputt.%n%nSie können sie selbst in der engine_fixes.ini neben der WMAIN.EXE setzen.
german.ConvertPageCaption=Zwischensequenzen werden umgewandelt
german.ConvertPageDescription=Das geschieht einmalig. Danach spielt das Spiel die umgewandelten Filme.
german.ConvertPreparing=Vorbereitung...
german.ConvertWorking=Zwischensequenzen werden umgewandelt...
german.ConvertDone=Die Zwischensequenzen wurden umgewandelt.%n%n%1%n%nMehr ist nicht nötig. Das Spiel verwendet sie ab sofort.
german.ConvertKept=Die Zwischensequenzen wurden unverändert gelassen.%n%n%1%n%nIm Spielverzeichnis lagen bereits umgewandelte Filme, es wurde also nichts kodiert und die gewählte Größe wurde nicht auf sie angewendet.%n%nUm sie in dieser Größe neu zu erzeugen, löschen Sie den Ordner movies_hd neben dem Spiel und führen Sie tools\Convert Movies.bat aus.
german.ConvertNoFilms=Es gab keine Zwischensequenzen zum Umwandeln.%n%n%1%n%nNeben dem Spiel wurden keine .BIK-Dateien gefunden, es wurde also nichts kodiert.%n%nDas Spiel ist installiert und läuft. Falls Sie Filme erwartet haben, prüfen Sie, ob GAMEDATA\MOVIE sie enthält.
german.ConvertPartly=Es wurden nicht alle Zwischensequenzen umgewandelt.%n%n%1%n%nDas Spiel ist installiert und läuft. Nicht umgewandelte Filme laufen wie bisher.%n%nSie können es jederzeit erneut versuchen mit tools\Convert Movies.bat im Spielverzeichnis.
german.ConvertFailed=Die Zwischensequenzen konnten nicht umgewandelt werden. Der Umwandler endete mit Code %1.%n%nDas Spiel ist installiert und läuft, alle Filme laufen wie bisher.%n%nSie können es jederzeit erneut versuchen mit tools\Convert Movies.bat im Spielverzeichnis.
german.ConvertNoScript=Das Umwandlungswerkzeug wurde nicht gefunden:%n%n      %1%n%nDas Spiel ist installiert und läuft, die Filme laufen wie bisher.
german.RestoreFailed=Ihre Spielstände und Einstellungen wurden gesichert, konnten aber nicht vollständig zurückkopiert werden.%n%nSie liegen noch hier und werden gelöscht, sobald dieses Fenster geschlossen wird:%n%n      %1%n%nKopieren Sie den Inhalt dieses Verzeichnisses ins Spielverzeichnis, bevor Sie die Installation beenden.
german.CarryOverFailed=Ihre Spielstände, Einstellungen und umgewandelten Filme konnten nicht vollständig gesichert werden nach:%n%n      %1%n%nEs wurde nichts installiert und nichts gelöscht. Ihre Installation ist genau wie vorher.%n%nMeist ist das Laufwerk mit dem temporären Verzeichnis voll. Umgewandelte Filme sind Videodateien und brauchen Platz.%n%nSchaffen Sie Platz, oder installieren Sie stattdessen über die vorhandenen Dateien.
german.DinputTaken=Im Spielverzeichnis liegt bereits eine fremde dinput.dll, und der Name dinput_orig.dll ist auch schon belegt:%n%n      %1%n%nDer Patch braucht den ersten Namen und hält die alte Datei unter dem zweiten am Leben. Hier müsste er eine der beiden löschen.%n%nEs wurde nichts installiert. Benennen Sie eine der beiden um oder verschieben Sie sie, dann starten Sie erneut.
german.DiscFound=Die Spiel-CD wurde gefunden. Mit Weiter wird von dort installiert.
german.Expanding=Das Spielarchiv wird von der CD entpackt. Das sind rund 80 MB und kann von einem CD-Laufwerk eine Minute dauern.
german.ExpandFailed=Das Spielarchiv GAMEDATA\GOBS\BIG.Z auf der CD konnte nicht entpackt werden. Das Entpackprogramm endete mit Code %1.%n%nEs wurde nichts installiert. Meist ist die CD zerkratzt oder unlesbar.%n%nEinzelheiten:%n%2

[Code]
{ Windows' own answer to "what kind of drive is this". Inno has no wrapper for it, and the W suffix
  is not optional: the script engine is Unicode and the ANSI entry point would be handed the wrong
  string. }
function GetDriveType(lpRootPathName: String): Cardinal;
  external 'GetDriveTypeW@kernel32.dll stdcall';

{ Inno can set attributes on a file it installs but has nothing that clears one, and clearing is what
  is needed here: every save in the downloaded archive carries the read-only flag, and the game has
  to write to those slots. Same W suffix, same reason. }
function SetFileAttributes(lpFileName: String; dwFileAttributes: Cardinal): Boolean;
  external 'SetFileAttributesW@kernel32.dll stdcall';

function SetEnvironmentVariable(lpName, lpValue: String): Boolean;
  external 'SetEnvironmentVariableW@kernel32.dll stdcall';

const
  DRIVE_REMOVABLE = 2;
  DRIVE_FIXED     = 3;
  DRIVE_CDROM     = 5;

var
  DiscPage: TInputDirWizardPage;
  ReinstallPage: TInputOptionWizardPage;
  RescuedFileCount: Integer;

  { MovieTotal is counted from the .bik files before starting, MovieDone comes from the converter's
    one line per movie, and MovieResult is its summary line so the closing message can say what
    happened rather than only that it stopped. }
  MoviePage: TInputOptionWizardPage;
  ConvertPage: TOutputProgressWizardPage;
  MovieTotal, MovieDone: Integer;
  MovieResult: String;

  { Both are written into engine_fixes.ini after it has been installed, because the row that installs
    it replaces the file. }
  ScalingPage: TInputOptionWizardPage;
  FpsPage: TInputOptionWizardPage;
  FpsEdit: TNewEdit;

{ Two answers in this file are lists of file names for a person to read, and empty means nothing to
  report. These three build them. }

{ One line of such a list. }
procedure AddName(const Name: String; var List: String);
begin
  List := List + Name + #13#10;
end;

{ Names the file when it IS there. Used for slots a stranger may be occupying. }
procedure AddIfPresent(const Base, Name: String; var List: String);
begin
  if FileExists(Base + Name) then
    AddName(Name, List);
end;

{ Names the file when it is NOT there. Used for things that have to be there. }
procedure AddIfMissing(const Base, Name: String; var List: String);
begin
  if not FileExists(Base + Name) then
    AddName(Name, List);
end;

{ The five landmarks of a genuine disc. BIG.Z is listed although nothing is copied from it: an
  installed game has the expanded big.lab and no BIG.Z, so this is what tells a disc from somebody
  else's installation.

  INSTALL is a folder, which is why that row is not AddIfMissing. }
function MissingDiscParts(const Root: String): String;
var
  Base: String;
begin
  Result := '';
  Base := AddBackslash(Root);

  AddIfMissing(Base, 'TPM.EXE',                   Result);
  AddIfMissing(Base, 'GAMEDATA\BIN\WMAIN.EXE',    Result);
  AddIfMissing(Base, 'GAMEDATA\GOBS\LOCALIZE.LAB', Result);
  AddIfMissing(Base, 'GAMEDATA\GOBS\BIG.Z',       Result);

  if not DirExists(Base + 'INSTALL') then
    AddName('INSTALL\', Result);
end;

{ One custom message with its single argument filled in. Thirteen of the fourteen messages in this
  file take exactly one, so that is the shape worth a name; the one that takes two is written out
  where it is used. }
function UserMessage(const Name, Argument: String): String;
begin
  Result := FmtMessage(ExpandConstant('{cm:' + Name + '}'), [Argument]);
end;

{ Used by every external Source above. Inno asks for it once while it works out how much has to be
  copied, and again for each file. }
function DiscPath(Param: String): String;
begin
  Result := RemoveBackslashUnlessRoot(DiscPage.Values[0]);
end;

{ The root of the first drive holding the game, or ''. Optical drives first, since a mounted image is
  one of those too, then fixed and removable ones but only at their root: searching whole drives takes
  minutes and this runs while the user waits.

  Network drives are skipped. A disconnected one blocks for seconds per probe. }
function FindGameDisc: String;
var
  Pass: Integer;
  Letter: Integer;
  Root: String;
  Kind: Cardinal;
begin
  Result := '';
  for Pass := 0 to 1 do begin
    for Letter := Ord('A') to Ord('Z') do begin
      Root := Chr(Letter) + ':\';
      Kind := GetDriveType(Root);

      { Pass 0 is DRIVE_CDROM, pass 1 is DRIVE_REMOVABLE and DRIVE_FIXED. }
      if ((Pass = 0) and (Kind = DRIVE_CDROM)) or
         ((Pass = 1) and ((Kind = DRIVE_REMOVABLE) or (Kind = DRIVE_FIXED))) then begin
        if MissingDiscParts(Root) = '' then begin
          Result := Root;
          Exit;
        end;
      end;
    end;
  end;
end;

{ Typing in the box ticks the option it belongs to. Without this a player types a number, leaves the
  option alone, and is quietly given the preselected 100 instead of what they typed.

  The other direction is not wired, and that is on purpose: it would depend on the list raising an
  event for a radio selection, and if it did not the box would sit disabled and the whole option
  would be unreachable. This way the box always works. }
procedure FpsEditChanged(Sender: TObject);
begin
  FpsPage.SelectedValueIndex := 2;
end;

procedure InitializeWizard;
var
  Found: String;
begin
  DiscPage := CreateInputDirPage(
    wpSelectDir,
    ExpandConstant('{cm:DiscPageCaption}'),
    ExpandConstant('{cm:DiscPageDescription}'),
    ExpandConstant('{cm:DiscPageText}'),
    False,
    '');
  DiscPage.Add(ExpandConstant('{cm:DiscPageLabel}'));

  { Only shown when the target folder already holds a game; see ShouldSkipPage. The first option
    is preselected because it is the one that produces a folder whose contents are all known. }
  ReinstallPage := CreateInputOptionPage(
    DiscPage.ID,
    ExpandConstant('{cm:ReinstallCaption}'),
    ExpandConstant('{cm:ReinstallDescription}'),
    '',
    True,
    False);
  ReinstallPage.Add(ExpandConstant('{cm:ReinstallClean}'));
  ReinstallPage.Add(ExpandConstant('{cm:ReinstallOver}'));
  ReinstallPage.Add(ExpandConstant('{cm:ReinstallElsewhere}'));
  ReinstallPage.SelectedValueIndex := 0;

  { The original size is preselected because upscaling a 1999 Bink source adds no detail and spends
    the space on compression artefacts. The larger sizes are offered anyway. }
  MoviePage := CreateInputOptionPage(
    wpSelectComponents,
    ExpandConstant('{cm:MoviePageCaption}'),
    ExpandConstant('{cm:MoviePageDescription}'),
    ExpandConstant('{cm:MoviePageText}'),
    True,
    False);
  MoviePage.Add(ExpandConstant('{cm:MovieOptOriginal}'));
  MoviePage.Add(ExpandConstant('{cm:MovieOpt1080}'));
  MoviePage.Add(ExpandConstant('{cm:MovieOpt1440}'));
  MoviePage.Add(ExpandConstant('{cm:MovieOpt2160}'));
  MoviePage.Add(ExpandConstant('{cm:MovieOptLater}'));
  MoviePage.SelectedValueIndex := 0;

  { Letterbox first and preselected, because it is the answer that shows the movie the shape it was
    made in. Stretch is offered rather than hidden: on a wide screen some people would rather have no
    bars than correct faces, and that is a taste rather than a mistake. }
  ScalingPage := CreateInputOptionPage(
    MoviePage.ID,
    ExpandConstant('{cm:ScalingPageCaption}'),
    ExpandConstant('{cm:ScalingPageDescription}'),
    ExpandConstant('{cm:ScalingPageText}'),
    True,
    False);
  ScalingPage.Add(ExpandConstant('{cm:ScalingLetterbox}'));
  ScalingPage.Add(ExpandConstant('{cm:ScalingStretch}'));
  ScalingPage.SelectedValueIndex := 0;

  { The frame rate cap. Four common answers and a box to type any other, because the useful values
    are not a list: a player with a 165 Hz screen wants 165 and nobody can enumerate that. }
  FpsPage := CreateInputOptionPage(
    ScalingPage.ID,
    ExpandConstant('{cm:FpsPageCaption}'),
    ExpandConstant('{cm:FpsPageDescription}'),
    ExpandConstant('{cm:FpsPageText}'),
    True,
    False);
  { The row height is set rather than measured, because the box below is placed on a row of this list
    and a guessed height would put it between two of them. }
  FpsPage.CheckListBox.MinItemHeight := ScaleY(18);

  FpsPage.Add(ExpandConstant('{cm:Fps100}'));
  FpsPage.Add(ExpandConstant('{cm:FpsUnlimited}'));
  FpsPage.Add(ExpandConstant('{cm:FpsOwn}'));
  FpsPage.SelectedValueIndex := 0;

  FpsPage.CheckListBox.Height := ScaleY(58);

  { On the third row and to the right of its label, so it reads as part of that option rather than as
    a separate question. The offset clears the longest label in either language with room to spare;
    the two are not measured against each other, so leave a gap when changing the text. }
  FpsEdit := TNewEdit.Create(FpsPage);
  FpsEdit.Parent := FpsPage.Surface;
  FpsEdit.Left := FpsPage.CheckListBox.Left + ScaleX(145);
  FpsEdit.Top := FpsPage.CheckListBox.Top + ScaleY(18) * 2 - ScaleY(2);
  FpsEdit.Width := ScaleX(56);
  FpsEdit.Text := '120';

  { Assigned after the text above, so filling in the default does not tick the option. }
  FpsEdit.OnChange := @FpsEditChanged;

  ConvertPage := CreateOutputProgressPage(
    ExpandConstant('{cm:ConvertPageCaption}'),
    ExpandConstant('{cm:ConvertPageDescription}'));

  Found := FindGameDisc;
  if Found <> '' then
    DiscPage.Values[0] := Found
  else
    { Nothing found: the folder the installer was started from is the next likeliest place, an
      installer copied onto the disc, or sitting beside a mounted image. }
    DiscPage.Values[0] := ExpandConstant('{src}');
end;

{ The disc is searched for again on the way in, because the commonest reason for not finding one at
  startup is that it had not been put in yet. Only a value that does not currently validate is
  replaced, so a path the user typed is never overwritten by a guess. }
{ A folder holding WMAIN.EXE is a game installation. That is the whole test, and it is deliberately
  not the registry: a folder left behind by an installer that no longer has a registry entry is
  still a folder we would install on top of. }
function GameIsInstalledAt(const Dir: String): Boolean;
begin
  Result := FileExists(AddBackslash(Dir) + 'WMAIN.EXE');
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if PageID = ReinstallPage.ID then
    Result := not GameIsInstalledAt(ExpandConstant('{app}'))
  else if PageID = MoviePage.ID then
    Result := not WizardIsComponentSelected('patch\fmv_player')
  else if PageID = ScalingPage.ID then
    { Asked whenever the player is installed, including when nothing is being converted now: the
      setting is read at playback, so it applies to files converted later or by hand. }
    Result := not WizardIsComponentSelected('patch\fmv_player')
  else if PageID = FpsPage.ID then
    Result := not WizardIsComponentSelected('patch\framerate_fix');
end;

function WantsCleanReinstall: Boolean;
begin
  Result := GameIsInstalledAt(ExpandConstant('{app}')) and (ReinstallPage.SelectedValueIndex = 0);
end;

{ True when the file contains Marker. }
function FileContainsMarker(const FileName, Marker: String): Boolean;
var
  Data: AnsiString;
begin
  Result := False;
  if LoadStringFromFile(FileName, Data) then
    Result := Pos(Marker, Data) > 0;
end;

{ Three libraries here answer to a name somebody else may also be using. Each is identified by a
  string its own binary carries and the library it displaces does not, checked against that other
  library in every case.

    dinput.dll    OpenPhantom     our loader
    ddraw.dll     dxwrapper       the graphics wrapper
    dsound.dll    DSOAL           the audio wrapper

  winmm.dll is deliberately not among them. A controller wrapper used to be installed under that
  name; this installer no longer installs a controller wrapper at all, because controller_input.dll,
  part of the patch, reads a pad directly and needs no wrapper. RetireOldControllerWrapper below
  still moves aside what a much older release left under that name, checked by its own literal
  marker string rather than through SlotMarker.

  Do not turn this round into a search for other projects' names: our own dxwrapper.dll contains the
  strings dgVoodoo and DDrawCompat because it recognises them at run time.

  An unknown slot gives an empty marker, which the caller reads as not ours. }
function SlotMarker(const SlotName: String): String;
begin
  if SlotName = 'dinput.dll' then
    Result := 'OpenPhantom'
  else if SlotName = 'ddraw.dll' then
    Result := 'dxwrapper'
  else if SlotName = 'dsound.dll' then
    Result := 'DSOAL'
  else
    Result := '';
end;

{ True when the file sitting in a contested slot is the one this installer puts there. }
function SlotHoldsOurFile(const Dir, SlotName: String): Boolean;
var
  Marker: String;
begin
  Marker := SlotMarker(SlotName);
  Result := (Marker <> '') and FileContainsMarker(AddBackslash(Dir) + SlotName, Marker);
end;

{ The files that mean a graphics wrapper other than ours is installed, one per line, or '' when the
  folder is clear.

  ddraw.dll is a slot we contest, so its content decides. The rest are names this installer never
  writes under any component, so for those the name is the evidence.

  Two wrappers in one folder compete for the same entry points and the loser is never asked, so the
  symptom is a setting with no effect and nothing reporting a conflict. }
function ForeignWrapperFiles(const Dir: String): String;
var
  Base: String;
begin
  Result := '';
  Base := AddBackslash(Dir);

  if FileExists(Base + 'ddraw.dll') and not SlotHoldsOurFile(Dir, 'ddraw.dll') then
    AddName('ddraw.dll', Result);

  AddIfPresent(Base, 'd3d8.dll',        Result);
  AddIfPresent(Base, 'd3d9.dll',        Result);
  AddIfPresent(Base, 'd3dimm.dll',      Result);
  AddIfPresent(Base, 'wined3d.dll',     Result);
  AddIfPresent(Base, 'dgVoodoo.conf',   Result);
  AddIfPresent(Base, 'dgVoodooCpl.exe', Result);
  AddIfPresent(Base, 'ddraw.ini',       Result);
end;

procedure CurPageChanged(CurPageID: Integer);
var
  Found: String;
begin
  if CurPageID = ReinstallPage.ID then begin
    { The folder is only known once the directory page has been passed, so the explanation naming
      it is filled in here rather than when the page was built. }
    ReinstallPage.SubCaptionLabel.Caption :=
      UserMessage('ReinstallText', ExpandConstant('{app}'));
    Exit;
  end;

  if CurPageID <> DiscPage.ID then
    Exit;

  if MissingDiscParts(DiscPage.Values[0]) <> '' then begin
    Found := FindGameDisc;
    if Found <> '' then
      DiscPage.Values[0] := Found;
  end;

  if MissingDiscParts(DiscPage.Values[0]) = '' then
    WizardForm.PageDescriptionLabel.Caption := ExpandConstant('{cm:DiscFound}')
  else
    WizardForm.PageDescriptionLabel.Caption := ExpandConstant('{cm:DiscPageDescription}');
end;

{ Leaving the disc page. Naming what is missing rather than saying the folder is wrong: on a disc
  that is merely the wrong pressing, that is the difference between a report and a shrug. }
function DiscPageAccepted: Boolean;
var
  Missing: String;
begin
  Result := True;
  Missing := MissingDiscParts(DiscPage.Values[0]);
  if Missing = '' then
    Exit;

  MsgBox(UserMessage('NotADisc', Missing), mbError, MB_OK);
  Result := False;
end;

{ The word the movie player reads for the shape it draws in. Its own function so that the two places
  that care, the page and the writer, cannot disagree about which index means what. }
function ChosenMovieScaling: String;
begin
  if ScalingPage.SelectedValueIndex = 1 then
    Result := 'stretch'
  else
    Result := 'letterbox';
end;

{ The cap as the ini spells it, or '' when what was typed is not one. 0 is a real answer meaning no
  limit, so nothing chosen cannot be reported as 0. Above the documented ceiling it is refused rather
  than clamped. }
function ChosenTargetFps: String;
var
  Typed: Integer;
begin
  case FpsPage.SelectedValueIndex of
    0: Result := '100';
    1: Result := '0';   { no limit }
  else
    begin
      Result := '';
      Typed := StrToIntDef(Trim(FpsEdit.Text), -1);
      if (Typed >= 0) and (Typed <= 1000) then
        Result := IntToStr(Typed);
    end;
  end;
end;

{ Leaving the frame rate page. Only the typed answer can be wrong, and it is caught here rather than
  at the end, because here the box is still in front of the person who filled it in. }
function FpsPageAccepted: Boolean;
begin
  Result := ChosenTargetFps <> '';
  if not Result then
    MsgBox(ExpandConstant('{cm:FpsBadValue}'), mbError, MB_OK);
end;

{ The third option. Picks a new target folder and writes it back into the directory page's edit,
  which is where the app constant is read from for the rest of the wizard, so nothing else has to
  know the folder changed.

  Cancelling the browser leaves the page up rather than falling through to one of the other two: the
  player asked to install somewhere else and has not said where yet.

  A folder that also holds a game puts the same question again about that folder, which is why the
  page is left up and the caption refreshed. Otherwise the choice is reset to the recommended one,
  so a later pass over this page does not start on an option that has already been acted on. }
function ChooseDifferentFolder: Boolean;
var
  Dir: String;
begin
  Dir := ExpandConstant('{app}');
  Result := BrowseForFolder(ExpandConstant('{cm:ReinstallBrowse}'), Dir, True);
  if not Result then
    Exit;

  WizardForm.DirEdit.Text := RemoveBackslashUnlessRoot(Dir);

  if GameIsInstalledAt(Dir) then begin
    ReinstallPage.SubCaptionLabel.Caption := UserMessage('ReinstallText', Dir);
    Result := False;
  end
  else
    ReinstallPage.SelectedValueIndex := 0;
end;

{ Leaving the reinstall page. The foreign wrapper is raised here because this is where the reinstall
  decision is made, so answering the question changes it. Only asked of somebody installing over the
  existing files; a clean reinstall removes the stranger anyway.

  No is taken at face value. The same stranger is refused again at the ready page if our wrapper is
  still ticked by then. }
function ReinstallPageAccepted: Boolean;
var
  Foreign: String;
begin
  Result := True;

  if ReinstallPage.SelectedValueIndex = 2 then begin
    Result := ChooseDifferentFolder;
    Exit;
  end;

  if ReinstallPage.SelectedValueIndex = 0 then
    Exit;

  Foreign := ForeignWrapperFiles(ExpandConstant('{app}'));
  if Foreign = '' then
    Exit;

  if MsgBox(UserMessage('WrapperFound', Foreign),
            mbConfirmation, MB_YESNO) = IDYES then
    ReinstallPage.SelectedValueIndex := 0;
end;

{ Leaving the last page. The first point where both the reinstall choice and the ticked components
  are known, and the last where the player can still go back.

  The deletion is confirmed rather than announced, because a preselected radio button is not consent
  to deleting a folder. The wrapper is refused rather than asked about, because by here the answer
  could no longer change anything. }
function ReadyPageAccepted: Boolean;
var
  Foreign: String;
begin
  Result := True;

  if WantsCleanReinstall then begin
    Result := MsgBox(UserMessage('ConfirmClean', ExpandConstant('{app}')),
                     mbConfirmation, MB_YESNO) = IDYES;
    if not Result then
      Exit;
  end;

  { A fresh installation removes any stranger in the wrapper slot along with everything else, so the
    refusal below has nothing to refuse. }
  if WantsCleanReinstall then
    Exit;

  if not WizardIsComponentSelected('patch\wrapper') then
    Exit;

  Foreign := ForeignWrapperFiles(ExpandConstant('{app}'));
  if Foreign = '' then
    Exit;

  MsgBox(UserMessage('WrapperBlocks', Foreign), mbError, MB_OK);
  Result := False;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;

  if CurPageID = DiscPage.ID then
    Result := DiscPageAccepted
  else if CurPageID = ReinstallPage.ID then
    Result := ReinstallPageAccepted
  else if CurPageID = FpsPage.ID then
    Result := FpsPageAccepted
  else if CurPageID = wpReady then
    Result := ReadyPageAccepted;
end;

{ big.lab lives inside GAMEDATA\GOBS\BIG.Z, an InstallShield 3 archive using PKWARE implode. Nothing
  on a current Windows expands that and the disc's own 16-bit installer no longer runs, so
  is3_extract does it.

  In PrepareToInstall because this is the last point where returning a message aborts without
  anything having been written. The extractor checks the expanded length against the archive's own
  directory and deletes its output on a mismatch, so exit code 0 means the file is right. }
{ A folder patched before is not a known quantity: another project's wrapper may hold the dinput or
  ddraw name, and older mods may sit in mods\ under names this release no longer uses. Both keep
  working well enough to look installed while doing nothing.

  The player's saves and settings are copied into the temporary folder before anything is deleted and
  copied back after the last file is written. }

{ Copies the files directly inside SourceDir into TargetDir. Found reports how many were there, so a
  caller can tell a complete copy from a partial one; the caller that matters runs immediately before
  the folder it just read is deleted.

  Does not recurse. A SourceDir that is not there reports zero and zero rather than failing. }
function CopyFilesInFolder(const SourceDir, TargetDir: String; var Found: Integer): Integer;
var
  Rec: TFindRec;
begin
  Result := 0;
  Found := 0;
  if not DirExists(SourceDir) then
    Exit;

  if FindFirst(AddBackslash(SourceDir) + '*', Rec) then begin
    try
      repeat
        if (Rec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then begin
          Found := Found + 1;
          { Deferred until something was actually found, so an empty source folder does not leave
            an empty target folder behind in a freshly laid down installation. }
          if ForceDirectories(TargetDir) then
            if CopyFile(AddBackslash(SourceDir) + Rec.Name,
                        AddBackslash(TargetDir) + Rec.Name, False) then
              Result := Result + 1;
        end;
      until not FindNext(Rec);
    finally
      FindClose(Rec);
    end;
  end;
end;

function CarryOverDir: String;
begin
  Result := ExpandConstant('{tmp}\carryover');
end;

{ One loose file, copied when it is there. Absent is not a failure: none of these files exists in
  every installation.

  Ok is only ever cleared, never set, so a caller can run a list of these and read the answer once at
  the end without a later success hiding an earlier failure. }
procedure CopyOptionalFile(const Source, Target: String; var Copied: Integer; var Ok: Boolean);
begin
  if not FileExists(Source) then
    Exit;

  if CopyFile(Source, Target, False) then
    Copied := Copied + 1
  else
    Ok := False;
end;

{ obi.ini is the game's own settings and goes back as it was.

  The patch configurations come back under a .previous name instead. Each names one section per DLL
  and DLLs are renamed between releases, so a file kept from an older one looks configured and
  behaves like defaults.

  movies_hd is the converted cutscenes: a 110 MB download and a full re-encode, and the one large
  thing carried here. It travels through the temporary folder like everything else, so a clean
  reinstall needs room for a second copy of it. Two limits: MovieDirectory can point somewhere else
  inside the game folder, which is not covered, and the copy does not recurse.

  Returns False when it did not carry everything it found, and the caller stops. This runs
  immediately before the game folder is deleted. }
function BackUpExistingInstall(var RescuedCount: Integer): Boolean;
var
  AppDir, Backup: String;
  Found, Copied: Integer;
begin
  AppDir := AddBackslash(ExpandConstant('{app}'));
  Backup := AddBackslash(CarryOverDir);
  RescuedCount := 0;
  Result := True;

  { The loose files below are copied straight into this folder, so it has to exist first.
    CopyFilesInFolder only creates the subfolder it is given, and only once it has found a file. }
  if not ForceDirectories(RemoveBackslashUnlessRoot(Backup)) then begin
    Result := False;
    Exit;
  end;

  Copied := CopyFilesInFolder(AppDir + 'Save', Backup + 'Save', Found);
  RescuedCount := RescuedCount + Copied;
  if Copied <> Found then
    Result := False;

  Copied := CopyFilesInFolder(AppDir + 'movies_hd', Backup + 'movies_hd', Found);
  RescuedCount := RescuedCount + Copied;
  if Copied <> Found then
    Result := False;

  CopyOptionalFile(AppDir + 'obi.ini',          Backup + 'obi.ini',                   RescuedCount, Result);
  CopyOptionalFile(AppDir + 'engine_fixes.ini', Backup + 'engine_fixes.ini.previous', RescuedCount, Result);
  CopyOptionalFile(AppDir + 'dxwrapper.ini',    Backup + 'dxwrapper.ini.previous',    RescuedCount, Result);
end;

{ Guarded twice, because this is the one action in the installer that cannot be taken back: the
  folder must actually hold the game, and it must not be a drive root. }
function RemoveExistingInstall: Boolean;
var
  AppDir: String;
begin
  Result := False;
  AppDir := RemoveBackslashUnlessRoot(ExpandConstant('{app}'));

  if Length(AppDir) < 4 then
    Exit;
  if not GameIsInstalledAt(AppDir) then
    Exit;

  Result := DelTree(AppDir, True, True, True);
end;

{ The way back, and the last time the rescued copies exist: the temporary folder goes when Setup
  closes. A file carried out and not carried back has to reach the player while they can still act. }
function RestoreCarryOver: Boolean;
var
  AppDir, Backup: String;
  Found, Copied, Restored: Integer;
begin
  AppDir := AddBackslash(ExpandConstant('{app}'));
  Backup := AddBackslash(CarryOverDir);
  Result := True;
  Restored := 0;   { counted by the helper, and not wanted here: the way in reports the number }

  Copied := CopyFilesInFolder(Backup + 'Save', AppDir + 'Save', Found);
  if Copied <> Found then
    Result := False;

  Copied := CopyFilesInFolder(Backup + 'movies_hd', AppDir + 'movies_hd', Found);
  if Copied <> Found then
    Result := False;

  CopyOptionalFile(Backup + 'obi.ini',                   AppDir + 'obi.ini',                   Restored, Result);
  CopyOptionalFile(Backup + 'engine_fixes.ini.previous', AppDir + 'engine_fixes.ini.previous', Restored, Result);
  CopyOptionalFile(Backup + 'dxwrapper.ini.previous',    AppDir + 'dxwrapper.ini.previous',    Restored, Result);
end;

{ The same preservation on the install-over path, where nothing is copied out. Runs before the first
  file is written and overwrites any older .previous, because the interesting one is the file the
  player was last running.

  Reports failure and the caller stops: the row that installs the new configuration promises this
  file exists. }
function PreserveExistingConfiguration: Boolean;
var
  AppDir: String;
  Preserved: Integer;
begin
  AppDir := AddBackslash(ExpandConstant('{app}'));
  Result := True;
  Preserved := 0;   { counted by the helper, and nothing here has a use for the number }

  CopyOptionalFile(AppDir + 'engine_fixes.ini', AppDir + 'engine_fixes.ini.previous', Preserved, Result);
  CopyOptionalFile(AppDir + 'dxwrapper.ini',    AppDir + 'dxwrapper.ini.previous',    Preserved, Result);
  CopyOptionalFile(AppDir + 'alsoft.ini',       AppDir + 'alsoft.ini.previous',       Preserved, Result);
end;

{ The height the player chose, or -1 for "not now". 0 is a real answer and means the source's own
  resolution, which is why this cannot report "nothing to do" as zero. }
function ChosenMovieHeight: Integer;
begin
  Result := -1;
  if not WizardIsComponentSelected('patch\fmv_player') then
    Exit;

  case MoviePage.SelectedValueIndex of
    0: Result := 0;
    1: Result := 1080;
    2: Result := 1440;
    3: Result := 2160;
  end;
end;

{ How many movies there are to convert, so the bar has a scale. Counted from the game's own folder
  rather than asked of the converter, because the converter only says how many it did once it is
  finished, and by then the bar is over. }
function CountMovies(const Dir: String): Integer;
var
  Rec: TFindRec;
begin
  Result := 0;
  if FindFirst(AddBackslash(Dir) + '*.BIK', Rec) then begin
    try
      repeat
        if (Rec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then
          Result := Result + 1;
      until not FindNext(Rec);
    finally
      FindClose(Rec);
    end;
  end;
end;

{ Called once per line the converter writes. Its -Quiet mode prints one machine-readable line per
  movie, which is what makes a progress bar possible without guessing how long anything takes.

  Machine lines go to stdout and anything written for a human to stderr, so Error tells them apart. }
procedure ConvertOnLog(const S: String; const Error, FirstLine: Boolean);
var
  Line: String;
begin
  if Error then
    Exit;

  Line := Trim(S);
  if Line = '' then
    Exit;

  if Copy(Line, 1, 5) = 'DONE ' then begin
    MovieResult := Copy(Line, 6, Length(Line));
    Exit;
  end;

  if (Copy(Line, 1, 3) = 'OK ') or (Copy(Line, 1, 5) = 'SKIP ') or
     (Copy(Line, 1, 5) = 'FAIL ') then begin
    MovieDone := MovieDone + 1;
    if MovieTotal > 0 then
      ConvertPage.SetProgress(MovieDone, MovieTotal);
    ConvertPage.SetText(ExpandConstant('{cm:ConvertWorking}'), Line);
  end;
end;

{ Runs the converter hidden with a progress page in front of it.

  The script is the one in the patch's unpacked folder, not the copy just installed: the game folder
  is writable by ordinary users on purpose, and this runs with Setup's rights. }
{ One field out of the converter's summary line, which is "converted=n skipped=n failed=n".
  Returns -1 when the field is not there, so a summary in some future shape reads as unknown rather
  than as zero. }
function SummaryCount(const Summary, Field: String): Integer;
var
  P: Integer;
  Digits: String;
begin
  Result := -1;
  P := Pos(Field + '=', Summary);
  if P = 0 then
    Exit;

  P := P + Length(Field) + 1;
  Digits := '';
  while (P <= Length(Summary)) and (Summary[P] >= '0') and (Summary[P] <= '9') do begin
    Digits := Digits + Summary[P];
    P := P + 1;
  end;

  if Digits <> '' then
    Result := StrToIntDef(Digits, -1);
end;

procedure ConvertMovies;
var
  Height, ResultCode: Integer;
  Converted, Skipped: Integer;
  Script, Params: String;
begin
  Height := ChosenMovieHeight;
  if Height < 0 then
    Exit;

  Script := ExpandConstant('{#PatchTmp}\tools\convert_movies.ps1');
  if not FileExists(Script) then begin
    MsgBox(UserMessage('ConvertNoScript', Script), mbError, MB_OK);
    Exit;
  end;

  MovieTotal := CountMovies(ExpandConstant('{app}\GAMEDATA\MOVIE'));
  MovieDone := 0;
  MovieResult := '';

  { convert_movies.ps1 looks for FFmpeg in its own cache, then on PATH, then downloads one. The
    copy installed under mods\fmv is put on PATH here so it is found at the second step and the
    third never happens. This changes the environment of this process only, which the converter
    inherits; nothing is written to the machine's environment.

    "Convert Movies.bat" does the same for itself, so a conversion the player starts later finds it
    too. Both are needed: this one runs the script directly and never goes through the batch file. }
  SetEnvironmentVariable('PATH',
    ExpandConstant('{app}\mods\fmv') + ';' + GetEnv('PATH'));

  Params := '-NoProfile -ExecutionPolicy Bypass -File "' + Script + '"' +
            ' -Quiet -GameDirectory "' + ExpandConstant('{app}') + '"' +
            ' -TargetHeight ' + IntToStr(Height);

  ConvertPage.SetText(ExpandConstant('{cm:ConvertPreparing}'), '');
  ConvertPage.SetProgress(0, MovieTotal);
  ConvertPage.Show;
  try
    if not ExecAndLogOutput(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
                            Params, ExpandConstant('{app}'), SW_HIDE,
                            ewWaitUntilTerminated, ResultCode, @ConvertOnLog) then
      ResultCode := -1;
  finally
    ConvertPage.Hide;
  end;

  { The summary line is what the message reports, because "it stopped" and "it converted eleven of
    them" are different things to be told. An exit code with no summary means it never got as far as
    converting anything, which is its own answer. }
  if (ResultCode = 0) and (MovieResult <> '') then begin
    { A successful run can still have encoded nothing, in two different ways, and the difference is
      the whole of what the player needs to know. The converter reports success for all of them, so
      testing the exit code alone said "the cutscenes were converted" whether it did eleven films
      or none.

      Either count reads as -1 when the summary line is not in the shape expected. That is not
      evidence of nothing having happened, so it falls to the general message rather than to a
      claim about films that were or were not found. }
    Converted := SummaryCount(MovieResult, 'converted');
    Skipped   := SummaryCount(MovieResult, 'skipped');

    if (Converted = 0) and (Skipped > 0) then
      { Every film already had an mp4 beside it, so the size just chosen was applied to none. }
      MsgBox(UserMessage('ConvertKept', MovieResult), mbInformation, MB_OK)
    else if (Converted = 0) and (Skipped = 0) then
      { Nothing encoded and nothing skipped: there were no .BIK files there to begin with. }
      MsgBox(UserMessage('ConvertNoFilms', MovieResult), mbInformation, MB_OK)
    else
      MsgBox(UserMessage('ConvertDone', MovieResult), mbInformation, MB_OK)
  end
  else if MovieResult <> '' then
    MsgBox(UserMessage('ConvertPartly', MovieResult), mbError, MB_OK)
  else
    MsgBox(UserMessage('ConvertFailed', IntToStr(ResultCode)), mbError, MB_OK);
end;

{ Everything here runs after the last file is written, so nothing a row installs can overwrite it.
  The carry-over goes back first, so movies converted before an earlier installation are in place
  when the converter looks and it skips them instead of producing them again. }
{ Copies the bundled saves into Save\.

  After the carry-over, which is why this is not a file row: rows are written first and the restore
  would copy the player's own folder straight over them.

  Collisions are overwritten and everything else is left alone. The game numbers its slots with two
  digits, so these eleven occupy 1 to 11.

  The read-only bit is taken off everything in Save\ afterwards. Nothing here sets it, but a folder
  restored from a backup or off a CD can carry it, and a slot the game cannot write back to looks
  like a broken game. }
procedure InstallCompleteSaves;
var
  Rec: TFindRec;
  SaveDir, SrcDir, Target: String;
  Found, Copied, Kept: Integer;
begin
  if not WizardIsComponentSelected('complete_saves') then
    Exit;

  SaveDir := AddBackslash(ExpandConstant('{app}\Save'));
  SrcDir := AddBackslash(ExpandConstant('{#SavesTmp}\Save'));
  Found := 0;
  Copied := 0;
  Kept := 0;

  if not DirExists(SrcDir) then
    Exit;

  { A SLOT THAT ALREADY HOLDS A SAVE IS NEVER WRITTEN. This used to hand the whole set to
    CopyFilesInFolder, which copies with FailIfExists false because that is what the carry-over
    restore needs, and the restore is putting the player's own files back. Here it meant a returning
    player who reran Setup to update a patch lost slots 1 to 11: complete_saves is part of the
    default install type, UsePreviousSetupType is off so a rerun returns to that default, and the
    error text on this very procedure promises their saved games were not touched. It now is.

    The component is deliberately still offered by default. On a fresh installation nothing
    collides, so all eleven arrive; on a rerun the player keeps every slot they have used and gets
    only the chapters they never started. }
  if FindFirst(SrcDir + '*', Rec) then begin
    try
      repeat
        if (Rec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then begin
          Found := Found + 1;
          Target := SaveDir + Rec.Name;
          if FileExists(Target) then
            Kept := Kept + 1
          else begin
            if ForceDirectories(RemoveBackslashUnlessRoot(SaveDir)) then begin
              if CopyFile(SrcDir + Rec.Name, Target, True) then begin
                Copied := Copied + 1;
                { Read-only cleared only on what was just written, never on a file the player
                  already had: the media these were compiled from can carry the attribute and the
                  game cannot write a slot that has it. }
                if (Rec.Attributes and FILE_ATTRIBUTE_READONLY) <> 0 then
                  SetFileAttributes(Target, Rec.Attributes and not FILE_ATTRIBUTE_READONLY);
              end;
            end;
          end;
        end;
      until not FindNext(Rec);
    finally
      FindClose(Rec);
    end;
  end;

  Log('complete_saves: ' + IntToStr(Found) + ' carried, ' + IntToStr(Copied) + ' written, ' +
      IntToStr(Kept) + ' slots left alone because the player already had a save there');

  { Only a genuine failure is worth a dialog. A slot kept is the feature working. }
  if (Copied + Kept) <> Found then
    MsgBox(UserMessage('SavesFailed', ExpandConstant('{app}\Save')), mbError, MB_OK);
end;

{ The answers that are keys rather than files. After the install, because the row that installs
  engine_fixes.ini replaces it and anything written earlier would go with it.

  A key is written only when the fix that reads it was installed; a section for a DLL that is not in
  mods\ is read by nobody.

  Failure is reported rather than swallowed. It cannot stop the installation, everything is on disk
  by now. }
procedure ApplyChosenSettings;
var
  Ini, Failed: String;
begin
  Ini := ExpandConstant('{app}\engine_fixes.ini');
  if not FileExists(Ini) then
    Exit;   { the patch was not installed, so there is no configuration to answer into }

  Failed := '';

  if WizardIsComponentSelected('patch\fmv_player') then
    if not SetIniString('fmv_player', 'Scaling', ChosenMovieScaling, Ini) then
      AddName('[fmv_player] Scaling', Failed);

  if WizardIsComponentSelected('patch\framerate_fix') then
    if not SetIniString('framerate_fix', 'TargetFps', ChosenTargetFps, Ini) then
      AddName('[framerate_fix] TargetFps', Failed);

  if Failed <> '' then
    MsgBox(UserMessage('SettingsFailed', Failed), mbError, MB_OK);
end;

{ Without this line the audio wrapper installs and is never asked for anything. The game reaches EAX
  through a Miles provider plugin, and this key names the plugin word for word; a stock installation
  names the 2D one, which has no 3D and no EAX. The name was read out of MSSEAX.M3D beside the game.

  obi.ini is the player's file, holding their key bindings, resolution and gamma, so one key is
  written and the file is copied beside itself first.

  Written even when the file does not exist yet. On a fresh installation the game writes it on first
  run, and waiting for that would leave the component inert. }
procedure ApplySoundProvider;
var
  Ini: String;
  Backed: Integer;
  Ok: Boolean;
begin
  if not WizardIsComponentSelected('patch\dsoal') then
    Exit;

  Ini := ExpandConstant('{app}\obi.ini');
  Backed := 0;
  Ok := True;
  CopyOptionalFile(Ini, ExpandConstant('{app}\obi.ini.previous'), Backed, Ok);

  if not SetIniString('options', 'Sound 3D Driver',
                      'Microsoft DirectSound3D with Creative Labs EAX(TM)', Ini) then
    MsgBox(UserMessage('SettingsFailed', '[options] Sound 3D Driver' + #13#10), mbError, MB_OK);
end;

{ A controller layout and a display mode, written into the player's own obi.ini one key at a time.

  Sixty writes rather than a file copy, for the reason the sound provider above gives: obi.ini holds
  the player's gamma, their sound choice and anything else they have set, so it is merged and never
  replaced. The previous file is copied beside itself first, the same as there.

  The bindings are the PlayStation release's arrangement, read off a working installation rather
  than derived: the engine stores each one as a packed integer whose meaning is not documented
  anywhere in this project, so they are carried as the numbers they are. JOYENABLE is among them
  because a layout with the pad switched off is the state this component exists to save people from.

  1920x1080 replaces the engine's own 640x480, which is what a fresh obi.ini carries. It is a
  starting value and not a limit: enhanced_resolution offers the full mode list, and the video
  options screen still writes whatever the player picks.

  Values are written even where the key already exists, which is the whole point when somebody ticks
  this, and is why the caption says so in both languages. }
{ One semicolon-separated string rather than an array, because Inno's Pascal Script has no typed
  constants and cannot initialise an array where it is declared. Broken on key boundaries so a line
  never splits a binding in half. }
const
  GameDefaultBindings =
    'X0JOY=258;X1JOY=513;Y0JOY=772;Y1JOY=1027;' +
    'Z0JOY=261;Z1JOY=518;R0JOY=775;R1JOY=2560;' +
    'R2JOY=1032;U0JOY=5129;U1JOY=5130;P0JOY=781;' +
    'P1JOY=1038;P2JOY=4623;K0JOY=1553;K1JOY=2322;' +
    'K2JOY=1299;K3JOY=1812;K4JOY=2069;K8JOY=2072;' +
    'K9JOY=3097;K10JOY=1306;K11JOY=1307;K12JOY=3100;' +
    'K13JOY=3357;K14JOY=3614;K15JOY=3871;K16JOY=4128;' +
    'K17JOY=4385;K18JOY=1314;K19JOY=1315;K20JOY=1316;' +
    'K21JOY=293;K22JOY=2048;K23JOY=550;K24JOY=2048;' +
    'K25JOY=807;K26JOY=2560;K27JOY=1064;K28JOY=2560;' +
    'K29JOY=297;K30JOY=1280;K31JOY=554;K32JOY=1280;' +
    'K33JOY=811;K34JOY=1280;K35JOY=1068;K36JOY=1280;' +
    'K37JOY=2605;K38JOY=2606;K39JOY=2607;K40JOY=2608;' +
    'K41JOY=65535;K42JOY=65535;JOYENABLE=1;R3JOY=2560;' +
    'P3JOY=4880;K5JOY=2582;K6JOY=2071;K7JOY=256';

procedure ApplyGameDefaults;
var
  Ini, Rest, Entry, Failed: String;
  Backed, Split, Written: Integer;
  Ok: Boolean;
begin
  if not WizardIsComponentSelected('game_defaults') then
    Exit;

  Ini := ExpandConstant('{app}\obi.ini');
  Backed := 0;
  Ok := True;
  CopyOptionalFile(Ini, ExpandConstant('{app}\obi.ini.previous'), Backed, Ok);

  Failed := '';
  Written := 0;
  Rest := GameDefaultBindings + ';';
  while Rest <> '' do begin
    Split := Pos(';', Rest);
    if Split = 0 then begin
      Entry := Rest;
      Rest := '';
    end else begin
      Entry := Copy(Rest, 1, Split - 1);
      Rest := Copy(Rest, Split + 1, Length(Rest) - Split);
    end;

    Split := Pos('=', Entry);
    { Every entry is written as KEY=VALUE above, so a missing separator is an edit to that table
      rather than anything the machine can produce. Skipped rather than guessed at. }
    if Split > 1 then begin
      if SetIniString('options', Copy(Entry, 1, Split - 1),
                      Copy(Entry, Split + 1, Length(Entry) - Split), Ini) then
        Written := Written + 1
      else
        Failed := Failed + '[options] ' + Copy(Entry, 1, Split - 1) + #13#10;
    end;
  end;

  if not SetIniString('options', 'screen_width', '1920', Ini) then
    Failed := Failed + '[options] screen_width' + #13#10;
  if not SetIniString('options', 'screen_height', '1080', Ini) then
    Failed := Failed + '[options] screen_height' + #13#10;

  { A count rather than a decoration. Every write above reports its own failure, but a table that
    parsed into nothing would report nothing at all and leave this component looking as though it
    had worked, which is the one way this can fail silently. }
  if (Written = 0) and (Failed = '') then
    Failed := 'the controller layout table parsed into no entries' + #13#10;

  if Failed <> '' then
    MsgBox(UserMessage('SettingsFailed', Failed), mbError, MB_OK);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep <> ssPostInstall then
    Exit;

  if not RestoreCarryOver then
    MsgBox(UserMessage('RestoreFailed', CarryOverDir), mbError, MB_OK);

  ApplyChosenSettings;
  ApplySoundProvider;
  ApplyGameDefaults;
  InstallCompleteSaves;
  ConvertMovies;
end;

{ Clears one contested name by moving what is there rather than overwriting it.

  BackupIsChainLink is the real difference. The loader forwards everything it does not handle to
  whatever held dinput.dll before and expects it as dinput_orig.dll, so that backup is live and an
  existing one must never be replaced. Nothing forwards to dsound.dll.previous; that wrapper hands
  what it does not answer to the system library, so an older backup can go.

  Returns '' when the slot is clear or was cleared, a message when it cannot be. }
function MakeRoomInSlot(const SlotName, BackupName, MessageName: String;
                        const BackupIsChainLink: Boolean): String;
var
  AppDir, Existing, Backup: String;
begin
  Result := '';
  AppDir := ExpandConstant('{app}');
  Existing := AddBackslash(AppDir) + SlotName;
  Backup   := AddBackslash(AppDir) + BackupName;

  if not FileExists(Existing) then
    Exit;
  if SlotHoldsOurFile(AppDir, SlotName) then
    Exit;   { a previous run of this installer, or the same library by hand; ours to replace }

  if FileExists(Backup) then begin
    if BackupIsChainLink then begin
      Result := UserMessage(MessageName, Existing);
      Exit;
    end;
    DeleteFile(Backup);
  end;

  if not RenameFile(Existing, Backup) then
    Result := UserMessage(MessageName, Existing);
end;

{ Moves aside a controller wrapper left by an older release of this installer, which installed it as
  winmm.dll.

  Leaving it is not harmless. On a machine where that name is not redirected the game loads it, so
  the older wrapper would quietly take precedence over the one being installed now, and nothing
  would say so. It is moved rather than deleted, under the same .previous name every other
  configuration keeps.

  Only when it is ours. A winmm.dll carrying somebody else's code is left exactly where it is: this
  installer no longer writes that name and therefore no longer contests it.

  Reported and not fatal. The new installation is complete and correct without this, and the one
  machine it matters on is the one that did not need the bridge in the first place. }
procedure RetireOldControllerWrapper;
var
  Existing, Backup: String;
begin
  Existing := ExpandConstant('{app}\winmm.dll');
  if not FileExists(Existing) then
    Exit;
  if not FileContainsMarker(Existing, 'Xidi') then
    Exit;

  Backup := ExpandConstant('{app}\winmm.dll.previous');
  if FileExists(Backup) then
    DeleteFile(Backup);

  if not RenameFile(Existing, Backup) then
    MsgBox(UserMessage('OldWrapperFailed', Existing), mbError, MB_OK);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ArchivePath, OutputPath, LogPath, ExtractorPath: String;
  Detail: AnsiString;   { LoadStringFromFile reads bytes, and the extractor writes plain ASCII }
  ResultCode: Integer;
begin
  Result := '';
  NeedsRestart := False;

  { First, because a clean reinstall removes the folder the steps below would inspect. The rescue is
    checked before the delete, which is the only point where checking it is still worth anything. }
  if WantsCleanReinstall then begin
    if not BackUpExistingInstall(RescuedFileCount) then begin
      Result := UserMessage('CarryOverFailed', CarryOverDir);
      Exit;
    end;
    if not RemoveExistingInstall then begin
      Result := UserMessage('RemoveFailed', ExpandConstant('{app}'));
      Exit;
    end;
  end
  else if not PreserveExistingConfiguration then begin
    { The other path's half of the same promise. The clean path carried both configurations out a
      moment ago; here they are copied beside themselves instead, because nothing is being deleted
      and there is nowhere they need to travel to. }
    Result := UserMessage('PreserveFailed', ExpandConstant('{app}'));
    Exit;
  end;

  if WizardIsComponentSelected('patch') then begin
    Result := MakeRoomInSlot('dinput.dll', 'dinput_orig.dll', 'DinputTaken', True);
    if Result <> '' then
      Exit;
  end;

  { This installer no longer installs a controller wrapper at all, so there is no slot of its own
    needing room. What a much older release left under the name winmm.dll still has to go, the same
    as it always did: the name is not ours and never answers to a controller any more. }
  RetireOldControllerWrapper;

  if WizardIsComponentSelected('patch\dsoal') then begin
    Result := MakeRoomInSlot('dsound.dll', 'dsound.dll.previous', 'DsoundTaken', False);
    if Result <> '' then
      Exit;
  end;

  ArchivePath   := AddBackslash(DiscPage.Values[0]) + 'GAMEDATA\GOBS\BIG.Z';
  OutputPath    := ExpandConstant('{tmp}\big.lab');
  LogPath       := ExpandConstant('{tmp}\is3_extract.log');
  ExtractorPath := ExpandConstant('{tmp}\is3_extract.exe');

  ExtractTemporaryFile('is3_extract.exe');
  WizardForm.PreparingLabel.Caption := ExpandConstant('{cm:Expanding}');

  if not Exec(ExtractorPath,
              '"' + ArchivePath + '" big.lab "' + OutputPath + '" --log "' + LogPath + '"',
              '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    ResultCode := -1;

  if ResultCode <> 0 then begin
    { The log holds the extractor's own account of which step failed. It lives in the temporary
      folder, which is deleted on the way out, so it is read into the message while it still
      exists rather than pointed at. }
    Detail := '';
    LoadStringFromFile(LogPath, Detail);
    { The argument list must not begin a line: Inno reads a leading '[' as a section tag, even
      inside [Code]. }
    Result := FmtMessage(ExpandConstant('{cm:ExpandFailed}'), [IntToStr(ResultCode),
                         String(Detail)]);
  end;
end;
