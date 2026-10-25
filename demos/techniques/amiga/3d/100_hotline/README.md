# 100 - HOTLINE (Superhot-like first-person, 2.5D)

Prototipo de juego first-person por nodos inspirado en Superhot: el tiempo solo avanza cuando el jugador se mueve. Los personajes son **mallas esqueletadas** de bajo poligonaje sobre una sala 2.5D con fondo pre-renderizado, 3 bitplanes y relleno de siluetas por Blitter. La UI también se dibuja con polígonos.

## Estado

**Vertical slice funcional.** Arranca, dibuja la sala, tres enemigos con volumen de cajas, coberturas, arma y HUD de polígonos. Tiempo detenido, disparo, colisión, muerte, victoria y reinicio están implementados. La demo se auto-demuestra a los 6 s sin input (dispara y retrocede; el modo attract completa la sala).

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/100_hotline --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/100_hotline --config A500_release
```

## Diseño

- 320x240 jugables + 16 px de HUD; **3 planos** (negro, blanco, dos grises, tres rojos principales y un rojo profundo) y 3 buffers.
- Cuatro vistas pre-renderizadas de la sala; el jugador avanza/retrocede por nodos y gira 90 grados.
- **Personajes rigged (malla única)**: siete segmentos del mismo cuerpo (cabeza, visor, torso, dos brazos y dos piernas), agrupados en un esqueleto de seis huesos. Cada segmento se transforma por su hueso y se proyecta con la cámara 2.5D; el relleno es la **envolvente convexa** de sus 8 vértices world. Cabeza rojo claro, torso rojo medio, visor rojo oscuro; brazos gris claro y piernas gris oscuro. `Enemy::anim` avanza con cada tick y mueve brazos/piernas; `aim` levanta los brazos cuando el enemigo ve al jugador. Todas las piezas comparten el mismo rig y el mismo objeto de malla, no son actores separados. Es una malla no convexa (un cuerpo articulado no puede ser un único poliedro convexo).
- **Coberturas** como cajas grises con la misma proyección; la línea de visión 2D bloquea disparos y el fuego enemigo.
- **UI poligonal**: chevron del logo, pips de enemigos vivos, indicador de estado, arma y mirilla. Sin texto en el HUD.
- Tiempo detenido: solo `up/down` avanza el mundo (enemigos y balas). Girar y disparar no mueven el tiempo.
- Un impacto mata. Disparo por raycast 2D al centro de la mirilla; los enemigos disparan proyectiles que se esquivan moviéndose.
- El fondo de la sala se copia con Blitter; el HUD ocupa las filas 208..239.

## Rendimiento medido

`measure-fps` (A500_release, ventana de 12 s): **9,57 fps emulados**, 740.874 ciclos/frame, 5,22 campos/frame, rango 5,24..15,76 fps. La cadencia es inestable porque el juego solo repinta en acciones/ticks (no persigue 50 fps continuos). La escena usa 23 segmentos como máximo (7 por enemigo vivo + 2 coberturas), cada uno con relleno de 3 planos por cookie-cut.

## Validación

- Captura limpia con los tres personajes articulados, coberturas y HUD: `out/vision-review/100_hotline_rig.png`.
- Secuencia de attract (`out/run/100_hotline/A500_release/sequence`): los enemigos se mueven en cada tick, el disparo elimina pips y termina en victoria.
- `flicker-check`: 55 candidatos deterministas asociados a la animación discreta del rig (poses entre ticks), muertes/coberturas y el OSD de entrada de WinUAE; una revisión de visión independiente describe brazos/piernas articulados de forma coherente y **sin corrupción ni piezas flotando**. El detector no distingue bien una pose discreta de un glitch; no se usa como gate de esta animación.
- La inyección de joystick del runner (`--joy`) y del MCP no llegó al juego en esta sesión; el modo attract valida las funciones de tiempo/disparo/reinicio, pero el camino `poll_input` con joystick físico queda **SIN VERIFICAR**.

## Límites actuales

- La cadencia de acciones es de ~10 fps y variable con 3 planos; falta perfilar por secciones para bajarla a un presupuesto estable.
- El HUD no tiene texto: la información se da con pips y estado; las instrucciones están aquí.
- Sin portada, menú, audio ni varios niveles.
- Sin commit; demo nueva fuera de la regresión global.
