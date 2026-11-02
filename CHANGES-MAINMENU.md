# Modified files notice (GPL-3.0-only, section 5a)

This is a modified version of [KARMA](https://github.com/eatyourgreensstudio-dotcom/karma-fm1)
(itself based on Felucca), free firmware for the M-VAVE FM-1, by Leo Kuroshita (@kurogedelic) /
Hügelton Instruments. The modifications below were made starting **2026-11-01** by Hayden
(MOODHAY) in this fork, <https://github.com/mood92/karma-fm1>, and are themselves licensed
GPL-3.0-only, same as the files they change (see `LICENSING.md` for the full licence picture,
including the Felucca Assets' separate terms, which this fork does not change).

The fork is a "Main Menu Live Player": a performance-oriented main menu built around a 20-slot
LIVE setlist, for recalling and storing sounds live on stage. It is not affiliated with, endorsed
by or supported by Hügelton Instruments, M-VAVE, or any of the third-party projects credited in
`LICENSING.md`.

## What changed, by area

- **The LIVE layer (new):** holding the LFO button opens a 20-slot setlist across the white and
  black keys. Each slot remembers a sound and the lane it belongs to; recalling one jumps to that
  lane first, then loads the sound there. MIDI Program Change (0..19) recalls a slot from any
  track's channel. STORE (OCT+ tapped, then a slot key; or held OCT+) snapshots the lane's sound as
  it is now — knob, FX and envelope tweaks included — into a reserved user preset slot, so the
  setlist holds a real sound, not just a reference to an unmodified preset. A one-finger lock
  (holding LFO about 0.8 s) keeps the layer open without a second hand. The setlist persists in the
  settings object alongside the existing MIDI part.
- **A hidden fourth (drum) track:** the main menu and this performance layer are built around the
  three synth lanes; the drum track's engine is unchanged underneath, but it is no longer reachable
  from the UI (track select, the mixer, the knob band, Program Change) and never sounds through the
  mix, so it cannot be surfaced by accident while performing. The metronome click is unaffected.
- **Visual rework:** the boot screen, header and track bar were changed to the Main Menu's own
  mark and a three-lane layout; new lane characters and a knob-touch pop-up (name, value and a
  gauge) replace the prior KARMA-branded art in the areas this fork touches.
- **Four screens (Nov 2 2026):** the instrument is SOUND (HOME), BROWSE (SAVE), SETLIST (SEQ) and LIVE (LFO).
  The step sequencer's pages and generators, the steps / roll / song layers, punch-in effects and the last
  of the drum UI were removed from the panel; the looper (REC / PLAY) stays. SETLIST edits the 20 slots as a
  list (play, move, store, clear) and BROWSE's KNOB 4 puts a sound into a slot. See docs/REDESIGN-PLAN.md.
- **Firmware identity:** the version string reported to the editor, console and splash screen
  (`FELUCCA_VERSION`) is set at build time to identify this fork rather than KARMA's own version.
- **Host test suite:** test-infrastructure fixes (a field-name bug, drum-level defaults after this
  fork's changes, a couple of pre-existing heap-overflow bugs in the shared host harness) and new
  tests covering the LIVE setlist's behaviour and its flash persistence.

## Files touched

See `git log 7d2560b..HEAD` in this repository for the exact commit-by-commit history (base commit
`7d2560b` is the last unmodified KARMA commit this fork started from). The files with changes as of
this notice: `firmware/src/ui.c`, `ui_input.c`, `ui_layers.c`, `ui_draw.c`, `ui_vis.c`, `ui_op.c`,
`ui_op.c`, `drums.c`, `project.c`, `params.c`, `upreset.c`, `felucca.c`; `tools/build.py`; and the host test
suite under `tests/` (`dsyn_test.c`, `studio_drums_test.c`, `drum_send_test.c`, `seq2_test.c`,
`userkit_test.c`, `fx251_test.c`, `regress.c`, `hostsim.c`, `ui_pages_test.c`, and the new
`live_persist_test.c` and `shots_test.c`).

This notice is kept up to date as the fork continues; it does not attempt to list every future
change individually once a change is reflected here in substance.
