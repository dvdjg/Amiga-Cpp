# Concurrencia y hilos (`eng::parallel`)

`engine/include/eng/parallel/parallel.hpp` es la **frontera de concurrencia** del
engine: la lógica escribe contra `eng::parallel` y nunca contra `<thread>` ni
registros, de modo que el mismo algoritmo se compila en un Amiga de una sola CPU y en
una máquina moderna multinúcleo. El objetivo es permitir optimizaciones paralelas
(p. ej. búsqueda adversaria, generación de tablas, análisis offline) sin
comprometer el target clásico.

## 1. Dos backends, una API

```text
                       ┌─────────────────────────────┐
                       │   lógica del engine/juego    │
                       │   usa eng::parallel::…       │
                       └──────────────┬──────────────┘
                                      │
                 ┌────────────────────┴────────────────────┐
                 ▼                                         ▼
      ┌──────────────────────┐                ┌──────────────────────────┐
      │ CLÁSICO (m68k)       │                │ MODERNO (host)           │
      │ una CPU, sin hilos   │                │ varias CPUs              │
      │ ─ sincronización no- │                │ ─ std::thread/mutex/     │
      │   op (1 contexto)    │                │   atomic/condition_var   │
      │ ─ hardware_threads() │                │ ─ hardware_threads()     │
      │   == 1               │                │   == nº de CPUs          │
      └──────────────────────┘                └──────────────────────────┘
```

La selección se hace en compilación con `#if defined(__m68k__)` (la convención del
engine, ver `fixed_math.hpp`/`c2p.hpp`): el cruce `m68k` no incluye la STL hosted.
El backend clásico mantiene la API con **no-ops** y `hardware_threads() == 1`, de
forma que los algoritmos caen a su ruta secuencial sin ramas en el sitio de llamada:

```cpp
if (eng::parallel::hardware_threads() > 1u) {
    eng::parallel::for_each_index(work, grains, [&](eng::u32 i) { process(i); });
} else {
    for (eng::u32 i = 0; i < work; ++i) { process(i); }
}
```

## 2. Inventario de la API

| Tipo / función | Papel | En m68k |
|---|---|---|
| `hardware_threads()` | Nº de CPUs utilizables | `1` |
| `has_threads()` | ¿Soporta hilos? (constante de compilación) | `false` |
| `Mutex` / `LockGuard` | Exclusión mutua y cierre RAII | no-op |
| `Atomic<T>` | `load`/`store`/`fetch_add`/`exchange`/`compare_exchange` | `volatile T` |
| `ConditionVariable` | `wait`/`wait(pred)`/`notify_one`/`notify_all` | `wait` no-op |
| `Thread` | Arrancar/`join` un hilo real | `start` devuelve `false` |
| `StopSource` / `StopToken` | Cancelación cooperativa de trabajos | funcional |
| `for_each_index(count, grains, fn)` | Reparto de iteraciones entre hilos | bucle secuencial |

`for_each_index` usa hasta `kMaxWorkers` hilos auxiliares **sin memoria dinámica**
(array fijo) y el hilo llamador también trabaja; el reparto de índices es con
`Atomic<u32>::fetch_add`.

## 3. Reglas de uso

- **Ningún camino por frame del Amiga crea hilos**: en el A500 no hay hilos y el
  paralelismo es cooperativo ([BACKGROUND_TASKS.md](BACKGROUND_TASKS.md), cola
  `eng::task::BackgroundQueue`). `eng::parallel` se usa en `init`, análisis offline o
  en plataformas modernas.
- **Consultar `hardware_threads()` antes de repartir**: garantiza una única ruta de
  código y que el comportamiento en el clásico es reproducible.
- **Cancelación con `StopSource`/`StopToken`**: los trabajos largos (búsqueda,
  empaquetado) consultan `stop_requested()` en cada rebanada y terminan limpiamente.
  La fuente debe sobrevivir a los tokens.
- **Determinismo de resultados**: el reparto de trabajo no puede cambiar el
  resultado. Cuando el orden importa (búsqueda con poda, acumulaciones), el
  algoritmo debe fijar el resultado independientemente del número de hilos (p. ej.
  reducir de forma ordenada o usar criterios estables).
- **Sin STL en m68k**: la STL hosted solo aparece en el backend moderno; las
  cabeceras de la lógica no incluyen `<thread>`.

## 4. Relación con `eng::task`

`eng::task::BackgroundQueue` sigue siendo el mecanismo de "trabajo en el hueco" del
Amiga (una sola CPU). `eng::parallel` no lo sustituye: es la capa que permite que un
algoritmo, además de trocearse cooperativamente, se reparta entre CPUs cuando el
target las tiene. Un mismo motor puede usar las dos: rebanadas cooperativas por
frame y `for_each_index` para lotes de `init`/análisis.

## 5. Consumidor previsto: búsqueda adversaria

El primer consumidor es `eng::board` ([BOARD_GAME_AI.md](BOARD_GAME_AI.md)):
la búsqueda de ajedrez/Go puede repartir el **root split** (cada hilo explora una
parte de las jugadas de la raíz con su propio `StopToken`) o usar *lazy SMP* sobre la
tabla de transposición cuando `hardware_threads() > 1`. En el A500 el mismo código
corre secuencial y el presupuesto lo fija el `MemoryPlan`.

## 6. Verificación

HOST-137 ejercita `hardware_threads`, `Mutex`/`LockGuard`, `Atomic` (con contadores
exactos entre 4 hilos), `Thread`, `ConditionVariable` (un worker despierta por
predicado), `StopSource`/`StopToken` y `for_each_index` (cada índice exactamente una
vez). La ruta clásica (no-ops) la cubre el cruce `m68k`, no el binario del host.
