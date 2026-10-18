# INTENT — demo 303 `hud_icons`

## Qué hace

Demo de **inventario de iconos con selección y foco** sobre Amiga 500 (fase **H2** del [`ROADMAP_HUD.md`](../../../../../docs/guides/roadmap/ROADMAP_HUD.md)): un campo de juego estático a 4 planos y una **franja inferior de 48 líneas a 2 planos con paleta propia** (blanco/amarillo/rojo) que contiene una **rejilla 6×2 de celdas de 16×16** (inventario). El modelo `eng::hud` mantiene la rejilla (`IconGrid` con selección y foco independientes); `HudInput` (`eng/hud/input.hpp`) consume los `UiEvent` de ratón y joystick —hit-test por capa y por fórmula de rejilla, *click* = *down*+*up* en el mismo objetivo, `Fire`/`Accept` = *click* del foco— y mueve selección y foco, que el presentador pinta **por deltas**. El juego sondea la entrada con `eng::amiga::poll_mouse`/`poll_input` y no escribe registros.

## Qué pretende demostrar

1. **Entrada del HUD (H2)**: `HudInput` sobre el contrato de eventos compartido con `eng::ui`; la política de consumo sale del **contrato de región** (`HudRegionSpec::capture_input`), no de una copia en la demo.
2. **Selección y foco independientes**: el ratón mueve el *hover* y el *click* fija la **selección**; el joystick mueve el **foco** por filas/columnas (con bordes) y `Fire` selecciona la celda con foco.
3. **Hit-test por fórmula**: la celda se calcula al recibir el evento (`(p − origen) / pitch`), sin generar N rectángulos por frame.
4. **Presupuesto**: 50 fps (1 campo) con repintado **por deltas** (solo las celdas cuyo estado cambia) y la franja por CPU.

## Elementos en pantalla

- **Campo de juego (líneas 0–207)**: fondo negro con unos bloques estáticos de color (backdrop; no es interactivo).
- **Franja HUD (líneas 208–255)**: marco blanco de 1 px; etiqueta `ITEMS`; **rejilla 6×2** en (8,8) con celdas de 16×16: **objeto** = cuadro amarillo de 12×12 (los slots vacíos no tienen objeto); **hover** (ratón encima) = marco blanco; **foco** = marco rojo; **selección** = celda blanca con el objeto en rojo; texto de estado `SEL nn FOC nn` en x=112 (amarillo); línea de depuración `HUD ICONS vX.Y.BBBB F NNNNN FPS NN.N` en blanco.

## Evolución temporal

- **Con entrada real** (ratón del runner o joystick/pad): el *hover* sigue al ratón por la rejilla; el *click* fija la selección; el pad mueve el foco (un paso por flanco) y `Fire` selecciona.
- **Guion de demostración (attract)**: tras ~2 s sin entrada real, un recorrido determinista por el perímetro de la rejilla (5 derecha, abajo, 5 izquierda, arriba; un paso cada 20 frames) con un disparo cada 60 frames que selecciona la celda con foco — la demo se ve viva sin pad y la validación tiene una dinámica reproducible. Cualquier entrada real reinicia la espera del guion.
- Los números `SEL`/`FOC` de la franja reflejan el modelo en cada cambio (evidencia legible por el modelo de visión); el número de frames y el fps se repintan sin borrar (longitud fija).
