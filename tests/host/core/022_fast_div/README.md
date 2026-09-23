# Test HOST-022: `fast_div` y utilidades de potencia de dos

Respalda la API de `engine/include/eng/core/fast_div.hpp` que usa el hot path del
campo de tiles (`TileFieldController`).

En 68000 **no hay multiplicación 32×32**, así que GCC no puede convertir una
división/módulo por una constante **no potencia de dos** en la secuencia de
"multiplicación mágica": acaba llamando a `__udivsi3`/`__umodsi3` (~150 ciclos)
incluso a `-Os`. Por una **potencia de dos** sí es un `lsr`/`and` (1-2 ciclos).
De ahí que el engine detecte potencias de dos y use shift/máscara.

Se comprueba:

- `eng::is_pow2` / `eng::ilog2` (runtime, para geometría que no llega como NTTP).
- `eng::asr_floor` = `floor(v / 2^shift)` también con negativos (C++20 define el
  shift aritmético como floor).
- `eng::fast_div<N>::q/r` exactos frente a `/` y `%` en un rango, para N potencia
  de dos (16, 256) y no potencia de dos (288, 768).

```bash
bash tools/run-host-tests.sh tests/host/022_fast_div
```

Contexto y auditoría de codegen: `docs/guides/optimization/OPTIMIZACION_GPP_68000.md`
(§11) y `tools/analyze/asm-audit.mjs`.
