# Demo 083 — ruido fbm (value noise + fbm de `eng/core/noise.hpp`)

Muestra el **ruido procedural** del engine como un **mapa de altura animado** sobre un
display *copper chunky* de 288×256 (rejilla de 36×64 bloques de 8×4 píxeles): la CPU
reescribe los `COLOR00` de la rejilla y el Copper los pinta por bloques, sin bitplanes.

## Qué enseña (técnica exclusiva)

Un campo **fbm 2D** (suma de octavas de value noise) genera un relieve que no se puede
dibujar con formas sencillas: cada píxel de la rejilla es un valor de ruido en `[0,1)`
que la paleta traduce a agua / orilla / verde / roca / nieve. La animación desplaza el
muestreo del campo, de modo que el terreno "fluye".

## Presupuesto en 68000

Evaluar fbm por bloque y por frame sería inasumible (miles de evaluaciones de ruido). Por
eso el fbm se evalúa **una vez** sobre una rejilla gruesa de 16×16 y por frame solo se
**muestrea bilinealmente** con un offset animado: el coste por frame es un muestreo, no
ruido. El init usa `eng::math::fbm2<MiniFloat16>` (3 octavas) con coordenadas y escalas
construidas sin `float` (`from_int` por bits + 0.125/256 exactos).

## Invariantes

- La rejilla de bloques es 36×64 (`kCols`/`kRows`); el campo grueso, 16×16.
- El offset de muestreo va en Q8 de celda gruesa y hace ping-pong (0..3) en ambos ejes:
  sin costura y sin salir del campo.
- La paleta (256 entradas RGB12) se interpola entre paradas fijas por altura.

## Comandos

```bash
bash ./tools/build/build-demo.sh demos/amiga/083_fbm_noise --debug --clean
bash ./tools/run/run-demo.sh demos/amiga/083_fbm_noise
bash ./tools/analyze/analyze-demo.sh demos/amiga/083_fbm_noise
```

El analizador de captura (`analyze-screenshot.sh`) exige contenido no-azul-Workbench y
presencia de verde (terreno); una pantalla vacía o de arranque no pasa.
