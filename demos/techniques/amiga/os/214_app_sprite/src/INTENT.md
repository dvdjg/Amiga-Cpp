# INTENT - 214_app_sprite

## Qué hace

Tutorial del camino de **objeto de juego por la fachada de alto nivel** (`App`/`Screen`): el juego describe su sprite (`eng::graphics::Sprite` con máscara y cookie-cut) y lo pinta con una sola llamada `screen.sprite(...)`; la geometría del destino la prepara la escena y `app.present()` ejecuta el plan de Blitter. La segunda vía (mundo retenido: `add_actor` + `draw_world`) usa el **mismo descriptor** (`actor_desc_from_sprite`), de modo que la representación (sprite HW o BOB) la elige el engine sin que el juego la fije.

## Qué pretende demostrar

- Que un juego pinta objetos **sin ver** `BlitJob`, punteros ni registros: la transición sprite↔BOB es transparente porque el `Visual` conserva su identidad.
- Que un **fondo grande** (640×256) se pre-renderiza a planar **una vez** (c2p CPU) y cada frame el Blitter copia solo la ventana visible (palabra de origen + shift fino): re-materializar por CPU no cabe en un frame (DT-030).
- El **doble buffer**: se dibuja en el back mientras el front queda visible (sin tearing).

## Elementos en pantalla

- **Fondo estructurado** 640×256 (cresta de horizonte + tablero de marcadores en colores 1/2/3 sobre navy) que **scrollea 2 px/frame** en bucle.
- **Dos objetos** con cookie-cut de disco (máscara + anillo): el sprite directo (`screen.sprite`, y = 96 + ciclo) y el actor del mundo retenido (y = 176 + ciclo), ambos con **x e y** y velocidades distintas.
- **Depuración**: contadores `fps10`, `copy_bad` (fallos de blit del fondo) y `scroll` (fase) por el periférico de depuración (ventana de debug del emulador; la demo no pinta texto).

## Evolución temporal

1. **Init**: backdrop indexado → planar (c2p), hoja del sprite, alta del actor; READY en el frame 4 (con el buffer ya publicado).
2. **Frames**: el scroll avanza 2 px (con wrap sobre la ventana extra del asset); el sprite y el actor recorren sus trayectorias x/y a distinta velocidad; el Blitter copia la ventana del fondo y dibuja los BOB en el back; `present()` publica.
3. **Telemetría**: cada segundo se publica `fps10`; cada frame `scroll`; `copy_bad` se queda en 0 si todos los blits se materializan.
