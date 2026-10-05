# Filosofía y reglas

Las decisiones que explican **por qué** el engine es como es. Leer esto evita escribir código que
«funciona» pero rompe el modelo.

## 1. Abstracciones de coste cero

Una abstracción del engine **no debe costar ciclos por sí misma** en el camino caliente: se compila a
lo mismo que el código «de metal». Herramientas del repo: `tools/check/frame-hot-path.mjs` (prohíbe
construcciones/copias por frame) y `tools/analyze/asm-audit.mjs` (prohíbe instrucciones 68020+/FPU).

```cpp
// La cámara es un dato, no hardware: moverla no toca registros.
m_cam_x += vx;                      // el vocabulario de juego
app.add_scroll_layer(m_layer);      // el App la conduce; el motor parchea el Copper
```

## 2. «La app pide; el engine dispone»

El juego expresa **intención** (mover el fondo, cambiar un color, colocar objetos); el engine **decide
el cómo** y **posee los recursos**. El juego **no** toca planos, registros ni Blitter. Este contrato se
usa incluso para **consumidores externos** (p. ej. un emulador): definen su vocabulario y un adaptador
sobre `eng/api/api.hpp`, sin que el engine se acople a ellos (`docs/engine/architecture/ROADMAP_API_COHERENCE.md` §7).

## 3. Sin heap, sin excepciones, sin RTTI

- **Memoria**: arenas por banco (**Chip/Slow/Fast**) que el backend reserva una vez; el motor reparte
  **bloques tipados** (`Block<Tag>`). El *scratch* por frame tiene arena propia (`reset_frame_scratch`).
- **Errores sin excepciones**: `[[nodiscard]] bool`/`util::Expected<T, E>`; un fallo en `init` se
  publica con un código (ver §5). El `frame` **no** falla si la escena era válida.
- **Sin RTTI/excepciones** (compilado `-fno-rtti -fno-exceptions`): nada de `dynamic_cast`/`try`.

## 4. Dos (tres) niveles de profundidad

| Nivel | Qué usa | Cuándo |
|---|---|---|
| **A — fachada** | `App`, `Screen`, `SpriteScene`, `ScrollLayer`, `IndexedDisplay`, `World` | un juego normal se escribe **aquí** |
| **B — dispositivo** | `Device`, `Scene`, `FramePlan`, `copper::Scheduler` | cuando A no llega (efectos, composición a mano) |
| **C — metal** | `eng::hw`, `eng::cpu`, backend, registros, DMA | lo último; el engine «desaparece» |

Regla: **sube al nivel más alto que resuelva la intención**; baja solo con motivo. Detalle en
[02_niveles](../02_niveles/README.md).

## 5. Un solo camino a la verdad (en ejecución)

- **Backend**: se instancia **solo en `main()`** (`eng::amiga::AmigaBackend backend {};`) y se pasa al
  `App`. La lógica no nombra tipos del backend.
- **Telemetría**: `g_eng_run_status` (magia `ENGR`) publica `InitStarted`/`Ready`/`Failed` + `detail`;
  el runner lo lee por el canal lateral (sin adivinar por la captura). Las demos lo usan con
  `eng::debug::mark_init_started/mark_ready/mark_frame/mark_failed`.
- **Una sola verdad por hecho**: el código cita la fuente (`docs/`, `fichero:línea`); la doc no se
  duplica.

## 6. El frame, en una línea

`init` (una vez) → por cada VBlank: `update` (lógica) → `render` (dibujo + `present`). El VBlank lo
marca el backend; el mini-SO (`eng/os`) publica el latido. Ver
[05_arquitectura/frame_path](../05_arquitectura/frame_path.md).

Volver al [índice de introducción](README.md) · [índice del manual](../README.md).
