# HOST-148: null-move, tiempo, PV/Multi-PV y análisis paralelo

Test host de `eng/board/search/{pruning,time,search}.hpp`.

## Qué enseña / comprueba

1. **Null-move pruning** (`ChessSearcherNull`): pasar turno y buscar con profundidad
   reducida corta ramas tranquilas; se desactiva en jaque y finales. Encuentra el
   mate y da la misma mejor jugada que sin poda.
2. **Gestión de tiempo** (`TimeManager`): reloj inyectable (VBlank/CIA en Amiga,
   `std::chrono` en host); el test usa un reloj falso. Límites blando y duro, y de
   nodos.
3. **PV / Multi-PV**: `analyze_move` devuelve la puntuación exacta y la línea
   principal; `search_multi_pv` devuelve las N mejores candidatas ordenadas.
4. **Análisis paralelo**: `search_multi_pv` reparte la puntuación de cada jugada de
   la raíz con `eng::parallel::for_each_index` (secuencial en Amiga, hilos en host) y
   el resultado es determinista (mismo ranking con 1 o 4 hilos).

## Salida de referencia

```
Ajedrez B5:
OK: B5 (null-move, tiempo, PV/Multi-PV, analisis paralelo determinista)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/board/148_chess_b5
```
