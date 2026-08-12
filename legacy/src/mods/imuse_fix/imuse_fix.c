/* imuse_fix.c: the music pause, which the engine built and never wired to anything.
 *
 * ==============================================================================================
 * What this does, and what it deliberately does not
 *
 * It does two things and logs a third.
 *
 *   1. pause the music while the game is not in front. The engine never does. Its window
 *      procedure routes WM_ACTIVATEAPP to exactly two functions, and both of them are about the
 *      mouse: one warps and captures the pointer, the other releases it. Nothing in the audio
 *      path is reached from a focus change at all. So a game left running behind a browser keeps
 *      playing its music, which is a 1999 assumption about how computers get used.
 *
 *   2. resume a pause nobody owns. The music pause is a single latch. Exactly one function sets
 *      it and exactly one clears it, and both are called from a small, closed set of places. If
 *      the latch is found set while the game is in front, the pause menu is not up and this DLL
 *      did not set it, then something paused the music and never came back, and since the
 *      DirectSound buffer the music streams through is a LOOPING buffer that is never stopped,
 *      the audible result is the last second of PCM circling forever rather than silence.
 *
 *   3. NAME EVERY CHANGE. One line per TRANSITION, never one per frame.
 *
 * ---- What it is not ---------------------------------------------------------------------------
 * There is a well-travelled claim that the music bug is a return value: the module handler's
 * `case 8` and `case 9` leave the result at 2 ("not handled") where every other implemented case
 * sets it to 0, and a dispatcher pair in this engine gates its suspend/resume bookkeeping on
 * exactly that result. The reading of those two dispatchers is correct. They are also dead: in
 * all three builds, including the Edit Tool's recompile, where they have moved; there
 * is no call to them, no jump to them and no stored pointer at them.
 *
 * The live pause path is the pause menu, and it uses the OTHER dispatcher: it walks the module
 * list, calls each handler and drops the result into a local nobody reads. `ImPause` and
 * `ImResume` are balanced across it. Patching those two return values changes nothing that
 * executes, which is why this file does not do it. The guard in point 2 is the honest version of
 * the same worry: instead of predicting how the latch might get stuck, it watches whether it IS
 * stuck, repairs it, and says so, so a log without that line is evidence, not silence.
 *
 * ---- The one thing to be careful with --------------------------------------------------------
 * Resuming a pause that IS owned would play music over the pause menu. Three conditions guard it,
 * and the feature switches itself off rather than guess when it cannot read the third:
 * the game must be in front, this DLL must not be the one that paused, and the pause menu's own
 * latch must be clear. On top of that the state has to persist for a grace period, so a single
 * frame caught between the broadcast and the menu's own bookkeeping can never trigger it.
 * ============================================================================================ */
#include "imuse_fix.h"

#include "music_probe.h"
#include "music_sites.h"

#include "common/frame_hook.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define IMUSE_FIX_SECTION "imuse_fix"

/* The engine's own per-frame service tests `g_musicAttached != 1` and returns. Anything this file
 * calls goes into IMUSE.DLL without that test, so it uses the same one. */
#define MUSIC_ATTACHED 1

/* A cap on the transition log. A session that produces this many music state changes has already
 * told us what we needed to know, and the log should not grow without bound. */
#define TRANSITION_LOG_LIMIT 400

typedef void (__cdecl *music_latch_fn_t)(void);

typedef struct imuse_fix_config {
    bool  enabled;
    bool  pause_on_focus_loss;
    bool  resume_orphaned_pause;
    int32_t orphan_grace_frames;
    bool  log_transitions;
} imuse_fix_config_t;

typedef struct imuse_fix_state {
    bool installed;
    bool active;             /* the pause half is armed */
    bool probe_active;       /* the heartbeat watch is armed; independent of the above */

    music_sites_t sites;
    music_latch_fn_t pause_music;
    music_latch_fn_t resume_music;

    DWORD process_id;

    /* Did WE pause it? Nothing else may be resumed by the guard. */
    bool we_paused;

    /* How many consecutive frames the latch has looked orphaned. */
    int32_t orphan_frames;

    /* The last state that was written to the log, so only changes are written. */
    bool have_last;
    int32_t last_attached;
    int32_t last_paused;
    int32_t last_sys_pause;
    bool    last_foreground;
    int32_t last_state_cue;
    int32_t last_sequence_cue;

    int32_t transitions_logged;
    bool    transition_cap_announced;

    int32_t orphans_repaired;
} imuse_fix_state_t;

static imuse_fix_config_t config;
static imuse_fix_state_t  state;

/* ============================================================================================ */
static void load_configuration(void)
{
    config.enabled = ini_read_bool(IMUSE_FIX_SECTION, "Enabled", true);
    config.pause_on_focus_loss =
        ini_read_bool(IMUSE_FIX_SECTION, "PauseMusicOnFocusLoss", true);
    config.resume_orphaned_pause =
        ini_read_bool(IMUSE_FIX_SECTION, "ResumeOrphanedPause", true);
    config.orphan_grace_frames =
        ini_read_int(IMUSE_FIX_SECTION, "OrphanGraceFrames", 120);
    config.log_transitions = ini_read_bool(IMUSE_FIX_SECTION, "MusicLog", false);

    /* Below about a dozen frames the grace period stops separating a stuck latch from the normal
     * gap between the pause broadcast and the menu claiming its latch, so the guard would fire on
     * a healthy pause. Above a few thousand it would never fire at all. */
    if (config.orphan_grace_frames < 12) {
        config.orphan_grace_frames = 12;
    }
    if (config.orphan_grace_frames > 6000) {
        config.orphan_grace_frames = 6000;
    }
}

/* ============================================================================================ */
static bool foreground_belongs_to_us(void)
{
    HWND  foreground = GetForegroundWindow();
    DWORD owner = 0;

    if (foreground == NULL) {
        return false;
    }
    GetWindowThreadProcessId(foreground, &owner);
    return owner == state.process_id;
}

static int32_t read_cell(const volatile int32_t *cell, int32_t absent)
{
    if (cell == NULL) {
        return absent;
    }
    return *cell;
}

/* The two cue latches, for the log only. They are what says WHICH piece of music was in force at
 * the moment something went wrong, and the engine reports its NULL cues as 1000 and 2000. */
static void describe_cues(char *buffer, size_t size)
{
    if (state.sites.state_latch == NULL || state.sites.sequence_latch == NULL) {
        buffer[0] = '\0';
        return;
    }
    _snprintf(buffer, size, ", cue state=%d sequence=%d",
              (int)*state.sites.state_latch, (int)*state.sites.sequence_latch);
    buffer[size - 1] = '\0';
}

static void log_transition(int32_t attached, int32_t paused, int32_t sys_pause, bool foreground,
                           const char *reason)
{
    char cues[96];

    if (!config.log_transitions) {
        return;
    }
    if (state.transitions_logged >= TRANSITION_LOG_LIMIT) {
        if (!state.transition_cap_announced) {
            state.transition_cap_announced = true;
            log_warning("%d music state changes logged, no more will be written. A count this "
                        "high is itself the finding: something is toggling the music state "
                        "continuously.", TRANSITION_LOG_LIMIT);
        }
        return;
    }
    state.transitions_logged++;

    describe_cues(cues, sizeof(cues));
    log_info("music %s: attached=%d paused=%d pauseMenu=%d foreground=%d ours=%d%s",
             reason, (int)attached, (int)paused, (int)sys_pause, foreground ? 1 : 0,
             state.we_paused ? 1 : 0, cues);
}

static void note_state(int32_t attached, int32_t paused, int32_t sys_pause, bool foreground,
                       const char *reason)
{
    /* The two cue latches are part of what counts as a change, not just decoration on the line.
     * Both setters write their latch BEFORE they hand the cue to the music DLL and neither takes
     * it back if the DLL refuses it, and the setters' own `cue == latch` guard then swallows
     * every later attempt at that same cue. So a cue the DLL cannot honour parts the latch from
     * what is audible permanently. Logging every cue change is what makes the LAST cue before the
     * music stopped readable, and that is the one worth knowing. */
    int32_t state_cue = read_cell(state.sites.state_latch, 0);
    int32_t sequence_cue = read_cell(state.sites.sequence_latch, 0);

    bool changed = !state.have_last ||
                   attached != state.last_attached ||
                   paused != state.last_paused ||
                   sys_pause != state.last_sys_pause ||
                   foreground != state.last_foreground ||
                   state_cue != state.last_state_cue ||
                   sequence_cue != state.last_sequence_cue;

    if (!changed) {
        return;
    }
    state.have_last = true;
    state.last_attached = attached;
    state.last_paused = paused;
    state.last_sys_pause = sys_pause;
    state.last_foreground = foreground;
    state.last_state_cue = state_cue;
    state.last_sequence_cue = sequence_cue;

    log_transition(attached, paused, sys_pause, foreground, reason);
}

/* ============================================================================================ */
/* The focus half. Returns true when it has just changed the pause state, so the orphan guard can
 * stand down for this frame rather than judge a latch that is one instruction old. */
static bool service_focus(int32_t paused, int32_t sys_pause, bool foreground)
{
    if (!config.pause_on_focus_loss) {
        return false;
    }

    if (!foreground) {
        /* Only ever pause music that is actually playing. If the game already paused it, the
         * pause menu was open when the player alt-tabbed away, leave that pause to its owner,
         * because resuming it later is the owner's job and not ours. */
        if (!state.we_paused && paused == 0) {
            state.pause_music();
            state.we_paused = true;
            log_transition(MUSIC_ATTACHED, 1, sys_pause, foreground,
                           "paused because another application is in front");
            return true;
        }
        return false;
    }

    if (!state.we_paused) {
        return false;
    }

    /* The order the two pauses arrived in decides this, and getting it wrong plays music over the
     * PAUSE MENU. The player can alt-tab away, we pause, and the pause menu can then open
     * while the game is in the background, which sets the same latch a second time from the other
     * owner. Resuming on the way back would clear a latch the menu still believes it holds, and
     * the menu's own resume on the way out is then a no-op: music under an open pause menu until
     * it is closed.
     *
     * So while the menu holds its latch we keep ours set and do nothing. The menu clears the
     * latch itself when it closes, and the first foreground frame after that finds paused == 0
     * and takes the harmless branch below. */
    if (sys_pause != 0) {
        return false;
    }

    if (paused == 0) {
        /* The menu closed and its own resume broadcast already cleared the latch. There is
         * nothing to resume; the only thing left to do is stop believing we hold a pause. */
        state.we_paused = false;
        return false;
    }

    state.resume_music();
    state.we_paused = false;
    log_transition(MUSIC_ATTACHED, 0, sys_pause, foreground,
                   "resumed because the game is in front again");
    return true;
}

/* The guard half. Only ever RESUMES, and only a pause with no owner. */
static void service_orphan_guard(int32_t paused, int32_t sys_pause, bool foreground)
{
    char cues[96];

    /* Without the pause menu's latch there is no way to tell an owned pause from an orphan, and
     * install() has already switched the guard off in that case. */
    if (!config.resume_orphaned_pause || state.sites.sys_pause_on == NULL) {
        return;
    }

    if (paused != 1 || state.we_paused || !foreground || sys_pause != 0) {
        state.orphan_frames = 0;
        return;
    }

    state.orphan_frames++;
    if (state.orphan_frames < config.orphan_grace_frames) {
        return;
    }
    state.orphan_frames = 0;
    state.orphans_repaired++;

    state.resume_music();

    describe_cues(cues, sizeof(cues));
    log_warning("the music was paused with nobody holding the pause, the pause menu is not up, "
                "the game is in front and this DLL did not pause it. Resumed (repair #%d)%s. "
                "This is the state in which the streaming buffer loops its last second forever, "
                "so if the music was stuck before this line, this is why.",
                (int)state.orphans_repaired, cues);
}

static void imuse_fix_frame(void)
{
    int32_t attached;
    int32_t paused;
    int32_t sys_pause;
    bool    foreground;

    /* The heartbeat watch is independent of the pause half and runs even when that half refused
     * to arm: what it measures lives in the music DLL, not in the engine. */
    music_probe_frame();

    if (!state.active) {
        return;
    }

    attached = read_cell(state.sites.attached, 0);
    if (attached != MUSIC_ATTACHED) {
        /* Music is off, or was never brought up. Both engine functions this file calls would go
         * into IMUSE.DLL regardless, so nothing is called and nothing is remembered.
         *
         * Dropping the belief here is load-bearing rather than tidy: a detach RESUMES before it
         * stops the DLL, so the latch this file was tracking is gone. Keeping the belief across a
         * detach would block the next focus pause and leave music playing in the background for
         * the rest of the session. The one case it cannot cover is a detach and a re-attach that
         * both complete inside a single frame, where this branch is never observed. */
        if (state.we_paused) {
            state.we_paused = false;
        }
        state.orphan_frames = 0;
        note_state(attached, read_cell(state.sites.paused, 0),
                   read_cell(state.sites.sys_pause_on, 0), foreground_belongs_to_us(),
                   "system is not attached");
        return;
    }

    paused = read_cell(state.sites.paused, 0);
    sys_pause = read_cell(state.sites.sys_pause_on, 0);
    foreground = foreground_belongs_to_us();

    note_state(attached, paused, sys_pause, foreground, "state changed");

    if (service_focus(paused, sys_pause, foreground)) {
        return;
    }

    service_orphan_guard(paused, sys_pause, foreground);
}

/* ============================================================================================ */
static void describe_installation(void)
{
    log_info("music service watch is live: pause on focus loss %s, orphan guard %s (grace %d "
             "frames), transition log %s. The engine itself never pauses music on a focus change "
             "- its WM_ACTIVATEAPP handler only captures and releases the mouse pointer.",
             config.pause_on_focus_loss ? "ON" : "off",
             config.resume_orphaned_pause && state.sites.sys_pause_on != NULL ? "ON" : "off",
             (int)config.orphan_grace_frames,
             config.log_transitions ? "ON" : "off");
}

void imuse_fix_install(void)
{
    if (state.installed) {
        return;
    }
    state.installed = true;
    state.process_id = GetCurrentProcessId();

    log_init("imuse_fix", false);

    /* EVERY feature DLL has to do this for itself. common/ is a STATIC library, so each DLL
     * carries its own copy of the host-image state, and the loader having resolved it does not
     * help anyone here. Without it host_image_text() is 0, the signature scanner searches an
     * empty range, and all five patterns come back with zero matches, which reads in the log
     * exactly like an unsupported build. */
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, the music service watch stays OFF");
        return;
    }

    load_configuration();

    if (!config.enabled) {
        log_info("Enabled=0, the music keeps the behaviour the game shipped with: it plays on "
                 "while the game is in the background, and a pause that is never resumed stays "
                 "unresumed");
        return;
    }

    /* The heartbeat watch reads the MUSIC DLL and the pause half reads the ENGINE, so they can
     * arm and fail independently and neither is a precondition for the other. The probe is armed
     * first because it is the one being used to chase an open defect. */
    state.probe_active = music_probe_install();

    if (config.pause_on_focus_loss || config.resume_orphaned_pause || config.log_transitions) {
        if (music_sites_resolve(&state.sites)) {
            if (config.resume_orphaned_pause && state.sites.sys_pause_on == NULL) {
                config.resume_orphaned_pause = false;
            }
            state.pause_music = (music_latch_fn_t)state.sites.pause_function;
            state.resume_music = (music_latch_fn_t)state.sites.resume_function;
            state.active = true;
        }
        /* music_sites_resolve has already said which site failed. */
    }

    if (!state.active && !state.probe_active) {
        log_info("nothing in this DLL is switched on, or nothing it needs could be resolved - "
                 "not one byte is touched this session");
        return;
    }

    /* One frame hook drives both halves. render_frameEnd also runs inside the blocking menu
     * loops, which is what makes the orphan guard able to see a pause taken by a menu screen. */
    if (!frame_hook_add(imuse_fix_frame)) {
        log_warning("the per-frame hook could not be installed, nothing in this DLL runs. There "
                    "is no degraded mode worth having: every part of this feature is a decision "
                    "taken from state that only means anything when it is sampled every frame.");
        state.active = false;
        state.probe_active = false;
        return;
    }

    if (state.active) {
        describe_installation();
    }
}
