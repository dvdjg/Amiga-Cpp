# HOST-019 — Extensiones de Copper (copper chunky)

Valida la codificación **exacta de libgfx** que necesita el efecto `plasma` (display
*copper chunky*): `SKIP`, `MOVE32` y parcheo de instrucciones.

## Qué cubre

- **`ListBuilder::skip(vpos, hpos)`**: `word0 = (vpos<<8)|((hpos>>1)|1)`, `word1 = 0xffff`
  (misma codificación que `CopSkip`; el bit 0 de la máscara distingue WAIT `0xfffe` de
  SKIP `0xffff`).
- **`ListBuilder::move32(reg, addr)`**: orden del original (`CopMove32`): primero `reg+2`
  (word bajo), luego `reg` (word alto).
- **`move_at` + `patch_data`**: emitir una instrucción y parchear su word de valor (el
  patrón del plasma: una instrucción `COLOR00` por bloque, parcheada cada frame como
  `CopSetColor`).
- `COP2LCH`/`COP2LCL`/`COPJMP2` añadidos al enum `Register`.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/019_copper_ext
```

Contexto: `docs/demos/effects/PLASMA_PORT_PLAN.md`.
