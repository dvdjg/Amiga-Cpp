# 010 - chip_slow_memory: intención detallada

Documento de **intención** de la demo (gate `tools/check/demo-docs.mjs`): qué hace, qué pretende demostrar, qué hay en pantalla y cómo evoluciona en el tiempo. La evaluación del modelo de visión vive en `VISION.md` (prompt canónico, sin pistas).

## Qué hace

Demo del **modelo de memoria Chip/Slow** con el arranque de alto nivel: `main` declara el display (320×256×4 con paleta mínima) y el **presupuesto por banco** (`MemoryConfig`: 64K chip, 32K slow, 8K frame); `run_demo` pone memoria, arranque y bucle. En `init` reserva recursos persistentes por **banco tipado** (`MemoryManager::chip()/slow()` → `Block<Tag, Banco>`): plantilla de copperlist, presupuesto de bitplanes, pool de entidades y blob de guion en Slow; y comprueba que una reserva de 64K en Chip **no cabe** y devuelve un bloque **inválido** (sin excepciones). La scratch de frame la gestiona el engine y se lee con `app.telemetry()`. El panel de depuración (`app.debug()`) muestra el estado y el HUD de la franja inferior (nombre + versión + frames) la norma del repo.

## Qué pretende demostrar

1. **La memoria del engine es por banco y por tipo**: los recursos DMA (copperlist, bitplanes) van a `chip()` con su Tag de dominio; los datos de CPU a `slow()`; el compilador impide pasar un banco equivocado a DMA.
2. **La reserva que no cabe no es una excepción**: `reserve<Tag>` devuelve un `Block` inválido y la demo lo verifica (el caso negativo es parte del contrato).
3. **La scratch de frame es del engine**: el juego no toca la arena LIFO; `app.telemetry()` reporta su uso real junto a los bancos persistentes.
4. **El arranque ergonómico cubre presupuestos explícitos**: el overload `run_demo(demo, display, MemoryConfig)` fija el perfil de la demo sin nombrar backend, `SysBase` ni `g_eng_run_status`.

## Elementos en pantalla

- **Panel de depuración** (ventana del depurador): recuadro con el título «AMG demo 010 - Chip/Slow memory model», el perfil de máquina, «Arena checks: OK/FAIL», tres barras de uso (Chip DMA, Slow metadata, Frame scratch) y las capacidades en hexadecimal.
- **Display 320×256×4** con paleta mínima (COLOR00 azul oscuro, COLOR01 blanco): el fondo queda azul oscuro y solo se rotula el HUD.
- **HUD** en la franja inferior (filas 248..255): `CHIP/SLOW v<major>.<minor>.<build> F <frame>`.

## Evolución temporal

- **Frame 0**: `init` reserva los bloques y valida el caso negativo; el primer `render` rellena el panel y el HUD, y al terminar el primer `present` la demo marca READY.
- **Estado estable**: estática; las barras reflejan el uso reservado (constante) y lo único que cambia es el contador `F` del HUD, reescrito ~1 vez/segundo. La visión canónica no observa cambios ni artefactos (`VISION.md`).
