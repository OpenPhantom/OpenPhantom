; An optional set of saved games, one per chapter, downloaded on request.

; The page hands the URL out with a &refresh= token that changes on every page load. Without the
; token the same bytes come back, so the plain form is what is pinned.
;
; There is no upstream checksum for this one: the site publishes the file, it did not build it. The
; hash below was measured from the download itself, so re-measure it when bumping rather than
; carrying a number forward.
#define SavesUrl           "https://savegame.pro/download/pc-star-wars-episode-i-the-phantom-menace-savegame/?wpdmdl=9106"
#define SavesSha256        "c352c097fcd153cce08a24221ccf7d6b2c34abaa5ea2772f259e1c9058abd851"

; Unpacked total including the advertising shortcut, which Setup unpacks even though nothing
; installs it.
#define SavesUnpackedSize  866915
#define SavesTmp           "{tmp}\complete_saves"

[Components]
Name: "complete_saves"; Description: "{cm:CompSaves}"; Types: custom

[Files]
; Download only. The copy into Save\ runs from [Code] after the carry-over has put the player's own
; saves back, otherwise the restore would overwrite it. See InstallCompleteSaves.
Source: "{#SavesUrl}"; DestDir: "{#SavesTmp}"; DestName: "complete_saves.7z"; \
    {#HashParam(SavesSha256)}ExternalSize: {#SavesUnpackedSize}; \
    Components: complete_saves; \
    Flags: external download extractarchive recursesubdirs ignoreversion
