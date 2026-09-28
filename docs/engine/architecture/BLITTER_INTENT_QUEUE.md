# API de Blitter por intención (cola asíncrona)

API **adicional** (no sustituye al `FramePlan`/`OrBlobBatch` actuales) inspirada en OpenGL: el
desarrollador **declara intenciones** (`stamp`, `fill`, `copy`…) y **no espera** a que se dibujen.
Las peticiones se **encolan** y se ejecutan **cuando el Blitter está libre**, de una en una o como
un **array de golpe**, avanzando por la **IRQ de blit** (o por poll en los huecos). Ver
`docs/reference/amiga/techniques/blitter-cpu-interleaving.md` y `BACKGROUND_TASKS.md`.

## 1. Por qué

Hoy cada blit se programa y **se espera** (`wait_blitter` sondea `BBUSY`): en la 117 eso es **~30 %
del tiempo de CPU**, y el comentario de `DrawObject` del original ya intenta solaparlo a mano (calcular
el vértice N+1 mientras corre el blit N). El patrón "cola + IRQ" lo generaliza: **la espera
desaparece** y el desarrollador solo marca **puntos de dependencia** explícitos.

## 2. Piezas

```
   intencion (sin hardware)        cola (ring fijo, sin heap)      ejecucion
   ┌───────────────────────┐      ┌────────────────────────┐      ┌────────────────────┐
   │ q.stamp(sheet,f,x,y)  │─────►│ [ BlitOp ][ BlitOp ]...│─────►│ feeder: IRQ blit o │
   │ q.fill(screen,rect,c) │      │  FIFO, capacidad N     │      │ poll en los huecos │
   │ q.all(span<BlitOp>)   │      └────────────────────────┘      └─────────┬──────────┘
   └───────────────────────┘                                               │ programa BLTxPT/BLTSIZE
                                                              q.flush() empezar / q.wait() vaciar
```

1. **`BlitOp`** — una petición (variante ligera, sin punteros a función):
   `Kind { Stamp, Fill, Copy, Line }` + params del dominio (`BitmapView src/dst`, `Rect`, `rgb444`,
   `sheet frame`). **No nombra registros.**
2. **`BlitQueue<N>`** — ring de `BlitOp` (capacidad fija). Métodos de **intención** (`stamp`,
   `fill`, `copy`, `line`) + `all(eng::Span<const BlitOp>)` para **un array de golpe**. Sin heap.
3. **El feeder** — quién programa el Blitter cuando queda libre:
   - **(P) poll**: se llama `pump()` en los puntos donde el CPU ya espera (la espera de VBlank y el
     hook de espera de Blitter, `g_blitter_service`). **Coste cero de IRQ.**
   - **(I) IRQ**: se habilita la **IRQ de blit** (`INTENA`/`INTREQ` bit 6, nivel 3, ya cableada en
     `level3_dispatch`); al terminar un job, el handler programa el siguiente. **Coste ~250 c/job**,
     pero el CPU queda **libre** entre blits para otra cosa.
   - **Política configurable** (`Feed::Poll | Feed::Irq`).
4. **Sincronización explícita** (lo único que bloquea):
   - `flush()` — garantiza que se está drenando (no vacía).
   - `wait()` — vacía la cola: el **punto de dependencia** (leer el destino, reusar el buffer,
     cambiar de pantalla). Es el `glFinish`.

## 3. Ejecución (restricciones reales)

- El Blitter tiene **un solo juego de registros** → la cola se **serializa** (de a uno). El feeder no
  paraleliza blits; solo elimina la espera **activa**.
- El bus es compartido con la CPU y el DMA de display. Con `BLTPRI` (blitter-nasty) el Blitter no
  cede slots: el feeder por IRQ **no** garantiza solape con la CPU (medido: el pase fusionado no
  ayudó). Con `BLTPRI` off el Blitter es más lento pero la CPU avanza. **Por eso el poll es el
  defecto** (no añade IRQs y usa huecos que ya existían) y la IRQ es opt-in para "fire-and-forget".

## 4. Uso (sabor)

```cpp
eng::gfx::BlitQueue<64> q;                       // se ata al backend una vez

void update(...) {
    q.fill(screen, {0, 0, 320, 256}, bg);        // intencion: limpiar
    for (const Bob& b : bobs)                    // intencion: estampar
        q.stamp(sheet, b.frame, b.x, b.y);
    q.all(more_ops);                             // array de golpe
    q.flush();                                   // empieza a drenar (NO espera)
    // ... CPU: proyectar el siguiente frame mientras el Blitter trabaja ...
    q.wait();                                    // unico punto de espera: commit de este frame
    install_copper(active);                      // el buffer destino ya esta completo
}
```

## 5. Relación con lo que YA existe

- `submit_blit_job`/`blitter_submit` programan registros → el feeder los usa (no se reescribe el
  backend).
- `g_blit_task` + `level3_dispatch` bit 6 → **el punto de entrada de la IRQ ya está**; falta el
  handler que avanza la cola.
- `g_blitter_service` (`wait_blitter` drena el fondo) → el **poll** se cuelga de ahí.
- `OrBlobBatch` (batch sincrono actual) → un caso particular (`stamp` × N + `wait` al final).
- `FramePlan` → puede **producir** `BlitOp`s en vez de ejecutarlos él mismo.

## 6. Decisiones y límites

- **Orden FIFO** (el Blitter es secuencial); prioridad opcional como extensión.
- **Capacidad fija** `N` (sin heap); si se llena, `flush`+`wait` antes de encolar (política).
- **Los puntos de dependencia son explícitos**: como en GL, la asincronía **exige** declararlos; el
  `wait()` es el único bloqueo.
- **Coherencia con `wait_blitter`**: la cola debe respetar los blits ya en vuelo (un `flush` tras un
  blit suelto).
