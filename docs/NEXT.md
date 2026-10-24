# Next tasks (handover, 2026-10-23)

**Latest: docs/STATUS-2026-10-23.md** (findings and next moves for 0.1.23-0.1.27).

**0.1.28 on the device (2026-10-24):** push descriptors off for pipelines (0.1.27) did not fix the textures
and cost FPS (4-12 vs ~27), so `env.txt` has `GOW3_PUSH_DESCRIPTORS=1` again. Captures of a good and a
broken scene in one run: the 1x1 exposure pass is the same in both, and the 64-write-fault safety net fired
14 times before the good capture, so neither is the cause. The broken scene samples 16 2048x2048 BC
textures (good: 4). `GOW3_GPU_PROFILE=2` crashed in Apple's driver on every start, and one cached pipeline
failed to build in the warm-up (`Cannot convert attribute from MTLAttributeFormatFloat4 to int4`); the
pipeline cache was deleted on the phone (copy kept on the PC).

**0.1.29 on the device:** with the renderer passes on descriptor sets the textures stayed right for a long
time and then broke mid-fight (some places were bad from the start); with the pipelines on sets too it broke
sooner, with wrong textures on objects. A capture of a broken frame: every BC texture is tile mode 13 /
array mode 2 (1D thin, micro tiled), as in good frames, so the detiler's math is not it. Suspect: something
reused before the GPU is done with it (descriptor sets, staging, stream buffer). The fixed profiler works:
in fights 25-27 ms of GPU time per frame went to 4-5 passes with no color or depth target (render area
32768x32768, vs ccdebd802401e5ac, no pixel shader).

**0.1.30 on the device:** locked 30 FPS up to ~1000 draws/frame with GPU ticks near 0 % (was 10-12 FPS);
the heaviest scenes (2000+ draws) run ~16 FPS. Textures still break over time. `GOW3_SUBMIT_WAIT=1` did not
help (broke fast), so reuse before the GPU finished is ruled out.

**0.1.31 on the device:** no `Alias copy:` line in a broken scene, so the game makes no mixed
compressed/uncompressed alias copies there. Also ruled out on 0.1.30/0.1.31: resolution (600p, 720p and 1080p
all break), the Video Arena (the 600p patch sets 2.25 GiB; DMEM is 5056 MiB), `GOW3_READBACKS=2` (no change;
readback waits are 0 ms).

**0.1.32 on the device (broken scene loaded from a save):** `Texture verify` checked ~980 textures (650 MB)
with 0 stale, and all uploads came from guest memory (0 from the GPU buffer): no missed invalidation, the
16 KiB tracker is not the cause. Live `toggles.txt` bisect: switching off the descriptor/view/binding/
image-lookup memos, partial-height views and render-state memo (mask 288283178479649952) changed nothing,
nor did the draw pipeline group (threaded recording, draw preparation, prepared resources, constant ring,
pipelined work; mask 87930865463312). The scene uploads 0 MB per frame while broken, so the bad data sits in
GPU images. Not checked yet: GPU-written textures (verify skips them). Side note: gow3_android runs FEX with
all TSO options off (`android/runtime/fex-config.json`); iOS uses FEX defaults (TSO on), a CPU cost to revisit.
Also seen: `MarkAsMaybeDirty` seeds `image.hash` over the whole image but `RefreshImage` compares a hash of
the first 8x8 texels, and 16 KiB pages put many more textures on that path than 4 KiB.

**0.1.33 on the device:** `reupload.txt` uploaded 1054 textures (744 MB) again in the broken scene and nothing
changed; 542 GPU-written textures (335 MB) were skipped. The re-upload goes through the same staging copy and
detiler, so a detiler that is wrong for some layouts every time is not ruled out.

**0.1.34 on the device: the corruption is in guest memory.** A dump of the 175 textures sampled in a broken scene
(`native/scripts/dumpcheck.py`: detile + BC decode on the PC) shows all 144 BC textures byte-identical between the Vulkan image and
guest memory, and many of them visibly broken in memory itself: regular stripes, bands of other data, rainbow
noise. Upload, detiling, binding and sampling are correct; the game's own CPU code wrote bad texture data.

**0.1.35 on the device:** `FEX host features: MOPS 0 CSSC 1 FlagM2 1 FRINTTS 1 RPRES 1 AFP 0 RCPC2 1 AVX 1`. The
A19 has no FEAT_MOPS, so the MOPS path never ran and the textures are still broken. FEX-side differences
from gow3_android that remain: TSO emulation on (Android: all TSO options off, `fex-config.json`), and the
A19's CSSC/RPRES/FlagM2/FRINTTS/RCPC2. Env tests without a build: Android's TSO settings, `GOW3_FEX_AVX=0`.

**FEX versions:** gow3_android pins upstream FEX-2609.1 (9fbdc00); we use willfaust/FEX ios-port-2607 =
FEX-2607 (1cc4b93, 2026-07-02) + 110 iOS commits, missing 174 upstream FEXCore commits. Tests on 0.1.35:
Android's TSO settings (all off) crash at once (a jump to float data, 0x3f8000003f1ce73a);
strict TSO (vector + memcpy/set TSO on) changes nothing; `GOW3_FEX_AVX=0` crashes on the game's first
AVX instruction (the game needs AVX).

**FEX translates the tiler correctly (Linux test, 2026-10-24):** the eboot's own Tiler1d (constructor
0x241a20, `tileSurfaceRegion` 0x241f10) run natively on x86-64 and under our FEX (ios-port-2607 + patches
0001-0003, built for aarch64 Linux, `qemu-aarch64 -cpu max,sve=off,sme=off`) gives byte-identical output in
210 tests (BC1/BC3/R8/RG8/RGBA8/RGBA16/RGBA32, 4x4 to 2048x2048, full/aligned/unaligned regions), all equal
to the reference 1D-thin tiler. Single-threaded, 4 KiB pages: not iOS memory or concurrency. Also seen in
the 0.1.38 log: `FEX E: Failed to mprotect last page of code buffer` x702 (a 4 KiB guard page cannot be set
on 16 KiB pages; the JIT's buffers are sized with margin, so probably harmless).

**Video Arena test (0.1.38, no build):** the game's own texture corruption (the PC port's "Texture Corruption
Fix") comes from a too small Video Arena; the phone already runs it at 2.25 GiB (600p patch, 0x3cd918 =
0x90000000; direct mapping 2.39 GiB). Raised to 3.25 GiB (0xd0000000; mapping 3.39 GiB confirmed in the log):
textures broken the same. Not arena pressure.

**FPS (2026-10-24, Hades save, 600p).** No-build tests on 0.1.40: gow3.ini fps_limit 30 -> 60, `GOW3_UPSCALER=off`,
`GOW3_OBJECT_MOTION=0`: menus 30 -> 60, light gameplay 30 -> 41-42, heavy 10-22 -> 17-21. The 32768x32768
attachment-less passes (~19 ms of 35 ms GPU) were object motion's vertex stores and are gone. `GOW3_FRAMES_AHEAD=2`
changed nothing. `GOW3_PERF_DIAG=1` then showed the heavy-scene limit: "Readback source cpu-write: 82 waits,
3828 ms, max 57.8 ms; byte rule: 0 CPU writes went on without waiting" (texture_cache.cpp:157). The game writes
each frame next to its 8x8/64x32 GPU images (read back), and on iOS OnWriteFault resolves readbacks for the whole
16K page, so the stale-mode byte rule never applied and each store waited a whole GPU frame (~47 ms). 0.1.42:
OnWriteFault resolves readbacks for the 8 written bytes first; iOS defaults fps_limit 60, upscaler off,
object motion off.
Crash seen twice (one before the readback fix): guest SIGSEGV at 0x199239 reporting address 0, in a loop that
atomically sums counters of valid objects (rdi valid) -- looks like a JIT/translation fault, not a game null
pointer. Not investigated yet.
**0.1.42 on the device:** the cpu-write waits are gone (texture_cache.cpp:157: 1 wait, 2.8 ms; "byte rule:
148-163 CPU writes went on without waiting"). Gameplay 29-41 FPS (heavy was 15-21), menus 60; now GPU-bound
(GPU ticks 75-82 %, 83-109 render passes/frame, up to 141 attachment loads). Dips to ~23 FPS while streaming
an area: readback image-free 55 waits / 1779 ms (141 MiB), image-refresh 142 waits / 177 ms.
The 0x199239 crash hit a third time (r15/r12/r13 = 0x70015aa7e0/7e8/7ec, the lock add targets, all valid).
Guess: Darwin left si_addr 0 for a lock add on a GPU-tracked (write-protected) page, so the tracker never saw
the write. 0.1.43: the fault handler takes the address from the exception state's FAR when si_addr is 0, and
the fault report prints host pc, ESR, FAR, si_addr, si_code and the JIT code around pc.

**FIXED (2026-10-24): texture corruption.** 0.1.40 showed every misplaced BC texture under large linear
R32G32B32A32Uint storage images (4096x80..4096x160, the game's GPU texture moves, as SpartanPort #331 says),
with no GPU-modified buffer data there. The PC launcher sets `GOW3_READBACK_LINEAR=1` (run.py) so those
images are read back to guest memory; iOS never set it. With it (plus `GOW3_DEFERRED_READBACK=1`,
`GOW3_STALE_READBACK=1`, the gow3.ini defaults) in env.txt the Hades save renders correctly. 0.1.41 sets
all three by default in Gow3CoreModule.swift. Lesson: diff the PC launcher's environment (run.py,
scripts/graphics_options.py) against ours whenever iOS behaves differently from PC.

**0.1.40 (diagnostics): misplaced textures.** With `GOW3_TILE_CHECK=1`, `FindImage` reports each new BC texture
whose address is inside a tiling but where none started (`runtime_tilecheck_classify` == 2): "Misplaced
texture: ..." with its raw T#, the tiling it lies in, whether the buffer cache holds GPU writes there, and every
cached image over it (format, size, flags, usage). A searcher thread then finds copies of that T# in resident
guest memory (mincore + memmem) and logs 192 bytes around each ("T# of ...: N copies"). With
`GOW3_WRITE_TRACE=1` GPU writes are traced too: buffers bound for shader writes (kind 14) and images bound
as render/depth/storage targets (kind 15). The dump adds the images over misplaced textures. 31 of the 109
misplaced textures in 0.1.39 sit ~128 MiB above a same-shape texture tiled earlier (control offsets: 0-2),
as if the game moved them in its pool and the data never followed.

**0.1.39 on the device (Hades save; dump_0 with 186 textures, tiles.txt, writes.bin):** 10,826 tilings, 0
wrong, 0 sources differing from the file, 0 reads in flight, 0 same file bytes twice. `native/scripts/
tilecheck.py` rebuilds each tiled range from the game file and compares with the dump: 76 BC textures are
exactly the tiled file data. 73 BC textures in use are NOT where their data was ever written: e.g. tex 46,
BC1 2048x2048 at 0x73d58f7000, lies +0x230c00 inside a 4 MiB 128-bpp surface tiled at 0x73d56c6400 (#7877,
HDS175.WAD @0xb3d4bc; memory there is 100% that surface), and over the whole session the only writes into
its range are those tilings (no GPU copy, read, memcpy). No tiling ever targeted any of these addresses, and
no fixed offset links them to same-shape tilings. So the texture data is right; the T# (texture descriptor)
base addresses the GPU uses point where that texture never was. Next: find who writes those T#s.

**Other ports and game files (2026-10-24, no build):** SpartanPort 2.7.0 (PC) still has open, milder
texture corruption after playing a while (#1015 striped floors/walls, #917, #1014); #331 (backing-write
invalidation) is already in our code; #899 was a bad dump. The phone's game files match the user's Windows
copy byte for byte (43 WADs; eboot.bin and both .plt are the 01.02 update). The PC launcher sets
`GOW3_DEFERRED_READBACK=1`/`GOW3_STALE_READBACK=1` (gow3.ini defaults), iOS ran without them: with both on
(log: "deferred image readback enabled"), textures broken the same.

**0.1.39 (diagnostics): tiling check.** `GOW3_TILE_CHECK=1` routes the game's only call of Tiler1d
(0x243bfb) through `runtime_tilecheck_tile1d` (src/tile_check.h, runtime_tilecheck.c), which per call logs to
`user/tiles.txt` (problems also to the loader log): a file read still running into the source; the source
against the game file it was read from (re-read); the same file bytes tiled into a second place; the tiled
result against the source element by element; overlaps with earlier tilings; and a watcher re-hashes every
tiled range every 3 s and reports the first change. Summary line every 12 s ("Tile check: N tilings ...").
Tested end to end on x86-64 with the eboot's real tileSurfaceRegion.

**0.1.38 on the device (Hades save, HDS175/HDS200/HDS300 WADs):** 90,643 records in 109 s (no drops).
Visibly broken textures (e.g. tex 109: a grey band over the top 22% of a roots texture) have NO traced
write: no file read, memcpy/memmove/memset, GPU backing write, readback or DMA copy touched them. The guest
store faults into live textures come from the game's texture tiler: Sony's GPU address library linked
into the eboot (image base 0x7000104000; dispatcher around image offset 0x241fc3, error codes 0x80d3000x,
bounds check `cmp rax,[r13+0x48]`; micro-tile writers 0x2e2a40/0x2e2ad0/0x2e2b60/0x2e2c90/0x2e2e80/
0x2e3080/0x2e3210/0x2e33f0/0x2e3740/0x2e3c30 chosen by bits per element, 512-byte tiles with
vmovups/vmovaps). So the bad bytes are written by the game's CPU tiling while the pages are not yet tracked.
WAD comparison caveat: large textures (2048x2048) differ from a linear read of the WAD by a constant
fraction (BC1 66.5%, BC3 80.5%) even when they look right, so the WADs store them in another layout; only
smaller textures are a reliable byte-exact oracle (38 match exactly). Next: hook the tiler entry and log
each call (destination, size, dimensions, pitch, format, buffer limit) to find overlapping destinations or
sizes that differ from the texture cache's.

**0.1.38 (diagnostics): write provenance trace.** `GOW3_WRITE_TRACE=1` records every write into guest memory
with its source into `user/writes.bin` (48-byte records, runtime_memory.c `WriteRecord`; kinds in runtime.h
`WTRACE_*`): write faults on tracked pages (guest RIP, or the host PC for runtime/GPU threads), file
reads (fd + offset; the log names each open), memcpy/memmove/memset of 4 KiB or more (source and the guest
caller), GPU writes through the backing view (`TryWriteBacking` call site 1-8), DMA copies (CPU or GPU path)
and fills. A lock-free 1M-record ring is drained by a writer thread (safe in the fault handler; a full ring
drops and says so). `native/scripts/wtrace.py writes.bin dump_dir loader.log` finds the texture chunks
that sit at two addresses in a dump and lists the writes that covered them.

**0.1.37 on the device:** still broken with the upstream fixes (run 1) and with every A19-only feature off
(run 2, `FEX host features: ... CSSC 0 FlagM 0 FlagM2 0 FRINTTS 0 RPRES 0 RCPC2 0 FCMA 0`). Test E (GPU memory
copies in order: `GOW3_COPY_THREADS=0`, toggles 672000 = deferred uploads, deferred stream copies, parallel
copies, pooled small copies off) also still broken.

**Texture forensics (2026-10-24, `native/scripts/dumpcheck.py` + a WAD search):** the WADs
(`EXEC/WAD/PS4_LE/*.WAD`, uncompressed) store textures linear; the game tiles them. Texture 159 of a dump
matches its WAD bytes exactly; texture 160's first 524,800 bytes hold other, finished (tiled) textures that
also sit correctly at their own addresses. Across all BC textures of two dumps, 512-byte tiled chunks appear
at two addresses ~30,000 times; the address deltas are texture sizes (0x156400, 0x2ab200, 0x801600) or pairs
of textures hold the same data shifted by 1536/2048 bytes. So correct texture data is written to a wrong
address (neighbouring slot or a few KiB off), not produced wrong. Next: a write-provenance trace for texture
memory (guest store RIP / file read / DMA copy / readback, with address and time) to catch the bad write.

**0.1.37:** `native/patches/fex/0003-upstream-fex-2609-translation-fixes.patch`: 15 upstream FEX correctness
fixes between 2607 and 2609.1 cherry-picked unchanged (cmpxchg 32-bit zero extension, movd/cvttss2si with
0x66, 32-bit MulH, PushRoundingMode, FPR spilling for preserve-all ABI calls, BLSMSK/BLSR CF, SHLD by 16,
F64->I32 precision, x87 fixes, LEA decoding, pdep). Not applied (conflicts): c0c95da (NOP padding),
96d955e (decoder rollback), dfcbdac (JIT alignment). Test: normal env first; if still broken, the
features-off env (`GOW3_FEX_OFF=CSSC,RPRES,FlagM,FlagM2,FRINTTS,RCPC2,FCMA`).

**0.1.36:** `GOW3_FEX_OFF=CSSC,RPRES,...` keeps FEX off the named host features (CSSC, RPRES, FlagM, FlagM2,
FRINTTS, RCPC, RCPC2, LSE, FCMA, CRC, AES, SHA, PMULL); the startup line lists them all.

**0.1.35 (fix candidate):** FEX's `rep movs` with FEAT_MOPS (MemoryOps.cpp `MemCpy`) checks for overlap with
`dst - src < Length`, where Length counts elements, not bytes, so a `rep movs{w,d,q}` move whose gap is between
the count and the byte length runs `cpyf` (forward-only) over overlapping memory. The A19 has FEAT_MOPS; the
Snapdragons gow3_android runs on do not, so only iOS takes this path. A texture pool compacting itself makes
exactly such moves. `native/patches/fex/0002-mops-memcpy-overlap-check-in-bytes.patch` compares against the
length in bytes; `GOW3_FEX_MOPS=0` turns MOPS off altogether (A/B). Local fix only: FEX does not accept
AI-written contributions, so a report upstream has to come from a person.

**0.1.34 (diagnostics):** creating `Documents/gow3/dump.txt` writes every texture sampled in the last two GC
ticks to `user/dumps/dump_N`: `NNN_gpu.bin` (mip 0, layer 0 of the Vulkan image), `NNN_mem.bin` (its tiled
guest memory) and `manifest.txt` (format, size, pitch, tile/array mode, flags, usage). Detiled and decoded on
a PC: memory bad = the game's data (FEX); memory good but GPU image different = upload/detile; both good =
sampling.

**0.1.33 (diagnostics):** creating `Documents/gow3/reupload.txt` uploads every texture not written by the
GPU again from guest memory (`Texture reupload: N textures ... M GPU-written skipped`). Fixed by it = the
copy to the GPU (staging copy threads or detiler) damaged them; with `toggles.txt` 256 the re-upload copies
synchronously, which separates the two. Still broken = guest memory is wrong or the images are sampled
wrongly. `Texture verify` lines now also count the GPU-written textures they skip.

**0.1.32 (diagnostics):** the buffer cache and memory tracker work in 16 KiB tracker pages on iOS
(`TRACKER_PAGE_BITS` 14; Android 12). A texture whose region is marked GPU-modified uploads from the buffer
cache's copy instead of guest memory. The log now prints `Texture uploads: N from memory, M from the GPU
buffer` every 5 s and the first GPU-sourced addresses; `GOW3_TEXTURE_FROM_MEMORY=1` always uploads from guest
memory. `GOW3_TEXTURE_VERIFY=1` re-hashes textures uploaded from memory every 2 s and logs and re-uploads
stale ones (`Texture verify: stale ...`): stale = a missed invalidation; none stale but broken = rendering.

**0.1.31 (main change):** alias copies between uncompressed storage images and BC textures go through a
buffer on iOS (vk_runtime.cpp `CopyAliasImage`). God of War III streams textures with GPU writes to storage
images over the texture pool (texture_cache.cpp `ApplyBackingWrites`), and Metal has no copy between
compressed and uncompressed textures, so MoltenVK emulates that vkCmdCopyImage; image -> buffer -> image is
the path every texture upload already takes. Each new format pair prints `Alias copy: ... through a buffer`.
`GOW3_ALIAS_COPY_DIRECT=1` keeps vkCmdCopyImage (A/B). A 0.1.30 run with `GOW3_AUDIO_TRACE=1` logged no failed
read when the textures broke, so the read change below is a safety fix, not the cause.

**0.1.31:** file reads go through a host buffer on iOS (runtime_file.c `bounce_read`). The runtime touched each
destination page so the fault handler unprotected it, then read() straight into guest memory; the GPU thread
can protect a page again before the kernel's copy reaches it (level files are ~200 MB), and XNU's read() then
fails with EFAULT after partial data: half-loaded textures. Android avoids it with userfaultfd. Now the kernel
fills a host buffer and a user-mode memcpy writes guest memory, so every write goes through the handler.
`GOW3_DIRECT_READ=1` restores the direct read and logs each EFAULT (A/B: if those lines appear when it breaks,
this was it).

**0.1.30:** such passes take the guest scissor as their render area instead of the maximum framebuffer
size (Apple's tile GPU works through the whole area). `GOW3_SUBMIT_WAIT=1` makes every submission wait for
the GPU (very slow): if the corruption goes away, it is reuse before the GPU finished.

**0.1.29:** the renderer's own passes pushed descriptors even with 0.1.27: the detiler (every tiled texture
upload), blits, GPU fault processing and HLE shaders. With robustBufferAccess, MoltenVK's stale pushed buffer
size clips the detiler part way through a large texture, which fits "large textures broken". They now bind
descriptor sets from a shared heap on MoltenVK 1.4.1+ (`vk_host_descriptors.h`, `GOW3_HOST_PUSH_DESCRIPTORS=1`
turns it off), while the game's pipelines keep pushing. Also: a cached pipeline that fails to build is dropped
by the warm-up instead of stopping the game; the GPU profiler uses one 4096-query pool per slice (Metal's 32 KiB
counter buffer limit made MoltenVK fall back to CPU timestamps, the 0 ms of mode 1) and mode 2 is mode 1 (it
submitted in the middle of a draw's recording); captures list each texture's tile mode, array mode, mips and
pitch; iOS frame limit 30 by default (heat; `fps_limit=` in gow3.ini). Presenter-side passes (final
post-process, scene resolution, TAA, FSR, camera motion) still push: they run on the presenter's scheduler
or bind only images. Device test: the broken scene (textures), then `GOW3_GPU_PROFILE=1` for the pass split.

**0.1.24 on the device (2026-10-23 evening): playable.** Played through the Poseidon fight with no crash
(jumps, hits, orbs: the NoExec crash is fixed). 27 FPS in the frame stats (was 8-12 on 0.1.22), frames land
on 33/50 ms (60 Hz vblank pacing), GPU ticks 63-72 % of the frame, 400-3000 draws/frame. Textures still badly
broken in places (item 3, now the most visible problem). Next: `GOW3_GPU_PROFILE=2` run for the pass split,
the corruption, and pacing at 120 Hz / a 30 FPS cap.

0.1.27: push descriptors off on MoltenVK 1.4.1+ (`GOW3_PUSH_DESCRIPTORS=1` turns them back on). KytyPS5
(PS5 emulator, PR #1083) found MoltenVK 1.4.1+ leaves buffer-size metadata stale for pushed descriptors and
corrupts rendering; we ship MoltenVK 1.4.2 and shadPS4 pushes descriptors for almost every draw. The 0.1.22
bounds checks (OpArrayLength reads that metadata) hiding Kratos fits the same fault. Test: textures, then FPS
(descriptor sets cost some CPU on the GPU thread). Alternatives checked: KosmicKrisp (Mesa Vulkan on Metal) is
conformant on macOS but has no iOS support yet (Oct 2026).

0.1.26: `Documents/gow3/capture.txt` triggers the frame analyzer (output in `gow3/user/captures/`).
`MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=0` did not fix the textures either (and cost FPS: 16 vs 27), so
argument buffers are out too. Next: the capture of a broken frame, and `[mvk-error]`/`[mvk-warn]` lines.

Toggle test on 0.1.25: `all-off`, `tracking` and `none` all leave the textures broken, so the corruption is
not in the port's switchable optimizations. Next suspect: MoltenVK itself, first argument buffers
(`MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=0` in env.txt: wrong textures bound would give these stripes).

0.1.25: `Documents/gow3/toggles.txt` is the default `GOW3_TOGGLE_FILE` (bisect the texture corruption on
the phone, files in `docs/toggles/`). Corruption on 0.1.24: rainbow diagonal stripes on most surfaces, blown-out
white light; with `GOW3_NO_MSAA=0` textures were right for a while and then broke (FPS rose when they broke),
so it is not MSAA itself but something that goes stale over time (caches, page tracking, the 64-fault
safety net in probe.c that leaves a page untracked).

0.1.23: hook stubs registered as guest code (the NoExec crash, item 1); buffer bounds checks off by default.
0.1.24 (GPU time, item 2): guest MSAA off by default on MoltenVK (`GOW3_NO_MSAA=0` restores it), game
surface at most 2x scale (1748x804 instead of 2622x1206), and `GOW3_GPU_PROFILE=2`: per-pass GPU time on
MoltenVK (one submission per timestamp; mode 1 read 0 ms there). Device steps:
1. Play the tomb area with `show_fps=1`; compare with `GOW3_NO_MSAA=0` in `env.txt` (fps and picture).
2. Add `GOW3_GPU_PROFILE=2` to `env.txt`, play 30 s in a slow scene, Copy logs: the `GPU profile:` lines
   list ms per frame by pass. The fps is lower in this mode; only the split between passes counts.
3. Then fix the top passes (load/store churn, a format fallback, a slow compute pass) to reach 35+ fps.

## State on the device (0.1.22, save from the PC version loaded)
- Title screen and menus look right at 30-60 fps (600p patch `1066x600`, `show_fps=1`).
- Gameplay renders correctly in some areas (the tomb area: textures, fire, lighting, Kratos all right)
  and is badly corrupted in others (the gold Hades gate scene: blocky blue static, blown-out lighting).
  So the corruption comes from specific passes or effects, not textures in general.
- Gameplay runs at 7-12 fps and the phone reaches thermal state `serious` in about a minute.
- The game crashes after some time in play, always the same way (NoExec, below). Attacks no longer crash
  (0.1.21).
- `Documents/gow3/env.txt` on the phone (read at start, 0.1.20) currently holds:
  `FEX_DISABLEL2CACHE=0`, `FEX_DYNAMICL1CACHE=0`, `MVK_CONFIG_PERFORMANCE_TRACKING=1`,
  `MVK_CONFIG_PERFORMANCE_LOGGING_FRAME_COUNT=300`, `GOW3_BUFFER_BOUNDS=0`.

What was tried and its result:
| Build | Change | Result |
|---|---|---|
| 0.1.19 | GPU write faults release the whole 16K page; landscape; frame stats | fault loops 10 -> 2 at load |
| 0.1.20 | `env.txt`; FEX lookup caches as upstream (`env.txt`) | the NoExec crash still happens (seen on 0.1.22 with them on) |
| 0.1.20 | MoltenVK fast math off | textures unchanged |
| 0.1.21 | no-perspective barycentrics from perspective ones on Metal | attack crash fixed (MSL can't declare both) |
| 0.1.22 | shader bounds checks for guest buffers (no robustBufferAccess2) | textures unchanged, **Kratos invisible**; off with `GOW3_BUFFER_BOUNDS=0` |

## Next, in order

### 1. The NoExec crash (top blocker) — likely fixed in 0.1.23, confirm on the device
Cause found in the code (2026-10-23): probe.c's hooks (red orb multiplier, cheats, enemy health bar
damage hook, damage/orb pickup multipliers) jump from the game to x86 stubs in their own low-map pages,
which follow the image, import traps and data traps, i.e. near image + 0x2400000. On iOS `protect(..., 5)`
only makes them readable, and they were **never registered with `guest_cpu_code`**, so FEXCore's
`QueryGuestExecutableRange` said NOEXEC the first time a hooked path ran (an enemy hit, an orb pickup).
Stubs sit at `i * 256` in their page, which matches the `+0x600` / `+0x700` / `+0x0` offsets of the three
targets. 0.1.23 registers every stub page (`protect_code` in probe.c), and also the ISA rewrite stub area
after the image, which had the same gap. If the crash still happens on 0.1.23, the notes below apply.

Earlier analysis:
Log signature: `[iOS-noexec] status=NOEXEC` at game offsets `0x23f8600`, `0x2400700`, `0x2410000`, then
`Guest fault (signal 11) ... address 0x0`. NOEXEC means FEX's decoder asked `QueryGuestExecutableRange`
(`native/cpu/guest_cpu_fex_darwin.cpp`) and the address is not registered code. Registered code is the
game image (ends at offset `0x1da4b3c`) plus the trap page, so **every crash target lies past the end of
the image**: the game jumps into data (bad function pointer or corrupted return address). The 16K page
alignment of `0x2410000` is probably a coincidence.

Candidate causes, cheapest check first:
- **What lives there:** log the runtime's mappings around image + `0x2400000` (flexible/direct memory map
  log). If it is a heap or a module's data, the caller is reading a stale or wrong pointer.
- **Who jumped:** when NoExec fires, log the last N guest block entries per thread (a small ring in the
  dispatcher or in `Core.cpp` next to the `[iOS-noexec]` log) so the indirect branch source is named
  instead of guessed. The stack-scan return addresses in the fault dump are stale.
- **FEX block cache returning the wrong block:** try `FEX_MULTIBLOCK=0` and `FEX_MAXINST=1` via `env.txt`
  (already wired in 0.1.20). If the crash goes away, the bug is in block linking or lookup.
- **Memory ordering between game threads:** `FEX_TSOENABLED=1` is the default; check it is on and try
  `FEX_VECTORTSOENABLED=1`, `FEX_MEMCPYSETTSOENABLED=1`, `FEX_HALFBARRIERTSOENABLED=0` via `env.txt`.
- **A runtime stand-in returning a bad pointer:** the fault report's last 32 host calls end in
  `guest_memcpy` / mutex loops; look at which HLE call ran last on the faulting thread.
- **Memory pressure:** iOS sent a critical memory warning before an earlier crash; log `phys_footprint`
  every few seconds (MoltenVK reported 2.4 GB GPU memory in the tomb area).
- Compare with the same scene on gow3_android if possible.

### 2. GPU time (7-12 fps)
MoltenVK performance tracking (0.1.22 log, tomb area): `Execute a MTLCommandBuffer on GPU` avg 34.5 ms,
recent ones 90-185 ms, about 3.4 command buffers a frame; `Present` avg 32 ms; encoding only 0.5 ms;
the frame waits on "GPU ticks" for 80+ % of the time. So the GPU itself is slow, not the CPU recording.
A good Vulkan-on-Metal translation should cost about 10 %; a gap this large usually means one bad path
or a stall, not overall overhead. shadPS4's own GPU profiler reads 0 ms per pass on MoltenVK.

What to look for:
- **Time each pass:** split work into more command buffers in a debug mode (e.g. one per render pass or
  per N draws) and read MoltenVK's per-command-buffer GPU time, or add Metal counter sample buffers.
  Find whether one pass (detiling compute, a full-screen effect, shadow maps, FSR) takes most of the time.
- **Stalls:** CPU-GPU sync points (readbacks, `vkQueueWaitIdle`, fences per draw), and render passes that
  are restarted per draw on a tile GPU (load/store of attachments each time is very costly on Apple GPUs).
  Count render pass begins per frame.
- **Expensive translation paths:** primitive restart warnings (`Metal does not support disabling primitive
  restart`), geometry or tessellation emulation, 3-component formats, and `IT_SET_PREDICATION` (unimplemented).
- **Upscaler:** the overlay shows FSR 3.1; check its cost and whether MetalFX can replace it.
- Heat: 30 fps cap, then lower resolution, thread QoS, thermal-state adaptation.
- First visit to an area compiles about 490 shaders (MSL compile avg 34 ms each); later visits use the
  cache. Not a steady-state cost.

### 3. Corruption in some scenes
Rendering is right in the tomb area and wrong at the Hades gate, so find the pass that differs:
- Capture both scenes with the frame capture tools in `vk_rasterizer.cpp` (`FrameCapture`) and compare
  pass lists, formats and shaders.
- Suspects: formats the A19 lacks (log: A2B10G10R10 SNORM/SINT, RGB32 sampling, D16S8 attachments,
  B4G4R4A4 as attachment) and their fallbacks; compute passes (storage images without format);
  `IT_SET_PREDICATION`; blending of HDR targets (blown-out light).
- MoltenVK settings to try via `env.txt`: `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=0`,
  `MVK_CONFIG_TEXTURE_1D_AS_2D`, `MVK_CONFIG_SHADER_COMPRESSION_ALGORITHM`.

### 4. Clean-up
- 0.1.23: the 0.1.22 buffer bounds checks are off by default (`GOW3_BUFFER_BOUNDS=1` turns them on); they
  made Kratos invisible and did not fix the textures. Most likely `OpArrayLength` does not give the bound
  range on MoltenVK. `GOW3_BUFFER_BOUNDS=0` in `env.txt` is no longer needed.
- Make the confirmed `env.txt` settings defaults in code once they are proven.

## Earlier handover (2026-10-22 night)

## State on the device (0.1.18, full game CUSA01715 + 01.02 on the phone)
**The game boots and plays its intro**: brightness screen, DualSense input, menus, cutscene video with
FFmpeg. It crashes when **New Game** is chosen (below).

The black-screen hang of 0.1.15-0.1.17, and its fix (0.1.18, `native/cpu/guest_cpu_fex_darwin.cpp`):
- Darwin reports a write to a protected page as SIGBUS / `BUS_ADRALN`, the code of an alignment fault.
  `guest_cpu_handle_fault` sent it to FEXCore's `HandleUnalignedAccess`, whose "already backpatched by
  another thread" check returns 0 (run it again) for a plain STR. A zero store to a page that GPU page
  tracking protects (game offset `0x1a55de`) then faulted ~600,000 times a second and the tracker never saw
  it: black screen, phone hot. Now only ESR data fault status `0b100001` (alignment) goes to FEXCore.
- The first boot of the day got past it by luck: its faulting store was an STP, which FEXCore logs as
  `Unhandled JIT SIGBUS` and passes on to the tracker.
- 0.1.16 added the tools that found it: the loader dump samples the pc 16 times with the fault count around
  the samples, prints library+offset for `<redacted>` system symbols and maps JIT addresses to game offsets
  (`gow3_guest_rip_at`); probe.c counts every fault; a watchdog (`GOW3_FRAME_WATCHDOG=60`, graphics only)
  dumps the threads after 60 s without a flip and ends the app after 180 s; fault storms (>1000/s) are
  logged; thermal state changes go to the log; game files are excluded from backup.
- 0.1.17 (kept as a safety net, did not fire): 64 claimed GPU-tracking faults in a row on one page make
  the page writable (`probe.c`, logs `GPU tracking: 64 write faults in a row`).

## Next
1. **New Game crash** (log of 2026-10-22 17:04 UTC): FEXCore `[iOS-noexec]` at game offset `0x23f8600`
   (image + 0x23f8600 = 0x70024fc600), then `Guest fault (signal 11) ... address 0x0`. The image ends at
   0x1da483c (eboot + modules 0 and 1), so the target is outside any registered code range
   (`QueryGuestExecutableRange` = the image + the trap page): most likely a bad function pointer. 4 s
   before, iOS sent a critical memory warning (`TRIM_MEMORY_RUNNING_CRITICAL`) and the thermal state was
   `serious`; no failed allocation is logged. The stack-scan return addresses are stale. To find the caller:
   log the last guest block entries (or the indirect branch source) when NoExec fires, check what the
   runtime mapped at that address (flexible/direct memory map log), and measure `phys_footprint` at the
   time. Compare with the same step on gow3_android if possible.
2. Performance and heat (now measurable in real play): 30 fps cap, lower render resolution + MetalFX,
   PAUSE spin loops, thread QoS, thermal-state adaptation, `FEX E:` debug noise.
3. AltStore shows the JIT helper expiring a day before the app; the installed profiles (`ideviceprovision`)
   show both renewed together each time, so it is only AltStore's display.

## GPU step 2: shadPS4's video core in the app (this branch)
- `native/runtime/gpu/` is gow3_android `gpu/` (imported unmodified, then iOS commits marked `gow3 (iOS)`).
  `native/cmake/gow3gpu.cmake` builds it as `libgow3gpu.a` inside the FEX tree (FEX's fmt and xxhash);
  `gow3gpu_deps.cmake` holds the parts CMake forbids in deferred code (OBJCXX, sirit, miniz).
  New submodules: Vulkan-Headers v1.4.344 (as gow3_android; FEX's 1.4.337 is too old), VMA 3.3.0,
  robin-map 1.4.1; SDL3 3.2.24 is a pinned tarball (static, no GL/GLES), Boost and glslang come from Homebrew.
- Left out on iOS: FSR 3/4 (`ios/ffx_stub.c` fails their calls), AvPlayer (`ios/avplayer_stub.cpp`, needs
  FFmpeg), Zydis/xbyak (x86-64 SRT walker JIT; ARM64 interprets). `gow3_png` decodes with ImageIO.
- iOS changes: page tracking at 16 KiB (`TRACKER_PAGE_BITS` 14), `ios/window_ios.mm` (Frontend::WindowSDL on
  the app's CAMetalLayer via `gow3_gpu_set_surface`; text dialog confirms its initial text), the Vulkan
  dispatcher from MoltenVK's linked `vkGetInstanceProcAddr`, `ios/gow3_darwin_compat.h` (gettid), utility-QoS
  helper threads, user paths in `Library/Caches/gow3` (static init, before main), no nullDescriptor /
  robustBufferAccess2 (soft on Apple; null buffer bindings get a zeroed stand-in).
- Local check on Linux x86-64 (catches dependency/API problems before CI): configure FEX with
  `-DENABLE_X86_HOST_DEBUG=ON -DENABLE_FEX_ALLOCATOR=OFF -DCMAKE_PROJECT_FEX_INCLUDE=native/cmake/fex_project.cmake`
  and `ninja gow3gpu -k 0`; only xbyak, FFmpeg and the .mm fail there (Linux-only).
- CI: `.github/workflows/gpu.yml` (GOW3_BUILD=gpu-check, error summary). The IPA workflow builds and links it.
- App: **Start game** (graphics: Metal view over the app, two-finger double tap hides it), **CPU only**,
  **Copy logs** (share sheet), **Dump threads**.

## State on the device (0.1.15, 2026-10-22, without the game data on the phone)
Everything up to the game's first data loads works on the iPhone 17: FEXCore runs the eboot, the runtime
loads it, MoltenVK device/swapchain (1206x2622), dense buffer arenas, presenter, overlay, eop/intro/sound
threads, first graphics pipelines (7 ms each), DualSense through SDL3 (`pad opened`), CoreAudio (3 ports), MP3
through FFmpeg. It then waits on a black screen because `EXEC/` is missing (`BOOT0_01.GNF`,
`DEPENDENCYGRAPH.IFF`, `R_FX.WAD` not found). No crash.

Fixed on the way (each from a device log):
- 0.1.10: no sparse binding on MoltenVK -> dense arenas (64 MiB, `GOW3_ARENA_MB`), soft robustness2.
- 0.1.11: fault report: guest registers, stack return addresses, last 32 host calls (`CallHost` ring).
- 0.1.12: `GOW3_DMEM_MB=7104` (default patch "Texture Corruption Fix" needs +2048 MiB, as run.py sets).
- 0.1.13: controllers and audio through SDL3 (`SDL_SetMainReady`, `SDL_JOYSTICK_HIDAPI=0`).
- 0.1.14: guest window 4+8+28 GiB; it took all of iOS's 64 GiB hole and thread creation failed (no VA left).
- 0.1.15: FFmpeg 7.1.2 static (H.264/HEVC/AAC/MP3, swscale, swresample) for runtime_videodec.c / runtime_mp3.c.

## Next
1. **Device run with the full game** (`EXEC/`, `ENGLISH/`, `*.plt` incl. the 01.02 update in `gow3/game/`):
   Start game on 0.1.15, Copy logs. Goal: intro logos, intro video, title screen.
2. Likely issues on that run: first real GPU work through MoltenVK (shader translation errors, formats the
   A19 lacks: A2B10G10R10 SNORM/SINT, RGB32 sampling, D16S8 -> fallback), arena merges ("Migrating arena"),
   memory (`phys_footprint` under ~6 GB), first-compile stutter, FFmpeg video decode on device.
   Empty texture slots are written as nil by MoltenVK (its 1.4.2 source); add placeholder images only if a
   log shows a problem. AvPlayer is still a stub (`ios/avplayer_stub.cpp`); enable it if the game imports it.
3. Later (UX): save backup/restore in the app (saves are `gow3/user/savedata/1/<CUSA>/`, same format as the PC
   port), touch controls, settings screen, boot.bin preparation on the phone (port scripts/prepare.py).
4. Later (performance): `FEX E:` debug noise from the FEX fork, JIT pool size, render resolution patches
   (480p/720p) + MetalFX upscaling, thermals.

---
## Earlier handover (morning of 2026-10-21)

## Where things stand
| Area | State |
|---|---|
| Built-in JIT (no StikDebug) | **Works on the iPhone 17** (in-app pairing, helper extension). docs/JIT.md |
| Memory | AltStore keeps `increased-memory-limit`: 6143 MB limit, 64 GiB free window at `0x70_0000_0000`. File-backed guest memory costs 0 MB. Install/refresh **only with AltStore** (SideStore drops the entitlement). |
| GPU facts | A19: `bc_textures` true (PS4 BC1–BC7 textures sample directly), 4 GB max buffer, 5.3 GB working set |
| Runtime | `native/runtime` (gow3_android src/, iOS port). Compiles for iOS; Linux tests pass (`native/runtime/tests/run.sh`) |
| Loader | CPU-only start in the app works: boot file accepted, patches applied, image mapped, **libc (module 0) init ran through FEX and returned 0**, 2.8 GB direct memory mapped in the window |
| Blocker | The game thread **stops while starting linked module 1** (app stays responsive; no crash) |

## 1. Find why module 1 stops (do this first)
1. Copy the full game (`ENGLISH/`, `EXEC/`, `*.plt`) to `Documents/gow3/game/` to rule out missing files:
   ```sh
   ideviceinstaller -l | grep -i gow3            # bundle id (AltStore adds the team id)
   ifuse --documents <bundle id> ~/iphone
   cp -r game/ENGLISH game/EXEC game/sce_discmap.plt ~/iphone/gow3/game/
   cp "_Update to v1.02/game/"*.plt ~/iphone/gow3/game/
   fusermount -u ~/iphone
   ```
2. Install 0.1.5, Enable JIT, Start game (CPU only). When it stops at `Starting linked module 1`, wait ~20 s,
   tap **Dump threads**, and read `Documents/gow3/loader.log`: each thread prints guest RIP (`image-relative=`),
   its rbp chain, the thread name and the host symbols it is in.
3. Likely causes to check against the dump:
   - waiting on a runtime primitive (`runtime_sema.c`, `runtime_mutex.c`, cond waits): Darwin's CLOCK_REALTIME
     waits or the polled timed locks in `compat_darwin.c`;
   - a guest spin loop (phone gets warm): a TSC/time source (`guest_cpu_tsc`, `runtime_time.c`) or an atomic
     the guest polls;
   - a host call into a stub that never returns (`native/runtime/ios/stub_*.c`);
   - an unresolved import trap (would normally print `unresolved` and exit).
4. Useful extra logging: `GOW3_TRACE_SEMA=1` (set with `setenv` in `startGameCpuOnly` in
   `app/modules/gow3-core/ios/Gow3CoreModule.swift`) prints every blocking semaphore wait.
5. FEX prints `Failed to mprotect last page of code buffer`: its 4 KiB guard page cannot be protected on 16 KiB
   pages. Harmless so far; patch `PooledAllocatorVirtualWithGuard` to a host page later (docs/AUDIT.md).

## 2. Finish the runtime (milestone 4)
- **Audio**: replace `runtime_audio.c`'s SDL3 output with AudioQueue/AVAudioEngine (same `runtime_audio_resolve` exports).
- **Pad**: `runtime_pad.c` on the GameController framework (DualSense/DS4/Xbox/Backbone).
- **FFmpeg for iOS** for `runtime_mp3.c` and `runtime_videodec.c` (cutscenes): built by build-ios.sh since 0.1.15.
- **boot.bin on the phone**: port gow3's `scripts/prepare.py`, `link_libc.py`, `link_modules.py`,
  `content_profile.py`, `patches.py` (Python) to C or Swift, so no PC is needed.
- Add the new sources to `native/cmake/gow3core.cmake` and `native/runtime/build-ios-check.sh`.

## 3. GPU (milestone 5, the big one)
- Port gow3_android `gpu/` (shadPS4 video core + shim) to iOS: MoltenVK (static), a CAMetalLayer from a native
  fullscreen UIViewController (`window_ios.mm`), SPIR-V → MSL through MoltenVK.
- Smoke test first: `src/vulkan_smoke.c` on the device, then the title screen.
- Replace `native/runtime/ios/stub_gpu.c` with the real library; remove `--cpu-only` from the start button.
- Render low (gow3 already has 480p/720p patches) and upscale with MetalFX spatial; frame generation only later.

## 4. Optimisation (milestone 6)
- Measure `phys_footprint` while playing; keep anonymous memory under ~6 GB (guest pool is file-backed already).
- JIT pool size (256 MiB now; Madeira uses 640–896 MiB), FEX options, thermals.

## Reference
- docs/AUDIT.md: platform findings and device results. docs/JIT.md: built-in JIT. CLAUDE.md: layout and decisions.
- CI: `.github/workflows/ios.yml` (IPA, dispatch it on the branch), `.github/workflows/runtime.yml` (Linux tests + iOS compile check, runs on push).
