# HOST-350 — petición de memoria en 2 ejes (`eng::MemSpec`) + `resolve_bank`

Respalda `engine/include/eng/memory/mem_spec.hpp`: pedir memoria con **dos ejes** en vez de un enum
con todas las combinaciones (evita la explosión combinatoria):

| Eje | Valores | Naturaleza |
|---|---|---|
| `MemReq` (requisito) | `Any` / `Chip` / `NonChip` | **duro** (corrección) |
| `MemHint` (preferencia) | `None` / `Fast` / `Slow` | **blando** (rendimiento) |

- `resolve_bank(MemSpec, MemAvail)` → `MemoryKind` físico: `Chip` solo Chip (o ninguno); `NonChip`
  nunca Chip (Fast/Slow según hint); `Any` preserva Chip para el DMA (Fast/Slow primero según hint).
- `MemoryKind` sigue siendo el **medio físico** (dato); `MemSpec` es la **petición**.
- Conexión: `MemoryManager::allocate(MemSpec, bytes)` (vía runtime, no-DMA) y, para DMA,
  `chip().reserve<Tag>()` (tipado).

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/350_mem_spec
```
