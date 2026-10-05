# Modelo de memoria

El engine **nunca llama a `malloc`/`AllocMem` en el camino de juego**. Al arrancar, el backend reserva
**un bloque por banco**; el motor reparte ese espacio con **arenas** y entrega **bloques tipados**. El
modelo completo está en `docs/engine/architecture/MEMORY_MODEL.md` y `MEMORY_OWNERSHIP.md`.

## Tres bancos (lo que ve cada uno)

```
  CPU (68000)                        Agnus/Denise (DMA)
  ┌───────────────────────────────┐  ┌───────────────────────────────┐
  │ FAST RAM  (CPU-privada)       │  │ CHIP RAM  (la ve el DMA)      │
  │  · estado del juego/simulación│  │  · bitplanes, sprites         │
  │  · tablas, stacks, buffers CPU│  │  · copperlists, sonido Paula  │
  ├───────────────────────────────┤  │  · cualquier buffer del Blitter│
  │ SLOW RAM  (no-Chip, auxiliar) │  └───────────────────────────────┘
  │  · CPU pero comparte bus;     │
  │    peor que Fast → auxiliar   │   Regla: lo que lee/escribe el Blitter
  └───────────────────────────────┘   DEBE estar en CHIP.
```

- **Chip**: única visible al DMA. Sin Chip no hay display ni Blitter ni audio.
- **Fast**: CPU a plena velocidad, Agnus **no la ve** → para trabajo intensivo de CPU (estado del 6502,
  física, descompresión). Se detecta en runtime (`eng::hw::HwInfo`, `hw::has_fast_ram`).
- **Slow**: no-Chip pero **comparte el bus** con el DMA → peor que Fast para CPU. Solo auxiliar.

## Reserva y reparto

```cpp
eng::amiga::AmigaBackend backend {};
// (a) automático: sondea el hardware y elige perfil
backend.configure_game_memory();
// (b) manual: declara los bancos en BYTES (chip, slow, frame, fast)
backend.configure_memory({512u*1024u, 0u, 0u, 1024u*1024u});
```

`MemoryManager` (`engine/include/eng/memory/memory_manager.hpp`) da un banco por tipo de almacenamiento:

```cpp
auto& mm = app.memory_manager();
auto planes = mm.chip().reserve<eng::PlaneTag>(bytes, 16u);   // DMA  → Chip (tipado)
auto estado = mm.fast().reserve<eng::SimTag>(tam, 4u);        // CPU  → Fast
```

El `Tag` fija el **dominio** y el banco correcto; `align` fija la alineación (16 para planos/Copper).
La reserva devuelve `Block<Tag>` (vacío si no cabe: consulta antes con `app.resources()` →
`res::Budget::remaining_chip()`/`can_fit()`). La política de banco la encapsula `res::load<Tag>` /
`reserve<Tag>(mm, MemoryPolicy, …)`.

## `Block<Tag>` y las vistas con tag

```
  Block<PlaneTag>            Bytes<PlaneTag> / Words<PlaneTag>
  ┌──────────────┐           ┌──────────────────────────────────────┐
  │ ptr base  ───┼──► mem    │ vista tipada (no un u8* suelto)        │
  │ tamaño(byte) │           │  · .data() / .size() / .subspan(...)   │
  │ kind: Chip ──┘           │  · .raw() en la frontera (documentada) │
  └──────────────┘           └──────────────────────────────────────┘
```

Una reserva **no** es un `u8*`: es un `Block<Tag>` (propietario) con su `kind` (banco efectivo). Las
vistas (`Bytes`/`Words`) llevan el `Tag` de dominio, de modo que `reserve<PlaneTag>` no se puede pasar
donde va un `BobTag`. Los punteros crudos solo existen en la **frontera** (`raw()`), documentada
(`docs/engine/architecture/INTERNAL_TYPE_SYSTEM.md`).

## Scratch por frame vs. retenido

- **Retenido** (vive toda la escena): bitplanes, copperlists, assets en caché. Se reserva en `init`.
- **Scratch de frame**: se **reinicia** cada frame (`reset_frame_scratch`) — sin acumular. Es donde van
  los planes de blits (`FramePlan`) y las estructuras temporales.

## Propiedad

- El **`App`/la escena** posee los recursos de display; el **juego** posee sus motores (capas, actores).
- Los **observadores** (una capa que mira la cámara del juego, un `Ref<Map>`) **no** son propietarios.
- La liberación es **explícita** (`release()`), no por `delete`. Ver
  `docs/engine/architecture/MEMORY_OWNERSHIP.md`.

Volver a [Arquitectura](README.md) · [índice del manual](../README.md).
