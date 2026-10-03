# HOST-244: geometría e invariantes del scroller de tiras (anillo de Copper)

Codifica como código verificable la **referencia de "Copper ring + incoming strip"**
(`docs/debugging/investigaciones/consulta-scroll-optimizacion*.md`): constantes compile-time y, sobre
un modelo de anillo en sombra, los invariantes del camino rápido hacia 50 fps.

## Qué comprueba

- **Constantes** (`static_assert`): anillo 368 px = 23 words, 46 B/planelínea, `BPL1MOD/BPL2MOD = 184`,
  `BLTDMOD` columna alta = 44, columna = 16 tiles × 80 planelíneas = 1280, `guarda ≥ ceil(step/16)+1`.
- **Guarda**: la columna destino del blit nunca cae dentro de la ventana visible (se pinta lo invisible).
- **Cobertura**: tras cada cruce de palabra, **toda la ventana visible está pintada** (nunca se revela un
  píxel sin pintar), en 20 000 pasos aleatorios de 1..16 px × 40 semillas, adelante y atrás.
- **Límite de hardware documentado**: el split XY (`0x2c + 256`) **excede el VPOS de 8 bits** del Copper
  OCS (0–255) → para un viewport de 256 líneas hay que usar el modo lineal/espejo, no el split.
