# 215 - app_bobs: intención detallada

Documento de **intención** de la demo (gate `tools/check/demo-docs.mjs`): qué hace, qué pretende demostrar, qué hay en pantalla y cómo evoluciona en el tiempo. La evaluación del modelo de visión vive en `VISION.md` (prompt canónico, sin pistas).

## Qué hace

Demo de la **capa de BOBs de la fachada**: el juego describe cuatro actores con `screen().bobs(layer)` (sin `FramePlan`, `BobTarget`, `BlitJob` ni minterms). La capa es heterogénea (dos hojas de 16×16 a 2 planos: una amarilla y una roja), ordena por `z` (el actor 1 va delante) y cada actor lleva su `Anim` ligada (el engine la avanza con `layer.tick()`). Los actores rebotan en los bordes moviéndose ≤ 1 px por frame. El fondo (color 0) se limpia **entero una sola vez** (el buffer nace sin limpiar) y cada frame se limpia solo la **huella previa** de cada actor con `screen().clear_box(...)` (encolado en el plan, `D = 0`), no la pantalla.

## Qué pretende demostrar

1. **La capa de BOBs como vocabulario de juego**: el juego mueve actores y llama `s.bobs(layer)`; el orden por `z`, la animación ligada y la emisión al Blitter las resuelve el engine.
2. **Limpieza acotada frente a borrar la pantalla**: con objetos opacos que se mueven poco, limpiar la huella previa (una caja por actor) cuesta una fracción del relleno a pantalla completa y mantiene el frame dentro de un VBlank (medido: 49,92 fps / 1,002 campos, frente a 24,94 fps con el relleno completo).
3. **Displays del tamaño justo**: la paleta usa índices 0..3, así que el display es de **2 planos** (la mitad de fetch de bitplanes que 4); un modo más ancho solo añade DMA sin cambiar lo que se ve.
4. **La capa heterogénea**: cada actor elige su hoja (`sheet_index`) y el engine ordena la emisión; el solape rojo/amarillo muestra el `z` (el amarillo queda delante).

## Elementos en pantalla

- **Cuatro cuadrados de 16×16** sobre fondo oscuro (color 0): dos con la hoja amarilla (planos 0 y 1) y dos con la roja (plano 0); el actor 1 (amarillo, `z=200`) va delante del actor 0 (rojo, `z=100`) cuando se solapan.
- Cada actor tiene una animación de dos frames: un cuadrado **grande (14×14)** y otro **pequeño (8×8)**, 15 frames de juego cada uno; por eso el tamaño de los cuadrados cambia con el tiempo (y el recuento aparente en una captura puede variar por los solapes).

## Evolución temporal

- **Movimiento**: los cuatro actores se desplazan 1 px por frame en diagonal y rebotan en los bordes (el par 0/1 se mueve junto y se solapa: se ve el orden `z`); el resto de la pantalla es estático (color 0).
- **Animación**: cada actor alterna el cuadrado grande y el pequeño cada 15 frames de juego.
- **Estabilidad**: la limpieza por huella no deja restos (la visión canónica no observa artefactos ni rastros) y la cadencia medida es **49,92 fps / 1,002 campos/frame** en estado estable (`A500_debug`).
