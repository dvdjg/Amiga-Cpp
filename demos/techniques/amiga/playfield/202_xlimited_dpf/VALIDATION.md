# Validación — Demo 202 (`xlimited_dpf`)

Intención: dual playfield 3+3 con scroll XLimited y mapa real; debe sostener 50 Hz PAL.

## Medición entregada

```text
202_xlimited_dpf/A500_release | emulado=48.7–50.2 fps según fase | 141435–145789 ciclos/frame | 1 campo/frame
202_xlimited_dpf/A500_debug   | emulado=49.9 fps (ventana corta) | HUD a 1/8 de refresco
```

La captura final muestra el mapa, las plaquettes y la franja inferior completos; el HUD de una línea aparece en el margen seguro.

Ventana larga de 12 s (`A500_release`, commit `2a760c23`): **48,37 fps**, 146663 ciclos/frame y 1,034 campos/frame. Las cuatro primeras ventanas de 1 s dan 49,3–50,6 fps; la cadencia cae a 46–47 fps en las cuatro siguientes. En la fase lineal horizontal el desglose medido es `update` BG 22,9k + `update` FG 29,0k + `compose` 21,6k + resto ≈14k = **87,4k ciclos de CPU activos** de los 141876 disponibles por campo: quedan ~55k ciclos libres, por lo que la caída **no es falta de CPU del bucle**; el límite está en el tiempo de pared (DMA/Blitter/VBlank), que el contador de CPU no desglosa.

## Diagnóstico y cambios

El cuello de botella no era la copperlist: el compositor dual ya usa `patch()` (~2-3k ciclos). La medición por secciones dio `update_scroll` ~131k ciclos, `compose` ~20k y `install` ~0.5k. El coste estaba en el scroll progresivo de dos campos:

- El campo lineal/mirror duplicaba blits y el scroll por píxel generaba trabajo de mapa en cada sub-paso.
- `add_draw` ejecutaba un bloque de instrumentación `DEBUG` por bloque, con divisiones/módulos en el camino caliente.
- El perfil progresivo no aprovechaba la geometría cacheada del bloque.

Correcciones:

- Eliminado el bloque `DEBUG` de `XLimitedPlayfield::add_draw`.
- `draw_block_job_for_block` cachea el bloque de mapa y evita resolverlo tres veces por copia/espejo.
- La demo entregada usa `kShareY=true` y `max_step=1`; se documenta la reducción frente a la variante de Y independiente.
- HUD de host a 1/8 de refresco.

## Deuda

La variante de **Y independiente** (`kShareY=false`) sigue sin caber en 50 Hz con el DPF 3+3 completo; requeriría un pre-render con lookahead o reducir la escena. No se declara válida como variante de 50 Hz.
