# 011 - typed_memory: intención detallada

Documento de **intención** de la demo (gate `tools/check/demo-docs.mjs`): qué hace, qué pretende demostrar, qué hay en pantalla y cómo evoluciona en el tiempo. La evaluación del modelo de visión vive en `VISION.md` (prompt canónico, sin pistas).

## Qué hace

Piloto de la **API de memoria tipada** del engine con el arranque de alto nivel: `main` declara el display (`GameDisplay` con paleta negra/blanca) y el presupuesto de memoria (`MemoryConfig`), y `run_demo` pone el resto (memoria, `start`, bucle, telemetría). En `init` reserva un bloque DMA desde el banco Chip (`MemoryManager::chip().reserve<PlaneTag>` → `Block<PlaneTag, Chip>` → `MemView`, la dirección DMA vive en el tipo) y un buffer de CPU con `fast_or_slow` (Fast RAM si la máquina la tiene, si no Slow), y marca READY cuando ambos son válidos. En `render` vuelca el resultado al canal de depuración (`app.debug()`: cuatro líneas con el estado) y rotula la franja inferior con `eng::debug::draw_hud` (nombre + versión + contador de frames), una reescritura por segundo.

## Qué pretende demostrar

1. **Que la procedencia de un buffer DMA se garantiza por el tipo**: el bloque de planos nace de `chip()` con `Block<PlaneTag, Chip>` y expone `MemView`; pasar RAM Slow/Fast no compila, así que la demo no puede equivocarse de banco ni necesita comprobaciones en runtime.
2. **Que la elección Fast/Slow para CPU es una política reutilizable**: `fast_or_slow` decide en una sola llamada y el banco efectivo queda legible (`m_sim.kind`), sin que la demo sondee tipos de memoria ni llame a Exec.
3. **Que el arranque ergonómico cubre también presupuestos explícitos**: el overload `run_demo(demo, display, MemoryConfig)` fija el perfil de la demo (64K chip para el display 320×256×4 y el bloque de planos, 4K slow, 1K frame, 16K fast) sin nombrar backend, `SysBase` ni `g_eng_run_status`.
4. **El HUD barato del repo**: `draw_hud` escribe por palabras (`Screen::text_words`) sobre 1 bit de tinta, sin bucle por píxel ni texto por frame.

## Elementos en pantalla

- **Display 320×256×4** con paleta negra/blanca: el fondo queda negro (ningún camino de dibujo de bitplanes lo pinta); la demo no dibuja escena propia, su contenido es de depuración.
- **Canal de depuración** (ventana del depurador): cuatro líneas —«Memoria tipada: OK/FAIL», «Planos DMA: Block<PlaneTag,Chip> + MemView», «Fast RAM: si/no» y «Sim buffer: Fast/Slow»—, que resumen el resultado de las reservas.
- **HUD** en la franja inferior (filas 248..255): `TYPED MEM v<major>.<minor>.<build> F <frame>` con la versión inyectada por el build y el contador de frames de juego.

## Evolución temporal

- **Frame 0**: `init` reserva los bloques (sin trabajo por frame después) y el primer `render` rellena el canal de depuración y rotula el HUD; al terminar el primer `present` la demo marca READY y el runner puede capturar.
- **Estado estable**: la demo es **estática** (no hay `update`); lo único que cambia es el contador `F` del HUD, que se reescribe ~1 vez/segundo. Sin parpadeo ni artefactos: la evaluación de visión (`VISION.md`) no observa cambios entre frames de la secuencia.
