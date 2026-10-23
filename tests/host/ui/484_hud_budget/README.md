# HOST-484: presupuesto y degradación del HUD (`eng/hud/budget.hpp`)

Test host de la fase **H4** del HUD: `engine/include/eng/hud/budget.hpp`. Ver `HUD_SYSTEM.md` §6.4 (backing declarado), §6.6 (presupuesto) y §6.7 (escalera de degradación); plan en `ROADMAP_HUD.md` (H4).

## Qué comprueba

1. **Coste de *backing* byte a byte contra referencia**: `hud_backing_bytes` con filas a palabra (ancho no múltiplo de 16, ancho 1), alto 1, 6 planos (EHB) y caja vacía; `hud_backing_fits` en el límite exacto, una fila/palabra de más y sin presupuesto declarado.
2. **Conflicto de canales de sprite**: máscaras disjuntas, con un bit común, idénticas y sin reserva previa.
3. **Escalera determinista bajo presupuesto simulado**: sin configurar nunca degrada; sube un nivel por frame con exceso; mantiene en la banda de histéresis (7/8..presupuesto); baja con holgura sostenida; tope en `max_level` (recortado a `kHudDegradeMaxLevel`); telemetría `changes()` exacta y reproducibilidad con la misma secuencia de medidas.
4. **Telemetría por región** (`HudRegionStats`/`HudStats`): acumulación con `add` (sumas, OR de máscaras de sprite, nivel máximo), reinicio por frame con `reset` (conserva `changes`) y estado degradado agregado.
5. **Comprobador de presupuesto** (`hud_budget_check`): primer campo excedido en el orden de la escalera (ops, blits, Copper, backing, sprites fuera de la máscara reservada, BOBs), límite exacto dentro y presupuesto 0 = sin límite (salvo la máscara de sprites).

## Salida de referencia

```
HOST-484 hud_budget: OK
```
