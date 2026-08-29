; An optional set of starting values for the game's own settings file, obi.ini: a controller layout
; and a 1080p display mode.

; There are no files here. obi.ini belongs to the player, so nothing is copied over it and no file
; row can express what this does; the keys are written one at a time from [Code], the same way the
; sound provider already is. See ApplyGameDefaults in the main script for the list and the write.
;
; Two things the shipped game leaves in a state most people change immediately, and both cost a trip
; through a menu that is easy to miss:
;
;   The display mode starts at 640x480 on a fresh installation, because that is what the 1999 engine
;   defaults to and nothing else writes the key. enhanced_resolution offers wider modes but does not
;   choose one, so a first run is a small window until somebody opens the video options.
;
;   The joystick bindings start unset and JOYENABLE starts off, so a pad does nothing at all until
;   every axis and button has been bound by hand on the controls screen. The layout written here
;   mirrors the PlayStation release's own arrangement rather than being invented, which is the
;   arrangement most people coming to this game already have in their hands.
;
; Off by default, and deliberately so: it writes into the player's own file, and somebody who has
; already bound a pad the way they like should not find it rearranged by installing a patch.

[Components]
Name: "game_defaults"; Description: "{cm:CompGameDefaults}"; Types: everything custom
