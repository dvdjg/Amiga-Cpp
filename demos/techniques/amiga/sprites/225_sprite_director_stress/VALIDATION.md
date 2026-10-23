# Validación — 225 SpriteDirector stress

## Build y ejecución

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/sprites/225_sprite_director_stress --debug
bash tools/run/run-demo.sh demos/techniques/amiga/sprites/225_sprite_director_stress --wait-ms 10000 --screenshot out/tmp/225_app_hud.png
node tools/debug/measure-fps.mjs 225_sprite_director_stress A500_debug --json
```

## Medida

`measure-fps` (perfil `A500_debug`, ventana de 8 muestras, 20,2 s):

```text
emulado=27.49 fps | host=27.45 fps | 258088 ciclos/frame (1.819 campos/frame)
muestras=27.21/27.80/27.18/27.73/27.50/27.39/27.73/27.34 | stable=true
```

**Excepción de presupuesto:** la demo es intrínsecamente pesada (composición de 8 entidades simples, una compuesta, banda de paleta y reconstrucción de copperlist cada frame). Queda por debajo de 50 fps pero por encima del suelo de 25 fps; se documenta como excepción, no se relaja el umbral. El HUD de depuración del host muestra el frame renderizado y los fps; la lectura del modelo de visión no se usa como medida.

## Visión

Evaluación final con el prompt canónico en [`src/VISION.md`](src/VISION.md). Secuencia de 8 frames cada 700 ms (`out/run/225_sprite_director_stress/A500_debug/sequence`), analizados los frames 0/2/4/5/7 con `qwen3-vl:8b-instruct-q8_0`.

Resultado: el modelo describe el fondo oscuro, la franja roja inferior, el texto `225 SPRITE DIRECTOR` con las líneas de frame/fps y estado, los sprites diagonales (incluido el par azul punteado), el BOB rectangular y el movimiento diagonal/descendente de los grupos; no reporta halos, recortes ni parpadeos. El OCR del HUD no es fiable (leyó `F36222756PB5`), por eso la medida de fps es `measure-fps`, no la lectura del modelo.

## Alcance

- Verificado: build, arranque, overlay de depuración, `SpriteDirector` en ejecución, banda de paleta, ausencia de artefactos visibles reportada por visión.
- Sin verificar por separado: conteo exacto de cada variante (el HUD muestra `HW6 BOB1 BAND20`; el modelo no distingue el compuesto de la tira). La contabilidad exacta de attached/tira/compuesto está cubierta por `HOST-485`, no por esta captura.
- Pendiente de decisión: si se quiere 50 fps, reducir entidades o mover la composición a un frame de actualización cada 2 VBlanks y documentar la cadencia; no se hace aquí porque el objetivo es el estrés de la fachada.
