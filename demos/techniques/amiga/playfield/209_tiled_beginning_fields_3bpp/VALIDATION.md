# VALIDACIÓN — 209 Beginning Fields 3bpp

## Estado

Demo funcional: Beginning Fields a **3 planos / 8 colores** con scroll flat (solo punteros), cámara Catmull-Rom 16.8 y animación de fuego/flores por paleta. La secuencia muestra desplazamiento continuo y cambios de color; no hay bandas alternas. El modelo de visión canónico describe la escena como estática (falso negativo ya conocido en detalles de 16 px); prevalecen el frame-diff y las capturas.

## Asset y paleta

- Fuente: `out/assets/tiled/beginning-fields-live-flowers/composed.png` (no hay TMX en el repo; se usa el compuesto existente). El bake base se usa solo para extraer la máscara real de las celdas vivas.
- `png2amiga --mode lores --depth 3 --dither floyd-steinberg --no-scale --chipset ocs` → 8 colores, PSNR 29,45 dB, S2 4,98. Atkinson: 29,29/4,53; `opt-checker`: 26,16/−12,47 (bandas). Elección: Floyd–Steinberg.
- `tools/tiled/make-3bpp-anim-map.mjs` reserva los índices 6/7 para flores/fuego y emite un mundo planar de 153.600 B. El resto de píxeles que usaban 6/7 se remapea al color más cercano de 0..5.

## Rendimiento

`node tools/debug/measure-fps.mjs 209_tiled_beginning_fields_3bpp A500_release --json`:

| Build | fps | ciclos/frame | campos/frame | Estable |
|---|---:|---:|---:|---|
| `A500_release` (6 px/frame por arco) | 49,89 | 142.178 | 1,002 | sí (min 49,78 / max 50,04) |
| `A500_demo_turbo_release` (16 px/frame) | 49,92 | 142.102 | 1,002 | sí (min 49,92 / max 49,92) |

El scroll no ejecuta blits: el coste es la reemisión de la copperlist (`compose`/`install`) y el HUD de host. No hay diferencia medible entre 6 y 16 px/frame (misma ventana de 1 campo).

## Scroll 16.8

La cámara se calcula en Q8 (`catmull_q8`, puntos y retorno px<<8). El compositor convierte a píxel entero (`>> 8`) al fijar `BigBufferScroll::position`; el `BPLCON1`/`BPLxPT` lo deriva `map_flat_scroll`. Paso normal: **6 px/frame constantes** mediante tabla de longitud de arco; build `EXTRA_DEFINES=-DDEMO_TURBO`: 16 px/frame. La secuencia de 80 frames consecutivos (`--sequence-step-frames`) dio 80/80 hashes únicos; el desplazamiento es ~12 px de host por frame (~6 px Amiga) y se detiene en las paradas, donde cambian las paletas.

## Flicker

`node tools/vision-review/flicker-highfreq.mjs --demo ... --frames 12 --config A500_release` → `bands=ok`, `candidates=37`, `max_blocks_low=1007`. Las bandas alternas están descartadas; los candidatos deterministas corresponden al desplazamiento global de la cámara (phase correlation ~14 px por par en la ventana de movimiento), no a una alternancia de escena. No se declara `flicker-baseline.json` con 0 candidatos porque la cámara está siempre en movimiento.

## Visión

- `src/VISION.md` (prompt canónico, 6 frames): describe la escena y los datos del HUD, pero afirma que es estática; falso negativo de movimiento global.
- Evidencia objetiva: `tools/vision-review/frame-diff.mjs --sequence ...` muestra cambios de ~145.000 px por par con bbox de casi toda la pantalla y SSIM 0,39; la cámara avanza (HUD `X=100→107` en frames consecutivos).
- Consulta dirigida de paleta/dither (`ollama-desc`, frames 0/4/8): identifica casas, caminos, agua y vegetación con 8 colores y confirma el avance de cámara (`X=160→220`); no reporta bandas ni zonas ilegibles. El modelo confunde la perspectiva («ángulos»), así que se usa como filtro de sospecha, no como prueba.

## Deuda / SIN VERIFICAR

- No se ha probado el asset con TMX (no hay TMX en el repo): la procedencia es `composed.png`, documentado.
- La animación de paleta se ha verificado con la build de diagnóstico `-DDEMO_FREEZE_FIRE` (cámara fija en la celda de fuego, tile 16,29): la secuencia de 12 frames muestra la llama amarilla en el frame 0 y naranja en el 11, sin que cambie el resto del mapa. Sin verificar en la visita completa (el fuego sale de plano según la parada).
- El modo turbo se selecciona en compilación; no hay conmutación en runtime.
