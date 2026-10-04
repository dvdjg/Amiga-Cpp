# Mapa símbolo → página

Índice inverso: desde un **símbolo del código** (`engine/include/eng/…`) hasta la **página del manual**
que lo documenta (y viceversa). Sirve para que ningún módulo quede sin entrada y para saltar del código
a la explicación.

## Cómo se mantiene

- **Fuente → manual**: al crear una página de `04_referencia/<módulo>/`, añadir aquí una fila
  `<fichero> → <página>`.
- **Manual → fuente**: cada página de referencia cita `fichero:línea`; esos enlaces viven en la propia
  página.
- El **check de cobertura** (checklist del [índice](../README.md#5-cobertura-los-23-módulos--checklist-de-referencia))
  garantiza que todo módulo tenga sección; este mapa garantiza que se pueda **llegar** a ella.

## Mapa actual

| Fuente (`engine/include/eng/…`) | Página del manual |
|---|---|
| `api/game.hpp` | [04_referencia/api/game](../04_referencia/api/game.md) |
| `api/screen.hpp` | [04_referencia/api/game](../04_referencia/api/game.md) (§`Screen`) |
| `api/scene.hpp` | [04_referencia/api](../04_referencia/api/README.md) + [05_arquitectura/planificador_de_escena](../05_arquitectura/README.md) |
| `api/scroll.hpp` | [04_referencia/api](../04_referencia/api/README.md) + [01_tutoriales/08](../01_tutoriales/README.md) |
| `api/sprites.hpp` | [04_referencia/api](../04_referencia/api/README.md) + [01_tutoriales/07](../01_tutoriales/README.md) |
| `api/framebuffer.hpp` | [04_referencia/api](../04_referencia/api/README.md) + [05_arquitectura/c2p_y_framebuffer_indexado](../05_arquitectura/README.md) |
| `memory/memory_manager.hpp` | [04_referencia/memory/manager](../04_referencia/memory/manager.md) + [05_arquitectura/modelo_de_memoria](../05_arquitectura/modelo_de_memoria.md) |
| `memory/arena.hpp`, `block_pool.hpp` | [04_referencia/memory/arena](../04_referencia/memory/arena.md), [block_pool](../04_referencia/memory/block_pool.md) |
| `memory/mem_bank.hpp`, `chip_storage.hpp`, `stack.hpp` | [04_referencia/memory/banks](../04_referencia/memory/banks.md) |
| `core/types/typed.hpp`, `domains.hpp` | [04_referencia/core/types](../04_referencia/core/types.md) + [05_arquitectura/sistema_de_tipos](../05_arquitectura/sistema_de_tipos.md) |
| `core/types/span.hpp`, `box.hpp`, `ptr.hpp`, `memory_kind.hpp` | [04_referencia/core/types](../04_referencia/core/types.md) |
| `core/math/*` | [04_referencia/core/math](../04_referencia/core/math.md) |
| `core/data/*` | [04_referencia/core/data](../04_referencia/core/data.md) |
| `core/util/*` | [04_referencia/core/util](../04_referencia/core/util.md) |
| `graphics/copper/*` | [05_arquitectura/copper_y_scheduler](../05_arquitectura/README.md) (pendiente) |
| `graphics/c2p.hpp` | [05_arquitectura/c2p_y_framebuffer_indexado](../05_arquitectura/README.md) |
| `field/xlimited_*`, `strip_*` | [04_referencia/field](../04_referencia/README.md) (pendiente) |
| `os/*` (msg, IO, timers) | [05_arquitectura/mini_os_de_mensajes](../05_arquitectura/README.md) |
| `res/*` (assets, VFS, engz) | [04_referencia/res](../04_referencia/README.md) (pendiente) |
| `audio/*` | [05_arquitectura/audio_sintesis_y_streaming](../05_arquitectura/README.md) |
| `ai/*`, `board/*`, `cards/*`, `sim/*` | [04_referencia/ai](../04_referencia/README.md) (pendiente) |

> Los destinos marcados «(pendiente)» apuntan a su **área** hasta que exista la página; el mapa se
> completa a medida que el manual crece. Para un símbolo concreto, `grep -rn '<símbolo>'
> engine/include/` da su cabecera, y esta tabla, su página.

Volver a [Apéndices](README.md) · [índice del manual](../README.md).
