# 129 — split-screen por bandas + dibujo **split-aware**

Dos **bandas** horizontales (una por "vista"/bitmap) compuestas con el mismo `eng::scene::RasterLayout`
que el DPF, y **dibujo repartido por banda**: una barra vertical que cruza la línea de corte se
**parte** con `eng::scene::for_each_band_part` —la mitad superior va al bitmap de la banda 0 y la
inferior al de la banda 1— de modo que el código de dibujo **no sabe** cuál es cuál (etapa 4 del
planner, `ROADMAP_GAME_API.md` §7).

- Banda 0 (arriba, rojo): bitmap A. Banda 1 (abajo, azul): bitmap B; `ModeSwitchZone` en la línea 128.
- El fondo de cada banda es sólido (colores de referencia); la barra blanca las cruza y sale
  **continua** aunque cada mitad vive en un bitmap distinto.

## Qué ilustra

- Composición por **bandas** (`RasterLayout` + `Band`) para split-screen.
- El primitivo **split-aware** `for_each_band_part`: un `fill`/`draw` dirigido a su banda.
- `band_from_view` como fuente de cada banda (el algoritmo de scroll escribiría su `PlayfieldHardwareView`).

## Build / run

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/129_split_screen_bands --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/129_split_screen_bands --warp
```
