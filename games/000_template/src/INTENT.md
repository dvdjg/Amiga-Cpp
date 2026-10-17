# 000 - template: intención detallada

Documento de **intención** del juego-plantilla (gate `tools/check/demo-docs.mjs`): qué hace, qué pretende demostrar, qué hay en pantalla y cómo evoluciona en el tiempo. La evaluación del modelo de visión vive en `VISION.md` (prompt canónico, sin pistas).

## Qué hace

Plantilla de juego del engine para Amiga: un juego 2D cerrado **sin bajar al metal**. La lógica usa solo la fachada (`eng/api/api.hpp`): `App`, `Screen`, `Anim`, input y escenas. `main` declara el display (320×256×4 con paleta azul/blanco/rojo/verde) y `run_demo` pone memoria, arranque y bucle; no hay `SysBase`, soporte de plataforma, registros, `FramePlan` ni copperlist en el juego. El juego empuja una **escena de título** (espera fuego o 150 frames) y cede a la **escena de juego**, donde un cuadrado rojo de 32×32 se mueve en pasos de 16 px con el pad. El fondo, el logo y el objeto van por el **plan del frame** (`Screen::fill_box`, relleno diferido del Blitter) y el HUD se pinta con `eng::debug::draw_hud` (franja inferior reservada, una reescritura por segundo).

## Qué pretende demostrar

1. **El camino canónico de una app Amiga**: `main` imperativo + `run_demo(game, display)`; el juego no declara `SysBase` ni `g_eng_run_status` ni incluye `support/` (criterios de `API_ERGONOMICS.md`).
2. **Escenas como composición del juego**: `push_scene`/`set_scene` conducen el bucle sin que el juego gestione estados globales; la transición título→juego es la del motor.
3. **Relleno a coste cero con el Blitter**: `fill_box` es un `FillRect` por plano encolado en el plan (D = A con A constante), en orden con los sprites; el juego pide el rectángulo y el color, no el minterm ni el módulo.
4. **HUD barato y sin plomería**: `draw_hud` escribe por palabras (dos glifos por palabra de 16 px) sobre la franja inferior; la franja se excluye de los rellenos de fondo y se limpia una sola vez con un `fill` inmediato.
5. **Entrada por el pad, no por el hardware**: la escena lee `app.input()`; el backend sondea los puertos.

## Elementos en pantalla

- **Escena de título**: fondo azul (color 1) y barra «logo» verde (color 4) de 224×32 en (48,112); sin fuente (no hay texto de juego).
- **Escena de juego**: fondo azul y cuadrado rojo (color 3) de 32×32 que se mueve con el pad en pasos de 16 px (alineados a palabra); la animación lógica (`Anim` de 4 frames a 6 frames de juego cada uno) avanza cada frame.
- **HUD** en la franja inferior (filas 248..255): `TEMPLATE v<major>.<minor>.<build> F <frame>` en blanco (color 2) sobre el azul del fondo.

## Evolución temporal

- **Título**: estático; se dibuja **una sola vez** (el buffer persiste y no se reencola el plan cada frame, de modo que la captura no coincide con un relleno a medias). Solo cambia el contador del HUD (~1 vez/segundo); tras 150 frames (3 s a 50 fps) o al pulsar fuego, `set_scene` cambia a la escena de juego.
- **Juego**: el cuadrado rojo se mueve con el pad (izquierda/derecha/arriba/abajo) y el fondo/objeto se redibujan cada frame por el plan; el HUD solo cambia el contador.
- **Nota de captura**: con el runner en `--warp`, la captura del canal lateral puede coincidir con la ejecución del plan y mostrar un relleno a medias (barra del logo negra o ausente); la captura del monitor (`screenshot.png`) y las secuencias sin `--warp` sobre escenas estáticas muestran el estado estable. Un frame suelto sin logo en una escena que reencola el plan cada frame no implica fallo del render.
