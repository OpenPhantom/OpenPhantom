; An optional set of saved games, one at the start of each chapter, compiled into Setup.

; These were recorded for this project rather than taken from a third party, so there is no URL, no
; hash and no download step: the files sit in dist\saves and Setup carries them. That is the whole
; reason they are here. The set that used to be downloaded came from an aggregator site that
; published it without making it, so nothing in the chain granted a licence and none could be named
; in the release notes the way every other component's is.
;
; They cost 188 KB in the compiled installer, against 857 KB on disk, because [Setup] already
; compresses everything with lzma2. That is small enough that carrying them beats a download that
; can fail: the old one went through four redirects onto a hostname minted per request, and Inno's
; downloader could not always follow it.

; Slot N holds the START of level N+1, so the set is a chapter select and not a completed game.
; ZANZI11 is the Darth Maul duel. The name the game shows in its load menu is inside each save,
; 32 bytes at offset 0x20, not derived from the filename.
#define SavesTmp           "{tmp}\complete_saves"

[Components]
Name: "complete_saves"; Description: "{cm:CompSaves}"; Types: everything custom

[Files]
; Laid down in {tmp} rather than straight into Save\. The copy into Save\ runs from [Code] after the
; carry-over has put the player's own saves back, otherwise the restore would overwrite it. See
; InstallCompleteSaves.
;
; A slot that already holds a save is never written. That is what makes this component safe to leave
; in the default install type: on a fresh installation nothing collides and all eleven arrive, and
; on a rerun to update a patch the player keeps every slot they have used and gets only the chapters
; they never started. It did overwrite, and the error text on the copy has always promised it did
; not.
Source: "dist\saves\*.SAV"; DestDir: "{#SavesTmp}\Save"; \
    Components: complete_saves; Flags: ignoreversion
