# HOST-476: controlador de presentación (`eng::graphics::PresentController`)

Test host de la pieza de presentación de F2 (`engine/include/eng/graphics/present_controller.hpp`): junta la política de paneo (`pan_window.hpp`), el pacing medido (`render_pacing.hpp`) y la **publicación diferida al inicio de campo**, y decide por frame qué se muestra y qué se rinde.

## Qué comprueba

1. **Publicación diferida**: el primer frame es un Full de ancla; `commit(pan)` deja el Full pendiente y el `begin` del frame siguiente reporta `publish` (una publicación por commit, y solo una); el offset de display del Full publicado es el paneo avanzado desde su render (p. ej. 1 px), creciendo en los fakes sobre el mismo buffer.
2. **Clamp al margen**: el offset de la ventana se clampa al `margin_x` de la política (arriba y abajo).
3. **Racha adaptativa**: `observe_full(ciclos)` re-ajusta `max_consecutive_fakes` vía `affordable_fakes` (con ~2 campos de coste, K=0 → Full casi cada frame; la corrida de 12 frames cuadra fulls+fakes).
4. **Gate asíncrono por hardware**: `async_ok` es falso sin Fast RAM y verdadero con ella (`eng::hw::has_fast_ram`), y el paso lo refleja — la base del camino de cola de blits por IRQ condicionado a hardware (veredicto medido de la campaña 218 en el roadmap).

## Salida de referencia

```
OK: present controller (publicacion diferida, offsets, racha y gate async)
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/graphics/476_present_controller
```

## Notas

- El controlador **no toca hardware**: mantiene el estado (política, pacing, pendiente, ancla del publicado) y devuelve decisiones (`PresentStep`); el llamador aplica el cambio de buffer y el parcheo de la ventana, y aporta el coste medido del render.
- Integrado en la demo 080: su `update()` delega la decisión en el controlador (la telemetría por frame no cambia — refactor neutro — y la secuencia de contrato del verificador adaptativo sigue en verde).
