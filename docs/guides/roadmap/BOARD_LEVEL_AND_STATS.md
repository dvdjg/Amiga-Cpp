# Board games: nivel de juego, footprint y estadística multidimensional

Estrategia para (a) ajustar el nivel de juego por recursos, (b) medir de forma fiable
el cómputo, (c) comparar configuraciones con estadística y (d) cazar/reparar bugs.
Continúa `docs/debugging/investigaciones/board-selfplay-and-perf.md` (coherencia y primera medida Amiga).

## 1. Estrategia de nivel por footprint

El nivel se ajusta combinando **módulos** (qué conoce/calcula el motor) y
**presupuesto** (cuánto calcula por jugada), no solo profundidad. El footprint objetivo
manda qué módulos caben.

| Perfil | Footprint | TT | Libro | Finales | Null-move | Presupuesto/jugada | Estilo por defecto |
|---|---|---:|---|---|---|---:|---|
| `P20` | 20 kB | ~256 entradas | no (o mini en RAM) | no | no | ~16 nodos | codicioso |
| `P64` | 64 kB | ~1 k | mini RAM | no | no | ~48 nodos | agresivo |
| `P256` | 256 kB | ~8 k | RAM | básico | sí | ~128 nodos | equilibrado |
| `P512` | 512 kB | ~16 k | RAM | sí | sí | ~512 nodos | posicional |
| `P1M` | 1 MB | ~64 k | + libro en disco | sí | sí | ~4 k nodos | multi-estilo |

Notas:

- El **libro de aperturas** siempre activo (barato y mejora mucho el nivel percibido);
  los **cierres/finales** son opcionales según RAM.
- El **estilo** (`aggressive_weights`/`positional_weights`) es ortogonal al footprint:
  se elige por jugador para enfrentar estilos.
- El nivel "humano-like" se logra bajando presupuesto y desactivando módulos, no
  introduciendo errores aleatorios.

## 2. Medición fiable del cómputo

Unidad base: **nodos visitados** (exacta y reproducible en host y Amiga). Para estimar
la carga del Amiga desde host:

```
ms_A500 ≈ nodos × ciclos_por_nodo / 7.09379e6
```

`ciclos_por_nodo` se calibra con `tools/debug/measure-fps.mjs` sobre la demo (medido:
~170 k ciclos/nodo con búsqueda de una sola llamada; ~425 k con rebanadas de 20 nodos).
Alternativas/complementos:

- **Unidades de trabajo (WU)**: `nodos + α·generate_legal + β·make/unmake`, para no
  depender solo de nodos cuando cambie la generación. El contador vive en el `Searcher`
  y lo expone el selfplay.
- **Ciclos emulados**: el contador del periférico de depuración da ciclos reales en el
  Amiga; sirve para recalibrar tras cada optimización.
- El selfplay reportará por jugada: nodos, WU, profundidad y `ms_A500` estimado.

## 3. Estadística multidimensional

Dimensiones:

- **footprint** (`P20`…`P1M`), **estilo** (agresivo/posicional), **módulos**
  (libro/finales/null-move), **presupuesto** (nodos), **color**, **arranque**
  (estándar/Chess960 con semilla).

Métricas por celda (agregadas sobre N partidas):

- victorias/derrotas/tablas (por estilo y por color), media de plies,
  nodos/jugada y WU/jugada, profundidad media;
- **tasa de errores**: comparar cada jugada elegida con un análisis profundo
  (`analyze_move` a d≥7) y medir la pérdida de evaluación (blunder/inaccuracy),
  útil para explicar *por qué* gana un estilo;
- determinismo (misma semilla/config → misma partida) y coherencia (`--verify`).

Salida: una tabla CSV/Markdown por barrido + gráficos (ASCII o generados con las tools
de `tools/`). El barrido es reproducible: `selfplay <N> --variant chess960 --seed S
--swap --move-nodes M --verify` por perfil.

## 4. Estrategia de bugs

1. `selfplay --verify` en cada campaña (clave incremental, legalidad, round-trip).
2. `tests/host/board/140_chess_movegen` (perft) tras cualquier cambio de reglas/movegen.
3. `analyze_move` sobre jugadas sospechosas para distinguir bug de horizonte.
4. Cada bug corregido añade una regresión host.

Historial de esta campaña: **2 bugs de motor** (búsqueda que perdía la mejor jugada
parcial al abortar; enroque Chess960 con solape rey/torre en make/unmake) + las
regresiones HOST-143/154/160.

## 5. Plan de implementación

1. `--profile <P20|P64|P256|P512|P1M>` en selfplay: fija TT, módulos, presupuesto y
   estilo; valida el footprint real (`size` del `.map`/BSS).
2. Contadores de WU en `Searcher` y salida `--accounting` (nodos, WU, `ms_A500`).
3. Barrido multicelda con salida CSV/MD y un gráfico resumen.
4. Tasa de errores vía `analyze_move` sobre cada partida (opt-in, más lento).
5. Optimizar `generate_legal` (pines/jaque sin make/unmake por jugada) y medir la
   mejora con `measure-fps`; recalibrar `ciclos_por_nodo`.
6. Con el host a nivel decente, fijar el perfil jugable en Amiga y actualizar la demo.
