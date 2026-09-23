# HOST-218: C2P en el seam (`Rasterizer::c2p`)

Test host del **chunky→planar a través del seam** `eng/field/raster.hpp`: la misma llamada
`Rasterizer::c2p(C2pRequest)` sirve con CPU o Blitter detrás.

## Qué comprueba

1. `CpuRaster::c2p` acepta la petición y convierte un chunky 16×4×4bpp.
2. El resultado es **idéntico** a la referencia `c2p_1x1_naive` (4 planos).
3. Píxeles conocidos: `(1,0)=1` fija el bit del plano 0; `(0,0)=0` no fija ninguno.
4. Con 6 planos cae a la vía naive (`c2p_1x1_naive`) sin fallar.
5. `BlitterRaster::c2p` con un `FramePlan` y 4 planos **encola** un `BlitJobKind::C2P`
   (el backend ejecuta las 13 fases); sin plan, cae a CPU.

Es la unificación de la interfaz: el llamador pide `c2p` sin saber si detrás hay CPU
(`c2p_1x1_4`, merge de Kalms) o el Blitter por fases. `Scene::c2p(req, plan)` expone el seam.

> La ruta Blitter en hardware (`c2p_4bpp_step`) está portada de la demo 080; una demo que la
> consuma por el seam queda pendiente (ver roadmap).

## Salida de referencia

```
OK: seam C2P (Rasterizer::c2p) valida chunky->planar.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/graphics/218_seam_c2p
```
