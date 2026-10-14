# HOST-441 - Coherencia de setup

Test host del **sistema de coherencia de configuración** (`eng/setup/coherence.hpp`; diseño en
`docs/engine/architecture/COHERENCIA_DE_SETUP.md`): valida que los valores que una demo
programará en el hardware son mutuamente consistentes, **antes** de que el fallo se vea en
pantalla. Cada caso del test es un **fallo histórico real**:

- **Regiones**: solape parcial entre bloques/sub-bloques (una hoja de BOB que rebasa su bloque
  e invade la región siguiente), contención padre-hijo (permitida), región fuera del banco de
  memoria y desbordamiento de dirección.
- **BPLCON0**: `BPU=5` con 4 planos emitidos (el «plano fantasma» de la demo 066, que leía
  `BPL5PT` con basura), falta del bit COLOR, HAM≠6 planos, DBLPF impar.
- **Orden de WAITs del Copper**: un WAIT que retrocede no espera a su línea y el efecto se
  pierde hasta el frame siguiente (las bandas de la demo 065); el par de overflow PAL
  (`0xffdf`/`0xfffe` de `CopWaitSafe`) reinicia el orden y el `END` corta el escaneo.

Los validadores son puros (`constexpr`, sin hardware): el mismo código sirve en la demo
(capa 2), en `static_assert` (capa 1) y en este test (capa 3).

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/debug/441_setup_coherence   # solo este
bash tools/run-host-tests.sh                                       # todos
```
