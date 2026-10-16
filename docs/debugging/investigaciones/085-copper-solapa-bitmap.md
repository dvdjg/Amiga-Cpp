# 085: disco negro en uno de cada dos frames (solape de la copperlist con el bitmap)

## Síntoma

En `085_copper_plan_scene`, **la mitad (o una fracción) de los frames** muestra el disco (BOB) en
**negro** en lugar de con su arcoíris por línea; otros frames se ven correctos. Las capturas cada
400 ms (≈20 frames) caen siempre en la misma paridad y el defecto parece permanente según el
momento de la captura; a 350 ms alterna (6076/0/6076/3124 px negros interiores medidos).

Es **preexistente** al refactor tipado de copper (2026-10): reproducido con el binario de `HEAD`
en un `git worktree` limpio, sin cambios locales (mismo conteo alterno de píxeles negros).

## Evidencia de runtime (WinUAE-DBG, GDB + canal lateral)

Con el build de 3 planos (`BPLCON0=0x3200`), pausado en un frame con disco negro:

| Dato | Valor |
|---|---|
| `BPL1PT`/`BPL2PT`/`BPL3PT` | `0x17860` / `0x1A060` / `0x1C860` (3 planos de 10240 B) |
| bitmap 0 implícito | `[0x17860, 0x1F060)` |
| bitmap 1 implícito | `[0x1F060, 0x26860)` |
| `COP1LC` | `0x25060` (= un bloque del doble buffer del `Plan`) |
| dump `0x25060` | copperlist viva (`00 96 ...`, `01 82 ...`) |

`0x25060 ∈ [0x1F060, 0x26860)` → **el bloque de copperlist que el Copper ejecuta está dentro
del rango del bitmap 1**: al mostrar el bitmap 1, las palabras de la lista se leen como bits de
los planos (los píxeles del disco pasan a índices con bits de más → COLORxx = 0x000 = negro).
El defecto no correlaciona de forma simple con la paridad porque la posición del disco y el
rango parcheado cambian cada frame (patrón de batido con el intervalo de captura).

## Por qué el validador de regiones no lo caza (abierto)

- Con esas direcciones, `RegionTable::validate()`/`validate_disjoint()` devuelven **código 1**
  (solape parcial), verificado con una sonda host sobre las direcciones medidas.
- Sin embargo, la validación **dentro del `init` de la demo** (registrando bitmaps, bloques del
  plan, shape y zero con `validate_disjoint()`) **pasa**: la demo llega a READY con
  `detail=0xF1F2` (241 intenciones, 242 palabras), no con el código `0x85xx` de fallo.
- Conclusión parcial: **las direcciones vistas en `init` difieren de las de runtime** o algo
  reasigna/mueve memoria después. Es la pista principal: instrumentar el allocator (log de
  direcciones por `reserve`) y comparar con `COP1LC`/`BPLxPT` en runtime. Contexto: los
  refactors recientes del `MemoryManager`/`MemBank` (`244edd4b`, `d1ae280d`) introdujeron
  free-list real, y hay trabajo en curso en `backend.hpp`/`amiga.cpp` por otro hilo.

## Hipótesis descartada: el 4.º plano

La primera lectura (BPL4PT = COP1LC en un build de 4 planos) sugería que el plano extra leía la
copperlist. **Descartada**: con 3 planos (`BPU=3`, sin puntero de plano 4) el defecto persiste.
El cambio a 3 planos se mantiene por corrección semántica (el disco usa bits 1/3/5 y el display
no necesita más), pero **no cierra** el defecto.

## Estado

- **Abierto**: bloqueado por el layout de memoria en runtime (arriba). La demo 085 tiene además
  la validación de regiones en su `init` (`validate_disjoint`), que hoy pasa y **debe fallar**
  cuando el allocator quede corregido si el solape es real en ese momento.
- Ver también: `VALIDATION.md` de la demo (defecto registrado) y `PROCEDIMIENTO_DEMOS_Y_JUEGOS`
  §secuencias (por qué una captura única no basta).
