# gow3 runtime (from gow3_android)

`src/` is wyatt-poole/gow3_android `src/` at commit 5c2219dcf852e4ae010d12a7402d48d004f144fe
(2026-10-17), GPL-2.0-or-later (`LICENSE`). The first commit that adds this directory is an
unmodified copy; the iOS changes come after it as ordinary commits, mostly `#ifdef __APPLE__`
sections next to the existing `_WIN32` ones, so that upstream changes can still be merged.
