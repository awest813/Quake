# Sega Dreamcast Port Plan (DreamSDK / KallistiOS)

This document is the implementation plan for porting this TyrQuake-based,
software-rendered Quake to the **Sega Dreamcast** using **DreamSDK**
(the Windows distribution of the KallistiOS toolchain).

It is a planning document only — no engine code is changed by this commit.

---

## 1. What we are starting from

* **Engine:** TyrQuake `v0.62-pre`, NetQuake (`-DNQ_HACK`), **software renderer**
  (the `d_*.c` / `r_*.c` 8-bit palettized rasterizer). No OpenGL is involved,
  which is the single most important fact for this port: the Dreamcast does not
  need a hardware GL renderer to run this code.
* **Platform layer is already isolated** behind a small set of files and is
  selected per-target by `port.h` `#define`s plus a per-target `Makefile.*`:

  | Concern            | File(s)                                  | DC replacement |
  |--------------------|------------------------------------------|----------------|
  | Entry / system     | `source/sys_unix.c`                      | `sys_dc.c`     |
  | Video / present    | `source/vid_sdl.c`                       | `vid_dc.c`     |
  | Input pump         | `source/vid_sdl.c` (`Sys_SendKeyEvents`) + `source/in_null.c` | `in_dc.c` |
  | SDL bootstrap      | `source/sdl_common.c`                    | dropped        |
  | Sound DMA backend  | `source/snd_sdl.c`                        | `snd_dc.c`     |
  | CD audio           | `source/cd_null.c` (already null)         | keep null first, optional `cd_dc.c` |
  | Networking         | `source/net_none.c` (already no-net)      | keep null first |
  | Resolution macros  | `source/port.h`                          | add `DREAMCAST` block |
  | Heap sizing        | `source/zone.c` `Memory_GetSize()`        | add DC cap |

* **Existing per-target Makefiles** (`Makefile.rs90`, `Makefile.amini`,
  `Makefile.k3s`, `Makefile.rs97`, ...) are the template for a new
  `Makefile.dreamcast`. They differ from each other only by `CC`, a few
  `-D<PLATFORM>` flags and the link line; the C file list is otherwise identical.

### Hardware budget we must fit into

| Resource        | Dreamcast        | Consequence for the port |
|-----------------|------------------|--------------------------|
| CPU             | SH-4 @ 200 MHz   | Software Quake at 320×240 is the realistic target; 640×480 software is too slow. |
| Main RAM        | **16 MB**        | The default 128 MB heap (`Memory_GetSize`) must be capped to ~8 MB. This is the binding constraint. |
| Video RAM       | 8 MB (PVR)       | Plenty for a 320×240 16-bit present surface / texture. |
| Sound RAM       | 2 MB (AICA)      | Streamed via KOS `snd_stream`; engine mixes into main RAM. |
| Storage         | GD-ROM via `/cd` | `pak0.pak` (~18 MB shareware) is too big for an in-RAM romdisk, so game data is read from the disc through KOS's newlib `/cd`. |

---

## 2. Strategy decisions (and why)

1. **Keep the software renderer.** Reuse the entire `d_*` / `r_*` pipeline
   untouched. The renderer already produces an 8-bit palettized image into
   `vid.buffer`; we only need to *present* that buffer. This is the lowest-risk,
   fastest path and matches every known good Dreamcast Quake.

2. **Render at 320×240, present in RGB565.** Add a `DREAMCAST` block to `port.h`
   with `BASEWIDTH 320 / BASEHEIGHT 240`. Each frame, convert the 8-bit buffer to
   16-bit using a palette LUT. The engine already declares `uint16_t
   d_8to16table[256]` in `vid_sdl.c` for exactly this — we fill it with **RGB565**
   values in `VID_SetPalette` and use it in `VID_Update`.

3. **Native KOS backends, not SDL.** KallistiOS has an SDL 1.2 kos-port, so in
   principle the existing SDL files compile. But DC SDL adds overhead and an extra
   dependency for no benefit here. We write thin native backends against KOS APIs
   (`pvr`/`vid`, `maple`, `snd_stream`, `timer`). This keeps the hot path
   (framebuffer present + input) tight.

4. **Cap the heap to fit 16 MB.** `Memory_GetSize()` defaults to 128 MB. Add a
   `DREAMCAST` branch returning ~8 MB (tunable; `-mem`/`-heapsize` parsing stays
   for emulator experimentation but DC has no command line, so the default must be
   correct). The hunk holds models, sounds and the surface cache; 8 MB is the
   proven figure for shareware `id1` at 320×240. We will measure and tune.

5. **Data on the disc, read through `/cd`.** `parms.basedir` becomes `/cd`.
   KOS exposes the GD-ROM/CD data track as a read-only newlib filesystem, so the
   POSIX `open/read/lseek` calls in `common.c` work unmodified. `id1/pak0.pak`
   ships in the disc's data track next to `1ST_READ.BIN`.

6. **Defer net and CD-audio.** Ship first with `net_none.c` (already in the file
   list) and `cd_null.c` (already wired). CDDA music and BBA/modem networking are
   clearly-scoped follow-ups, not part of the first bootable build.

---

## 3. Files to add / change

### 3.1 `source/port.h` — resolution
Add before the `#else` fallback:
```c
#elif defined(DREAMCAST)
#define BASEWIDTH  320
#define BASEHEIGHT 240
```

### 3.2 `source/zone.c` — `Memory_GetSize()`
Add a DC cap so the default (no command line) is safe:
```c
#ifdef DREAMCAST
    return (size_t)8 << 20;   /* 8 MB hunk fits 16 MB DC RAM; tune after profiling */
#endif
```
(Keep `-mem` / `-heapsize` parsing above it for emulator runs.)

### 3.3 `source/sys_dc.c` — system + entry (new; modeled on `sys_unix.c`)
* `Sys_Init`, `Sys_Error`, `Sys_Quit`, `Sys_Printf` (to dbgio/serial console).
* `Sys_DoubleTime` via KOS `timer_ms_gettime()` / `timer_us_gettime()`.
* `Sys_FileTime`, `Sys_mkdir`, `Sys_FileOpenRead/Write` — newlib POSIX on `/cd`
  works for reads; writes (configs/saves) target the **VMU** via `/vmu/a1/...`
  or are stubbed initially.
* `main()`:
  * KOS init: declare `KOS_INIT_FLAGS(INIT_DEFAULT)` and a romdisk stub.
  * No argv on hardware → build a fixed arg vector (`"quake"`,
    optional `"-mem","8"`), call `COM_InitArgv`.
  * `parms.basedir = "/cd"`; `parms.memsize = Memory_GetSize()`;
    `parms.membase = malloc(...)`.
  * Drop `fcntl(STDIN…O_NONBLOCK)`, `signal`, `sys/mman`, `usleep`-on-stdin —
    none exist meaningfully on DC. The dedicated-server branch is compiled out.
  * Standard `Host_Init` + `while(1){ Host_Frame(Sys_DoubleTime()-old); }`.

### 3.4 `source/vid_dc.c` — video present (new; replaces `vid_sdl.c`)
* `VID_Init`: set `vid.width/height = 320/240`; init PVR (`pvr_init_defaults()`)
  or set a 320×240 RGB565 framebuffer via `vid_set_mode`. Allocate the 8-bit
  offscreen `vid.buffer` from the hunk (do **not** point it at VRAM — the
  rasterizer reads back the buffer). Allocate `d_pzbuffer` + surface cache exactly
  as `vid_sdl.c` does (`D_SurfaceCacheForRes`, `D_InitCaches`). Fill `vid.colormap
  = host_colormap`, `vid.fullbright`, aspect, `numpages = 1`.
* `VID_SetPalette` / `VID_ShiftPalette`: build `d_8to16table[256]` as RGB565.
* `VID_Update`: convert the dirty region from 8-bit → RGB565 via the LUT into a
  PVR texture (then a single textured quad blit, hardware-scaled to 640×480) **or**
  straight into the 16-bit framebuffer, then flip. Texture-quad present is
  preferred: it offloads the 320→640 upscale to the PVR and gives a clean VGA/RGB
  output.
* `D_BeginDirectRect` / `D_EndDirectRect`: draw the loading-disc icon into the
  offscreen 8-bit buffer (same logic as `vid_sdl.c`, just no `SDL_UpdateRect`).

### 3.5 `source/in_dc.c` — input (new; replaces SDL event pump + `in_null.c`)
* `Sys_SendKeyEvents`: poll the Maple bus each frame
  (`maple_enum_type(0, MAPLE_FUNC_CONTROLLER)`, `cont_state_t`), diff against the
  previous state, and emit `Key_Event(key, down)` for each button. Map:
  * D-pad → arrows / menu navigation, A → jump/enter, B → back/cancel,
    X/Y → attack / use, Start → escape, triggers (L/R) → strafe or
    attack/jump. Final binding table chosen during bring-up.
* `IN_Init/IN_Shutdown/IN_Commands` and `IN_Move`: feed the **analog stick** into
  `cmd->sidemove/forwardmove` (or mouse-look via `cl.viewangles`) and the analog
  triggers as needed. Detect a Maple **mouse** / **keyboard** if present and route
  them too (cheap win on KOS).

### 3.6 `source/snd_dc.c` — sound (new; replaces `snd_sdl.c`)
* Implement the same `dma_t shm` contract Quake expects:
  * `SNDDMA_Init`: `snd_stream_init()`, allocate `shm->buffer`, set
    `samplebits=16, channels=2, speed=22050` (DC-friendly; 44100 if CPU allows),
    register a `snd_stream` callback that copies from `shm->buffer` using a
    `rpos/wpos` ring exactly like `snd_sdl.c`'s `paint_audio`.
  * `SNDDMA_GetDMAPos`, `SNDDMA_Submit`, `SNDDMA_Shutdown`,
    `SNDDMA_Lock/UnlockBuffer` mirroring the SDL backend.
  * Run the stream poll from a KOS thread or pump it in the main loop —
    decide during bring-up based on measured underruns.

### 3.7 `source/cd_dc.c` — CD audio (optional, phase 5)
* Use KOS `cdrom_cdda_play()` for redbook-style music if a multi-track image is
  built. Until then `cd_null.c` stays in the build.

### 3.8 `Makefile.dreamcast` — build (new; modeled on `Makefile.rs90`)
* `CC := kos-cc` (from DreamSDK's `environ.sh`), or `sh-elf-gcc` with the KOS
  include/lib flags.
* `CFLAGS = -O2 -ml -m4-single-only -ffunction-sections -fdata-sections \
   -DNQ_HACK -DNDEBUG -DELF -DDREAMCAST -DTYR_VERSION=... -DQBASEDIR="/cd" -Isource`
* `LDFLAGS` from KOS (`-Wl,--gc-sections` + the KOS link spec). **No** `-lSDL`.
* C file list = the existing target list **minus** `vid_sdl.c`, `snd_sdl.c`,
  `sdl_common.c`, `sys_unix.c`, `in_null.c` **plus** `vid_dc.c`, `snd_dc.c`,
  `in_dc.c`, `sys_dc.c`.
* Output `quake.elf`.

### 3.9 `scripts/dc/` — disc image pipeline (new)
Wrap the standard KOS pipeline (confirmed against KOS docs / dreamcast.wiki):
```sh
sh-elf-objcopy -R .stack -O binary quake.elf quake.bin
$KOS_BASE/utils/scramble/scramble quake.bin 1ST_READ.BIN
$KOS_BASE/utils/makeip/makeip IP.tmpl IP.BIN        # boot metadata
genisoimage -C 0,11702 -V QUAKE -G IP.BIN -joliet -rock \
    -o quake.iso  cd_root/        # cd_root/ holds 1ST_READ.BIN + id1/pak0.pak
cdi4dc quake.iso quake.cdi        # emulator/burnable image
```
`cd_root/` layout: `1ST_READ.BIN`, `id1/pak0.pak` (+ `pak1.pak` for full game).

---

## 4. Build & boot order (milestones)

**Phase 0 — Toolchain.** Install DreamSDK; confirm `kos-cc` builds and runs a
KOS hello-world in an emulator (lxdream / Flycast / Redream). No engine code yet.

**Phase 1 — Compile the engine core.** Add the `DREAMCAST` blocks to `port.h`
and `zone.c`; add `Makefile.dreamcast` with **stub** `vid_dc/in_dc/snd_dc/sys_dc`
(empty functions returning sane values, `Sys_Printf`→dbgio). Goal: it *links*
into an ELF. Flush out missing-symbol issues from removing SDL.

**Phase 2 — Boot + console.** Implement `sys_dc.c` (timer, file I/O over `/cd`,
`main`) and minimal `vid_dc.c` (clear screen + present a test pattern). Build a
CDI with `pak0.pak` and confirm Quake reaches the **console / menu** and reads
the pak from `/cd`. This proves memory sizing and data loading.

**Phase 3 — Playable video.** Finish `VID_Update` (8→RGB565 LUT + PVR present).
Confirm the game renders a level at 320×240 and check the frame rate.

**Phase 4 — Input + sound.** Implement `in_dc.c` (Maple controller → keys +
analog look/move) and `snd_dc.c` (AICA `snd_stream`). Now it is *playable*.

**Phase 5 — Polish (optional).** VMU save/config, CDDA music (`cd_dc.c`),
controller rebinding menu, performance tuning (heap size, `-particles`/dlight
limits), full-game `pak1.pak`, and BBA networking.

---

## 5. Known risks & mitigations

| Risk | Mitigation |
|------|------------|
| **16 MB RAM too tight** for hunk + surface cache + AICA mix buffers. | Start at 8 MB hunk; profile with `hunk`/`zone`/`cache` console commands; reduce surface cache or particle limits if needed. 320×240 keeps caches small. |
| **Software fill-rate** too slow for 60 fps. | Target 320×240; let PVR do the 320→640 upscale; tune `r_*` cvars; this matches known-good DC Quake performance. |
| **No command line on hardware** → must default correctly. | Hard-code argv and the 8 MB heap in `sys_dc.c`; don't rely on `-mem`. |
| **Save/config writes** — `/cd` is read-only. | Route writes to VMU (`/vmu/a1`) or no-op writes in phase 1; full VMU support in phase 5. |
| **Removing SDL** exposes hidden symbol deps (`d_8to16table`, `VGA_*`, `Sys_SendKeyEvents`). | Phase 1 link-only build surfaces these before any runtime work. |
| **Endianness / FP** — DC is little-endian SH-4. | Same byte order as existing targets; use `-m4-single-only`. The `LittleLong`/`LittleShort` macros already abstract this. |

---

## 6. Definition of done (first release)

* `make -f Makefile.dreamcast` under DreamSDK produces `quake.elf`.
* `scripts/dc/` produces a bootable `quake.cdi`.
* Boots on emulator and real hardware, loads `id1/pak0.pak` from `/cd`,
  reaches the menu, plays a shareware level at 320×240 with controller input
  and AICA sound, within the 16 MB RAM budget.

---

## 7. Implementation status

**Phase 1 (compile/link scaffolding) — done in this branch:**

* `source/port.h` — `DREAMCAST` → 320×240.
* `source/zone.c` — `Memory_GetSize()` capped at 8 MB for `DREAMCAST`.
* `source/sys_dc.c` — KOS init (`KOS_INIT_FLAGS(INIT_DEFAULT)`), timing via
  `timer_us_gettime64`, file I/O over newlib, fixed-argv `main()`, `basedir=/cd`.
* `source/vid_dc.c` — 320×240 8-bit offscreen → RGB565 present (2× pixel-doubled
  into the 640×480 framebuffer), palette LUT, surface-cache setup, direct-rect.
* `source/in_dc.c` — Maple controller polling → `Key_Event`, analog stick →
  `IN_Move`, triggers as `K_AUX1/2`.
* `source/snd_dc.c` — AICA `snd_stream` backend with a ring buffer pumped by
  `snd_stream_poll()` from `SNDDMA_Submit`.
* `Makefile.dreamcast` — `kos-cc` build; SDL backends swapped for the KOS ones.
* `scripts/dc/make_cdi.sh` — ELF → `1ST_READ.BIN` → ISO (+IP.BIN) → `.CDI`.

> These compile against the engine's symbol contract (verified statically), but
> have **not** been built with DreamSDK in this environment — the KallistiOS
> toolchain isn't present here. Phases 2–4 (boot-test on emulator/hardware, then
> tune video/input/sound) are the next step and require a DreamSDK build.

## 8. References

* Dreamcast Programming — IP.BIN and 1ST_READ.BIN: https://mc.pp.se/dc/ip.bin.html
* Creating a bootable Dreamcast disc (dreamcast.wiki): https://dreamcast.wiki/Creating_a_bootable_Dreamcast_disc
* KallistiOS SDK (toolchain reference, Docker example): https://github.com/Nold360/docker-kallistios-sdk
* DCEmulation — making a CDI from a KOS project: https://dcemulation.org/phpBB/viewtopic.php?t=104570
