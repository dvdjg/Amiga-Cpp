# HOST-401 — HUNK con banco por segmento (R6.3)

`eng/res/hunk.hpp`: `HunkImage::load(image, MemoryManager&, MemoryPolicy)` reserva **cada hunk en su
banco** según los flags del formato (`HUNKF_CHIP` → Chip obligatorio, `HUNKF_FAST` → Fast, sin flag →
`FastPreferred`) con fallback explícito; los bloques se conservan (banco efectivo en `block.kind`) y
`unload(mem)` los libera.

## Qué comprueba

- Con Chip/Slow/Fast: un hunk sin flag cae en **Fast**; uno con `HUNKF_CHIP`, en **Chip**.
- Sin Fast: el hunk sin flag cae en **Slow**.
- `owns_memory()` y `unload` que **restaura** los bancos.
