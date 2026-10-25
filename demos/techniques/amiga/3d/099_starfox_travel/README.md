# 099 - Star Fox style foreground 3D

Laboratorio de **relleno de poligonos por bitplane**: Arwing, dos cazas enemigos, un arco y un edificio 3D. No hay fondo de cielo/terreno: solo foreground sobre negro. La camara mantiene yaw/pitch/roll y el jugador zigzaguea; no hay entrada, colision, puntuacion ni audio.

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/099_starfox_travel --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/099_starfox_travel --config A500_release
```

## Tecnica

- **4 bitplanes de display, solo 3 renderizados**: los objetos usan los planos 0..2 y los **7 colores opacos** 1..7. El plano 3 queda a 0 (negro); el color 8 no se usa.
- Triple buffer. Cada frame se limpian solo los 3 planos de foreground.
- **Ruta por defecto: per-plane por objeto**. Para cada objeto se calcula su lista de caras visibles, su bbox y se llama a `fill_polygons_by_plane_dirty` con **3 pasadas de Blitter**. El bbox por objeto (no el de toda la escena) es lo que da la mejora: menos clear y menos area fill.
- Solo foreground: Arwing, enemigos, disparos, arco, edificio y HUD.
- Arwing 23 vertices/31 triangulos; enemigo 11/14. Arco y edificio 3D con caras n-gon; painter por `z`, sin z-buffer.
- El plano 3 no se toca en el render.

### Modos de prueba

`K099_FILL_MODE`:

- `0` = CPU exacto por cara (3 planos). **No usable todavia**: el edificio cuelga la medida.
- `1` = per-plane por objeto (**por defecto**, el más rápido medido).
- `2` = router automático con histórico CPU/planos y calibración de 2 frames.

El router se implementó y se probó. En esta escena **no gana**: la ruta CPU para arco/naves es más lenta que per-plane, y el CPU del edificio se bloquea. El histórico anterior/actual sirve para calibrar, pero el reparto “pequeño CPU / grande Blitter” no se cumple: el per-plane por objeto es mejor incluso para las naves.

## Rendimiento

- Release A500, modo 1 (per-plane por objeto): **3,76 fps**, 1.884.396 ciclos/frame, 13,28 campos/frame (ventanas 3,3-3,9).
- Modo 2 (router auto): 3,11 fps, 2.281.111 ciclos.
- Per-plane global (una pasada para toda la escena): 3,26 fps.
- CPU de referencia anterior (4 planos con fondo): 3,34 fps.
- **No cumple 6 fps.** El modo 1 sí supera al CPU y al per-plane global.

## Validación

- `VISION.md` describe el Arwing, el portal/arco, el bloque gris y **movimiento** ligero entre frames; contornos limpios y fondo negro uniforme.
- `verify-3d-sequence` sigue reportando “area 3D vacia” en los primeros frames por el detector de área sobre fondo negro; no se usa como prueba única.
- `flicker-check`: 7/7 frames con cambio, 7 candidatos / 102 bloques bajos; baseline 36/220; pasa.
- 088 y 090 con `blitter_line_draw(..., false)`: capturas limpias.\n- Regresión de 099 (`out/regression/20261025-130312/`): OK, incluido flicker. El gate de análisis genérico marca `yellow=0` por falta de overlay específico.

Vision: [`src/VISION.md`](src/VISION.md). Medidas: [`VALIDATION.md`](VALIDATION.md). Intención: [`src/INTENT.md`](src/INTENT.md).

## Limites conocidos

- El clear de bbox por objeto borra lo que haya detrás dentro del bbox; con fondo negro es aceptable, pero una API general necesita scratch+S.
- El CPU por cara es más lento que per-plane en esta escena y el edificio CPU se cuelga.
- El objetivo >6 fps sigue abierto.
- Sin commit; el cambio de línea es de engine y falta regresión de 088/090.
