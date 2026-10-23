# Toggle files for the texture corruption (0.1.25)

Copy one of these to `Documents/gow3/toggles.txt` on the phone (Files: On My iPhone > gow3-ios > gow3,
then rename it to `toggles.txt`). The game re-reads it every 250 ms, so a file can be swapped while
playing; the log prints `Runtime: disabled optimizations mask=<n>` on every change.

| File | Switches off |
|---|---|
| `toggles-all-off.txt` | all three groups below (slow; only whether the textures come right counts) |
| `toggles-textures.txt` | texture caches: descriptor cache (32), lock-free upload check (64), image lookup cache (128), binding memo (1024), lock-free UpdateImage (1<<30), view memo (1<<33), bind helper (1<<34), texture set memo (1<<45), partial height views (1<<58) |
| `toggles-tracking.txt` | page tracking shortcuts: region cache (1), early exit (4), page-granular read tracking (2048), 256 KiB fault window (65536) |
| `toggles-pipeline.txt` | threads and pipelining: threaded recording (16), recording-thread uploads (256), barrier memo (512), draw-preparation workers (4096, 8192), recording-thread copies (16384), bits 35-44 and 46 of gow3_toggles.h |
| `toggles-none.txt` | nothing (0): back to normal |

Order: `all-off` first. If the textures come right, try the three groups one at a time to find which
one holds the bug, then single bits inside it.
