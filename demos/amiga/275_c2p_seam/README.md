# 275_c2p_seam — C2P por el seam (`Scene::c2p`) [WIP]

Mismo efecto que la 061 (rotozoom chunky 4bpp + `row_repeat` + doble buffer), pero el
chunky→planar va por el **seam** `scene.c2p(request, &plan)` (ruta **Blitter**,
`BlitJobKind::C2P`, 13 fases) en vez del `c2p_1x1_4_asm` directo.

## Qué se corrigió (y por qué está aquí)

`Scene::c2p(req, plan)` → `DrawTarget::c2p(req)` usaba el `rasterizer()` del **playfield**
(CPU) e **ignoraba el `plan`**: devolvía `true` pero el `BlitJobKind::C2P` **nunca** se
encolaba. `DrawTarget::c2p` ahora enruta a `kBlitterRaster` cuando hay `plan`.

## Estado: **WIP (no verificado)**

`detail != 0` (≈9861): el planar que produce la ruta Blitter del seam **no** coincide con la
referencia CPU `c2p_1x1_4`. **Causa conocida**: el `C2p4` del `BlitterRaster` usa el buffer
`chunky` como **fuente + su 2ª mitad como scratch planar** de las 13 fases
(`eng/graphics/blitter_state.hpp`, `C2p4::chunky`; `amiga_c2p.cpp:15` `dst = chunky + bytes`).
Esta demo le pasa un chunky de `w*h` bytes y el contrato del seam exige el layout con scratch.
**Pendiente**: dar el layout correcto (o documentar el contrato en `C2pRequest`) y volver a
verificar; entonces `detail` debe ser `0`.

**Verificado**: HOST-218 (seam CPU == referencia; encolado del BlitterRaster) y la 061 siguen OK;
la 275 **compila y arranca** (READY), lo que confirma que el fix no rompe la ruta existente.

## Build/run

```bash
bash tools/build/build-demo.sh demos/amiga/275_c2p_seam --debug
WINUAE_SIDE_CHANNEL_PORT=2421 bash tools/run/run-demo.sh demos/amiga/275_c2p_seam --warp
```
