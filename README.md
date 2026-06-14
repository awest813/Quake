This is a port of Quake for devices like the RS-97, Arcade Mini, PAP K3S... that rely on SDL 1.2.
(SDL 2.0 is too slow on those platforms)

It used to be based around sdlquake but that port had numerous crashes and issues so i rebased it around TyrQuake,
which was the only suitable base that was not Darkplaces.

TyrQuake only supported SDL2 so i had to reuse some of the sdlquake's code in there but it was mostly compatible with it.

It also runs pretty well on the said platforms, even on the PAP K3S with a screen resolution of 800x480.

## Sega Dreamcast

A native KallistiOS port lives on branch `cursor/dreamcast-phase2-playable-89ff`
([PR #2](https://github.com/awest813/Quake/pull/2)).  Build with
`make -f Makefile.dreamcast`, package with `scripts/dc/build_disc.sh` (requires
your own `id1/pak0.pak`).  See [DREAMCAST_PORT_PLAN.md](DREAMCAST_PORT_PLAN.md)
for architecture, controller layout, and phase status.
