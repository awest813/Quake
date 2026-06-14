# Sega Dreamcast Port (DreamSDK / KallistiOS)

TyrQuake-based NetQuake for the **Sega Dreamcast**, built with **DreamSDK**
(KallistiOS).  Software renderer at 320×240; PVR presents a hardware-scaled
640×480 image; saves and config persist on the VMU in slot A1.

---

## Quick start

### CI (no local toolchain)

Every push builds `quake.elf` and a bootable `quake.cdi` via GitHub Actions
(`.github/workflows/dreamcast.yml`) using the `nold360/kallistios-sdk` image.

### Local build (DreamSDK / KOS shell)

```bash
# Source your KOS environment first (kos-cc on PATH, KOS_BASE set).
make -f Makefile.dreamcast
sh ./scripts/dc/make_cdi.sh quake.elf quake.cdi
```

### Playable disc (requires your own `pak0.pak`)

Quake data files are not redistributable.

```bash
QUAKE_ID1=/path/to/id1 ./scripts/dc/build_disc.sh
./scripts/dc/run_flycast.sh    # if Flycast is installed
```

Attach a **VMU to port 1 slot A** in Flycast or on hardware for
`config.cfg` and save-game persistence.

---

## Architecture

| Layer | File(s) | Role |
|-------|---------|------|
| System | `sys_dc.c` | KOS init, timer, `/cd` basedir, main loop |
| Video | `vid_dc.c` | 8-bit offscreen → RGB565 PVR texture → 640×480 quad |
| Input | `in_dc.c` | Maple pad: stick turn/move, triggers strafe, d-pad look |
| Sound | `snd_dc.c` | AICA `snd_stream` ring buffer |
| VMU I/O | `vmu_dc.c` | VMS package wrap/unwrap for `/vmu/a1` |
| Filesystem | `common.c` | Read `/cd/id1`; write flat `/vmu/a1` |
| Build | `Makefile.dreamcast`, `scripts/dc/` | `kos-cc` link + CDI packaging |

Resolution and heap: `port.h` (`320×240`), `zone.c` (`8 MB` default hunk).

---

## Controller (default)

| Input | Action |
|-------|--------|
| Stick X | Turn |
| Stick Y | Walk forward / back |
| L / R trigger | Strafe |
| D-pad | Look / strafe (digital) |
| A | Jump |
| B | Attack |
| X | Use |
| Y | Run |
| Start | Menu |

Bindings are applied after `quake.rc` via `IN_DC_ApplyBindings()`.

---

## Phase status

| Phase | Scope | Status |
|-------|--------|--------|
| 0 | Toolchain / emulator | Manual (DreamSDK + Flycast) |
| 1 | Compile, link, CI CDI | **Done** |
| 2 | Boot, `/cd` data load | **Done** (needs runtime test with `pak0.pak`) |
| 3 | PVR video present | **Done** |
| 4 | Pad input + AICA sound | **Done** |
| 5 | VMU saves (VMS headers) | **Done** |
| — | CDDA, BBA net, tuning | Deferred |

Runtime validation with real shareware data on Flycast/hardware is the
remaining step before calling the port fully verified.

### Audit fixes (polish pass)

* **Input:** `IN_DC_ApplyBindings()` runs after `exec quake.rc` so keyboard
  defaults from the pak do not override the pad layout.
* **Sound:** `stream_cb` ring-buffer read handles wrap and zero-fills underruns;
  callback buffer is 32-byte aligned; `smp_req`/`smp_recv` are bytes (KOS
  `snd_stream_fill` convention for 16-bit PCM).
* **Video:** `dcache_flush_range` before PVR texture upload; guard if PVR init
  failed; UV coords sample texels `0..319` / `0..239` so Po2 padding is never
  read.
* **VMU:** VMS `app_id` set to `TyrQuake\DC`; simple icon; error logging on
  failed writes; `DC_FClose(NULL)` safe.
* **Saves:** `load` closes the VMU staging file if map spawn fails; parse
  errors close the file before `Sys_Error`.
* **Demos:** demo recording uses `DC_FOpen`/`DC_FClose` so `.dem` files are
  VMS-packaged on the VMU like saves.
* **Filesystem:** `COM_FOpenFile` skips unreadable VMU paths instead of crashing
  `COM_filelength`.

---

## CI artifacts

| Artifact | Description |
|----------|-------------|
| `quake.elf` | SH-4 ELF (~3.7 MB) |
| `cd_root/1ST_READ.BIN` | Scrambled boot binary |
| `IP.BIN` | Boot metadata |
| `quake.cdi` | Bootable disc image (no game data in CI) |

---

## References

* [Dreamcast port plan (detailed)](DREAMCAST_PORT_PLAN.md)
* [KOS SDK Docker image](https://github.com/Nold360/docker-kallistios-sdk)
* [Bootable DC disc (dreamcast.wiki)](https://dreamcast.wiki/Creating_a_bootable_Dreamcast_disc)
