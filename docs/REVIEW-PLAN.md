# Main Menu Live Player: review findings and fix plan

Review of commits 063c6cd..54806ce (everything after the CI workflow), done 2026-11-01.
Work through the phases in order. Each step says what is wrong, where, and what "done" means.

## Phase 0: set up the safety net first (do this before any code change)

**0.1 Run the host tests and screen renders locally, every time.** The repo has a full host test suite
(`tests/run_tests.sh`) that compiles the firmware for the PC, fuzzes the UI, checks project and settings formats,
and **renders every screen to images** (`build/host/*.ppm`). It was never run during this work, so every UI change
was made without seeing it. It runs in the Claude workspace:

```sh
cd /home/claude/karma-fm1
python3 -c "import sys; sys.path.insert(0,'tools'); import build; build.generate()"   # generated headers, once
mkdir -p build && touch build/felucca.fwsc                                             # the suite only checks it exists
sed 's/-lc++/-lstdc++/' tests/run_tests.sh > build/run_tests_local.sh
sed -i 's#cd "$(dirname "$0")/.."#cd /home/claude/karma-fm1#' build/run_tests_local.sh
sh build/run_tests_local.sh
# single suites, faster:
cc -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o build/host/ui_pages_test tests/ui_pages_test.c build/host/libplx.a -lstdc++ -lm && build/host/ui_pages_test build/host
cc -O1 -w -Ibuild/gen -Ifirmware/src -o build/host/project_test tests/project_test.c build/host/libplx.a -lstdc++ -lm && build/host/project_test
```

Convert any `.ppm` to `.png` with Pillow and **look at it** before pushing a UI change.

Current state: `project_test` passes; `ui_pages_test` runs clean under AddressSanitizer. Failures today are
(a) the drum tests and the drum-page UI tests, an expected result of hiding the drum track (see 1.4), and (b) an
upstream compile error in `tests/dsyn_test.c` (`decay` vs `dec`). That one is not ours, but it stops the script
before `ui_pages_test`, `project_test` and the soak test run.

**0.2 Make the suite green and meaningful.** Fix the `dsyn_test.c` field name (`decay` -> `dec`). Update the
drum assertions in `song_audio_test.c`, `studio_drums_test.c`, `drum_send_test.c`, `seq2_test.c`,
`userkit_test.c` and the DRUMS cases in `ui_pages_test.c`. Either set `song.g[G_DRLVL]` and select the drum
track directly inside those tests (they exercise the engine, which still exists), or skip them with a clear
`/* MAIN MENU: drum track hidden */` note. Done = `sh build/run_tests_local.sh` ends with no FAIL.

**0.3 Run the tests in CI.** Add a step after "Build" in `.github/workflows/build.yml`:
`sudo apt-get install -y libc++-dev libc++abi-dev` then `sh tests/run_tests.sh`. A red test fails the build,
so no broken package gets published.

**0.4 Add a Live-layer test** to `tests/ui_pages_test.c`, using its existing helpers (hold a button, press a key,
`frame()`, `ppm()`). It should cover:
- hold LFO, press white key 3, and the lane loads the slot-3 sound;
- one-finger lock (hold LFO for 800 ms, release, and it stays locked);
- OCT+ tap then a key stores the slot;
- Program Change 5 recalls slot 5;
- the setlist survives `settings_save()` followed by `dsu_boot()`;
- render `layer-live.ppm`.

## Phase 1: correctness bugs (fix before the first hardware flash)

**1.1 HIGH: setlist slots point at the wrong sound on other lanes.** `setlist[]` stores a position in the
*selected lane's* PRESETS list (`preset_pos`, `pl_entry`). Each lane only allows certain engines
(`engines.c eng_allowed`: lane 1 = ACID + JUNO, lane 2 = ELEMENTS only, lane 3 = PRISM only). So position 7 is a
different sound on each lane. Store on lane 2, recall on lane 1, and you get an unrelated sound. Program Change
has the same problem. Saving a new user preset can also shift positions.
Fix: store a **stable sound id** (`pl_entry` result: BANK index < NBANK, or NBANK + user slot) and the **lane it
belongs to** (`eng_allowed`). On recall (key or PC), `track_select(lane)` then load that sound by finding its
position in that lane's list. Settings part: 2 bytes per slot (id, lane), with 0xFF = empty. Bump `LIVE_PART`
to 44 and accept both 24- and 44-byte parts in `dsu_boot`. Show the lane in the tile (colour bar = lane colour).

**1.2 HIGH: storing a slot does not keep your tweaks.** A slot stores only which preset it is. Knob changes,
FX sends and ADSR are lost on recall, but the agreed spec was that SAVE keeps them. Fix: on store, save the
lane's current sound as a **user preset** (`upreset.c`, 32 slots) and point the slot at it. Reserve user slots
13..32 for the setlist, or ask Hayden whether a plain preset reference is enough. Use `up_save`/`up_ui` the way
the USER page does, and write to flash only when the transport is stopped, as settings do.

**1.3 MED: store stays armed after leaving Live.** `live_store` (ui.c) is never cleared when the layer closes.
The next visit's first slot press then silently overwrites that slot. Clear it whenever `ui.layer` leaves
`LY_LIVE` or the lock is released (`layer_unlock`, and the `held == LY_PLAY` path in `layers_input`). Also
give the armed state a timeout of about 5 s.

**1.4 MED: the "hidden" drum track still plays and can still be reached.**
- `G_DRLVL` default 0 only applies to new projects. Saved projects and autosave restore their own level
  (`proj_apply` sets it on every load *and* every section). Force `song.g[G_DRLVL] = 0` after `proj_apply`, or
  better, skip the drum track in the sequencer/render when a `MAIN_MENU_NO_DRUMS` flag is set.
- MIDI channel 10 still plays GM drums (`G_DRCH` default 10). Set the default to 0 (OFF).
- GLO layer (`ui_layers.c` `layer_key` LY_MIX): white key 4 mutes the drum track and **key 8 solos it, which
  silences all three lanes**. Knob 4 raises the drum level (`layer_knobs` LY_MIX `k == TRK_DRUM`). Ignore
  `w == 3`, `w == 7` and knob 4.
- `song.sel` can come back as 3 (the drum track) from a saved project (`project.c` lines 706 and 800:
  `p->sel < NTRK`). Change both to `< NPART`.

**1.5 MED: the mixer still says SYN1 / SYN2 / SYN3 / DRUM** and shows 4 mute and 4 solo keys
(`ui_layers.c` LY_MIX draw, `ui_op.c` mixer keys row). Rename to LANE 1-3, drop the 4th column, and blank keys
4 and 8 (see render `build/host/layer-mix.ppm`).

**1.6 LOW: race car turns around on every other note.** The lane-1 scene still flips the sprite like the sumo
did (`ui_op.c op_scene_sumo`: `op_sprite(..., lean && side < 0, ...)`). Pass `0` for flip.

**1.7 LOW: storing when the current sound matches no list entry stores sound 1.** `preset_pos` returns 0 when
nothing matches (an engine switched by hand, say). Detect "no match" and refuse with "SAVE AS PRESET FIRST",
or let 1.2 make this go away.

**1.8 LOW: boot screen still draws 4 coloured bars** (`ui_vis.c` boot_draw, `x < 4u`). Make it 3.

**1.9 LOW: Program Change message** says `T1 PROG n` with the internal list number. Show `SLOT n  NAME`.
`live_slot` is global, so make it per lane (`live_slot[NPART]`) so the blinking key matches the lane shown.

## Phase 2: identity, licence, release hygiene

**2.1 The firmware still calls itself KARMA.** The version string (`FELUCCA_VERSION`, used by the web editor
`editor.c`, the console and `splash.c`) and the package identity (`tools/build.py PRODUCT = "FM-1_926"`) are
Karma's. Installers and the Firmware Hub may show it as Karma, or offer to "update" it back. Set
`-DFELUCCA_VERSION="MAIN MENU LP 0.1"` in `tools/build.py`. Leave `PRODUCT` alone until you've checked what
M-UPGRADE and the web installer key on (`tests/ota_test.c`, `tests/ldr_test.c`).

**2.2 GPL-3 notices.** Section 5a requires modified files to carry a prominent notice that they were changed,
with a date. Add a `CHANGES-MAINMENU.md` and a line at the top of README ("Main Menu Live Player, a modified
version of KARMA by Ariel Muchnik / SLOOP / Felucca, changed 2026-11"). Keep LICENSE and the author credits.

**2.3 The Main Menu logo is in a public GPL repo.** `assets/logo/mainmenu-line.png` and the generated
`mm_logo.h` are now public and sit next to GPL code. Add a note in `LICENSING.md` that the Main Menu marks are
trademarks of Main Menu Records and not covered by the GPL. Alternatively, make the fork private (fine for
personal use; the GPL only requires source when you distribute).

**2.4 Tag a known-good build** before every hardware flash (`git tag lp-0.1-emu-ok`). Tell Hayden the rescue
path: hold OCT- at power-on for USB recovery, and use the installer's "Return to official V15".

## Phase 3: design limits Hayden should decide on (not bugs)

- **Lanes are tied to engines.** Lane 1 can only play ACID/JUNO sounds, lane 2 only ELEMENTS, lane 3 only
  PRISM (`eng_allowed`). "Any sound on any lane" isn't possible today. A path to try: the repo's
  `tests/arena_test.c` checks every engine against every part's memory. Widen `eng_allowed` per lane, run
  `arena_test` and the soak test, and keep only the combinations that fit. With 1.1 done, the setlist hides
  most of this anyway (a slot jumps to the right lane).
- **3 lanes, not 4** (memory and CPU per part). Already told Hayden.
- Still to build: loop-count conditions ("every 2nd pass"; the hook is `step_plays()` in seq.c, and `t->pass`
  already counts loop passes; store per lane next to `pl_chance`), lane controls inside Live, the setlist and
  sound screens from the mockup, the colour logo on a settings screen, and the knob readout on the STEP and
  TRACKS pages.

## Order of work for the next session
0.1 -> 0.2 -> 0.3 -> 0.4 -> 1.4 -> 1.3 -> 1.1 -> 1.2 (ask Hayden first) -> 1.5 -> 1.6..1.9 -> 2.1 -> 2.2 -> 2.3
-> 2.4. Push after each green local run, and send Hayden one build at the end.
