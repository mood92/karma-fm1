# SEQ-SONG: chance, variation and song building (branch `seq-song`)

Built on stock KARMA 3.0.3. Notes stay locked to the grid; the groove comes from velocity and chance only.

## SEQ > VARY and VARY 2 (per selected track)
| Knob | What it does |
|---|---|
| GHOST / SOFT / HARD | chance (0..100 %) that a hit of that level plays, e.g. ghost notes at 30 % |
| AMOUNT | how much of all the chances is heard; 0 = every hit plays (tight) |
| VEL RND | a hit's velocity moves by up to +- this |
| RATCHET | chance that each further ratchet hit plays |
| LOOK | LIVE: new every pass. HOLD: the same every pass for this SEED |
| SEED | which held look (0..99) |

* A synth step has its own chance: hold the step and turn SELECT (a "?" marks the tile). Drums use the level chances.
* Chance applies to MIDI out too. While recording, nothing is dropped.
* The VARY settings are saved with the settings (not in a project).

## SONG screen
* K1 entry, K2 section (A-D), K3 bars (0 = LOOP), K4 length.
* ALGO picks a track, SELECT mutes/unmutes it for this entry only (red mark; audio and MIDI). PRESET: turn right to duplicate the entry, left to delete it. Stopped only.
* Entry mutes are packed in the scene byte (bits 2..5), so saved songs keep loading.

## Already in stock KARMA (not rebuilt)
64-step patterns with page tabbing, copy/paste, undo, follow playhead, queued section switching, song chains.

## Limits
4 sections, 16 entries (flash and RAM budget); drum chance is by level, not per hit.
