# INTENT — demo 302 `hud_band`

## Qué hace

Demo de **HUD de juego en franja propia** sobre Amiga 500: un campo de juego a 4 planos con un actor que rebota y una **franja inferior de 48 líneas a 2 planos con paleta propia** (blanco/amarillo/rojo) compuesta con `ModeSwitchZone` en la línea de display 208. El HUD usa el modelo `eng::hud`: un contador de puntos (6 dígitos con ceros a la izquierda), un medidor de energía lineal y 4 *pips* de vidas; el presentador `HudStrip` materializa las operaciones (`Fill`/`Frame`/`Text`/`Digits`) sobre un `CanvasPlayfield` en Chip con `Surface`. El juego no escribe registros: declara la región y el `App` compone el display con `RasterLayout`.

## Qué pretende demostrar

1. **Contrato de región H0**: una región `Band` con presupuesto y paleta propia es declarativa; el HUD no toca el chipset.
2. **Modelo portable + presentador de franja (H1)**: `Counter`/`Meter` actualizan solo cuando el valor visible cambia (`any_dirty`) y `HudStrip` los pinta por CPU de referencia; el borrado de la zona dinámica lo emite el presentador de la demo.
3. **Franja con modo gráfico propio**: la banda usa 2 planos y su propia paleta en el corte de Copper; el campo de juego conserva sus 4 planos y no se redibuja la franja completa.
4. **Presupuesto**: la demo debe mantener 50 fps (1 campo) sin tirones ni flicker, con el HUD repintado solo cuando cambia.

## Elementos en pantalla

- **Campo de juego (líneas 0–207)**: fondo negro; suelo azul oscuro en la base; bloques estáticos de colores y un **actor cuadrado claro** (16×16) que rebota horizontalmente.
- **Franja HUD (líneas 208–255)**: marco blanco de 1 px; etiquetas `SCORE`, `ENERGY`, `LIVES`; contador de 6 dígitos en amarillo (cuenta los frames); barra de energía amarilla que se vacía y llena (diente de sierra de 120 frames; pasa a rojo por debajo del 25 %); 4 pips de vidas (amarillo = viva, rojo = perdida; pierde una cada 50 frames y las recupera en el ciclo); línea de depuración `HUD BAND vX.Y.build F <frames> FPS <fps>` en blanco.

## Evolución temporal

- El **contador** avanza cada 10 frames (5 Hz; el repintado de dígitos es el pico más caro del HUD y el texto se formatea solo al cambiar el valor).
- La **energía** baja de 60 a 0 y vuelve a subir en ciclos de 120 frames; el ancho de la barra es `valor × 2` píxeles (sin multiplicaciones ni divisiones). El presentador pinta **por deltas**: solo el tramo que cambia; al cruzar el 25 % repinta la barra completa para cambiar a rojo sin dejar dos tonos.
- Las **vidas** cambian cada 50 frames (4 → 3 → 2 → 1 → 0 → 4); el pip que cambia se repinta solo.
- El **actor** recorre el ancho de ida y vuelta (2 px por frame) por el **sistema de objetos** del engine con `BackgroundPolicy::SaveUnder`: el Blitter restaura el fondo que tapa, guarda el nuevo y dibuja (los bloques estáticos quedan intactos). El render pinta primero la capa viva (blits) y después la franja (CPU).
- La línea de depuración `HUD BAND vX.Y.BBBB F NNNNN FPS NN.N` es de **longitud fija** (5 dígitos de frames, 4 de fps) y se repinta sin borrar la fila (sobrescribe las mismas celdas): frames cada 100, fps al cerrar cada ventana de 1 s.
