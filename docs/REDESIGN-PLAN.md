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
- Punch-in effects — see open question 3 below on exactly what this means.

## Open questions (need your call before I start cutting code)

1. **Arpeggiator (ARP, ARP 2 pages)** — keep or cut? Not mentioned in the mockup or your
   notes either way.
2. **Scale/chord page (SEL, SEL 2: root, scale, quantize, chord, transpose, strum,
   voice-leading)** — keep or cut? This is what keeps notes in-key and voices chords while you
   play live, so it might be worth keeping even in a stripped-down instrument.
3. **"Punch-in effects"** — do you mean the INSERT / INSERT 2 pages specifically (a
   switchable effect you punch in live), or the whole FX family including the DIST/CHORUS/
   DELAY/REVERB sends? You did ask for Juno chorus, delay and reverb specifically, so my
   assumption is: keep those, cut INSERT. Confirm?
4. **Sub-style tags** — "deep house," "jazzy house," "sub bass," "stand-up bass," etc. don't
   exist anywhere in the current preset data; only the broad kind (BASS, LEAD, ...) does.
   Tagging all ~150 factory presets by sub-style by hand is a lot of guessing on my part. Two
   options: (a) I tag them as best I can from the sound/name and you correct the ones that are
   wrong once you can hear them in BROWSE, or (b) we cut the factory list down to a smaller,
   curated set you actually want, each one tagged properly from the start. Which do you want?

## Rough phase order (once the above is answered)

1. Strip what's being removed (sequencer, punch FX, drum UI) — mechanical, and shrinks the
   page table before the navigation rework so there's less to carry through it.
2. Collapse navigation down to the four screens (LIVE / SOUND / BROWSE / SETLIST).
3. Build BROWSE (list, category tabs, favorite star, audition-on-scroll).
4. Build/finish SETLIST as its own reorder screen.
5. Visual pass so all four screens read as one consistent app, matching the mockup's layout
   within the existing pixel-font rendering.

This is a genuine rebuild, not a polish pass — expect it to take a lot more than the last
round of fixes. Pushing and testing in the browser emulator after each phase, same as before.
