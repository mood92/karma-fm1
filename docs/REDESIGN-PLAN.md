# Main Menu Live Player — redesign plan (browse-first UI)

## Why this exists

The current firmware is still KARMA's own navigation: cycle through ~40 parameter pages
(ENV, LFO, FX, SEQ, GLO, ...) with the ALGORITHM knob, scroll one preset at a time with the
PRESETS knob. What Hayden actually asked for, going back to the original design chat, is
different: a **browse-first instrument** — open it, see a categorized list of sounds (EP,
chords, stabs, leads, bass, organ, each with a sub-style like "deep house" or "jazzy house"),
pick one, drop it on a lane, loop it live. KARMA was meant to supply the *look* (pixel
characters, the knob pop-up, the screen style) and some usable DSP engines, not its own menu
structure. That browsing screen has never been built; everything so far has been fixes and
trims on top of KARMA's native page-cycling UI. This plan is the actual rebuild.

## The four screens (from the mockup)

1. **LIVE** — the setlist grid (already mostly built this session: 20 slots, keys = slots,
   MIDI Program Change recall, STORE snapshots live tweaks). Needs its home screen reworked to
   match the mockup: "NOW PLAYING" name + character + PC number, not a bare page title.
2. **SOUND** — one trimmed screen: last note played, 4 knobs (their names depend on the
   engine), ENV / FX / LIVE as the only sub-tabs. Replaces the current maze of ENV / ENV DEST /
   LFO / LFO 2 / LFO DEST / FX / FILTER / EDIT 1 / EDIT 2 / VOICE / VOICE 2 pages with one
   screen and a small number of knob pages reachable from it.
3. **BROWSE** — new. A scrollable list of sounds, category tabs across the top (ALL / EP /
   POLY / BASS / STAB / LEAD / FX, matching the existing `BK_*` kinds already in `ui.c`'s
   `BANK[]`), a star to favorite, KEYS = audition (play it as you scroll), LIVE = SET (send the
   selected sound into the current LIVE slot). This is the screen that doesn't exist yet.
4. **SETLIST** — new. Edit the 20-slot setlist directly as a list (not live, one slot at a
   time): reorder, see each slot's category tag, jump to BROWSE to replace one.

## What's reused almost as-is

- The pixel character per engine (`op_scene_*` in `ui_op.c`), the knob touch pop-up
  (`pop_draw`/`knob_say`), the five DSP engines (ACID, JUNO, ELEMENTS, PLAITS, PRISM).
- The LIVE setlist machinery from this session's work: `setlist_id`/`setlist_lane`,
  `live_key()`'s STORE/RECALL (snapshots tweaks into a reserved user-preset slot), MIDI Program
  Change recall. This is the backbone LIVE and SETLIST both sit on.
- The categorized preset table already in `ui.c`: `enum { BK_BASS, BK_KEYS, BK_ORGAN, BK_PAD,
  BK_LEAD, BK_PLUCK, BK_STAB, BK_FX }` and the ~150-entry `BANK[]` array with a kind per sound
  (some already house-flavored: "HOUSE STAB", "RAVE 5TH"). This is the taxonomy backbone for
  BROWSE; it has no sub-style tag (deep house / jazzy house / etc.) yet — see open questions.

## What gets removed

- The whole step sequencer: STEP, PATTERN, PLAY, ACID GEN (1/2), GEN (1/2), HARMONY, DRUM GEN
  (1/2), SONG — the entire `FAM_SEQ` family, ~12 pages and the generator/arranger logic behind
  them.
- The drum track and HEX drum synth UI (already hidden from navigation this session; this
  phase can go further and drop the engine/pages outright).
- INSERT / INSERT 2 ("punch-in" effects) only — the DIST/CHORUS/DELAY/REVERB sends stay (Juno
  chorus, delay and reverb were explicitly asked for).

## Decided (Nov 1 2026)

1. **Arpeggiator** — keep, as one of SOUND's sub-pages.
2. **Scale/chord page** — keep. Also: Hayden plans to run this same firmware on the SMK-37
   Elite (same chip, has pads), and wants a future **chord-capture mode**: hold a chord on the
   keys, it's captured, then assigned to one of the SMK-37's pads for one-tap chord playback
   (the kind of chord/scale-assist some newer Ableton-adjacent keyboards do). This is an SMK-37
   phase feature, not part of this FM-1 BROWSE rebuild — noted here so it isn't lost, revisit
   once SMK-37 work starts.
3. **"Punch-in effects"** = INSERT / INSERT 2 pages specifically. Confirmed, see above.
4. **Preset tagging** — I tag all ~150 factory presets by sub-style as best I can from the
   name/sound; Hayden corrects the wrong ones once he can hear them in BROWSE.

## Status (Nov 2 2026)

Phases 1-5 are done (commits after `5399486`):

- **Removed:** the sequencer pages and generators (STEP .. SONG), the steps / roll / song layers, punch-in
  (FX held, INSERT pages), and the remaining drum UI (HEX pages, GLO > DRUMS, TRACKS > DRUMS). The engine
  underneath stays: the looper (REC / PLAY), projects and the editor still use it.
- **Buttons:** HOME = SOUND (again: TRACKS, the lanes' loops; again: the visualiser), SAVE = BROWSE (then
  USER, PROJECT, TOOLS), SEQ = SETLIST, LFO = LIVE (a tap keeps it open). ENV holds the LFO pages now. FX,
  EDIT, ARP, SEL, GLO open their pages; EDIT held erases, SEL held sets the key, GLO held is the mixer.
- **SOUND:** the lane's sound large, its kind, the last note, the setlist slot, the lane's character, the
  sound's four main knobs.
- **BROWSE:** KNOB 1 the list, 2 the tab, 3 favourite, 4 SET (into SETLIST's slot); PRESETS scrolls too.
- **SETLIST:** KNOB 1 slot (plays it), 2 move, 3 store the lane's sound, 4 clear (twice); PRESETS = KNOB 1.
- **LIVE:** the slot playing and its Program Change number in the header.

Still open: sub-style tags (deep house / jazzy house) on the presets, and the SMK-37 chord-capture mode.

## Rough phase order

1. Strip what's being removed (sequencer, punch FX, drum UI) — mechanical, and shrinks the
   page table before the navigation rework so there's less to carry through it.
2. Collapse navigation down to the four screens (LIVE / SOUND / BROWSE / SETLIST).
3. Build BROWSE (list, category tabs, favorite star, audition-on-scroll).
4. Build/finish SETLIST as its own reorder screen.
5. Visual pass so all four screens read as one consistent app, matching the mockup's layout
   within the existing pixel-font rendering.

This is a genuine rebuild, not a polish pass — expect it to take a lot more than the last
round of fixes. Pushing and testing in the browser emulator after each phase, same as before.
