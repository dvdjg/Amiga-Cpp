# Multiplexor de sprites con fallback a BOB (selección dinámica de recurso)

Técnica clásica (Amiga y C64) para escenas con **muchos objetos móviles**: usar los **8 canales
de sprite hardware** hasta donde se pueda y dibujar **el resto como BOB (Blitter)**. La clave
es que **todo el trabajo pesado se hace una vez por frame** (o en el setup), y el bucle
principal solo **consulta una decisión ya tomada** (`assigned`). Encaja con el sistema de
objetos del engine ([OBJECT_SYSTEM.md](../../engine/architecture/OBJECT_SYSTEM.md)).

## 1. Idea

```text
   setup / carga de nivel                     por frame (una vez)                bucle de dibujo
   ─────────────────────                       ──────────────────                 ──────────────
   clasificar tipos de objeto          ┌─► ordenar por Y (casi ordenado:      ┌─► if (obj.assigned)
   (fijo / grupo con trayectoria /     │     insertion/gnome incremental)      │      drawSprite(ch)
    libre) + tablas de grupos          │   pase lineal: primer canal libre en  │   else drawBob(obj)
                                       └─► [Y, Y+alto] (gap 1 línea) ────────┘   (una lectura de byte)
                                           sin hueco → BOB (assigned=0)
```

## 2. Clasificación de objetos (setup)

- **Fijos de alta prioridad** (jugador, HUD, score) → siempre sprite; se asignan primero.
- **Grupos con trayectoria conocida** (ristras de tiros, formaciones de naves) → se tratan
  como un **meta-objeto** y se intenta meterlos **juntos en los mismos canales**.
- **Objetos libres** (enemigos, power-ups, partículas) → asignación individual.

Del setup pueden salir **tablas de "slots preferidos"** por tipo de grupo (precalcular el canal
que suele quedar libre para una formación), de modo que el reparto por frame sea casi directo.

## 3. Fase por frame: reparto barato

```text
1. Ordenar los objetos activos por Y. Si la lista venía casi ordenada del frame anterior,
   insertion/gnome sort es casi O(n).  (Bucket sort por bandas de 8-16 líneas si no.)
2. Recorrer la lista ordenada y, por objeto, buscar el PRIMER canal libre en [Y, Y+alto]
   con el gap mínimo de 1 línea. Si no hay → BOB.
   La ocupación se lleva con lastY[8] (última línea usada por canal) → comprobación O(1).
3. Prioridad de asignación: primero fijos y grupos con trayectoria; después los libres.
```

```c
struct Object {
    s16 y, x;
    u16 height;       // líneas de raster
    u8  type;         // 0 = libre, 1 = ristra de tiros, 2 = formacion...
    u8  priority;     // 0 = mas alta
    u8  assigned;     // 0 = BOB, 1..8 = canal de sprite (par bajo si attached)
};

s16 lastY[8];         // ultima linea ocupada por canal (-1 = libre)
```

`lastY[canal]` basta porque los objetos ya vienen **ordenados por Y**: para un objeto en `y`,
el canal está libre si `lastY[canal] < y`. Al asignarlo, `lastY[canal] = y + alto + gap - 1`.

Para solapes complejos con objetos cortos, una variante más exacta es un **bitfield de
ocupación por línea** (`u16 occupancy[8][16]` = 8 canales × 256 bits; en el engine, arrays de
capacidad fija, sin `std::bitset`).

## 4. Grupos con trayectoria conocida

Un grupo se asigna a **canales fijos** (o al primer par libre) para todo su **bounding box
vertical** `[minY, maxY]`:

```text
   ristra de tiros (misma trayectoria) ─── todos en el MISMO canal N
   formacion de naves ──────────────────── corrida de canales contiguos
   si el bounding box no cabe en un canal/par → opciones:
      (a) asignar individualmente (rompe la "ristra"), o
      (b) forzar todo el grupo a BOB (dibujo coherente)
```

Cada grupo lleva `preferredChannel` (canal preferido, precalculado en el setup) y
`forceTogether`. Los grupos se procesan **antes** que los libres.

## 5. Construcción de la lista DMA + Copperlist

Una vez asignado cada objeto:

- **Sprite**: se escribe su entrada en la estructura DMA del canal (cabecera `POS`+`CTL` +
  DATA por línea + terminador). Varios sprites del mismo canal (multiplexado vertical) van
  **encadenados** en la misma estructura (reuso de canal, AHRM cap. 4): el DMA pasa al
  siguiente al llegar al final, con **≥1 línea** entre uno y otro.
- **BOB**: se encola un `BlitJob` (cookie-cut `$CA` u OR `$FC` intercalado) en el `FramePlan`.
- **Copperlist**: `SPRxPTH/PTL` de los 8 canales (una vez) + rearmes/reposiciones horizontales
  si se usa mux horizontal (ver [sprite-horizontal-multiplex.md](sprite-horizontal-multiplex.md)).

El coste del bucle de dibujo es **trivial**: por objeto, mirar `renderType`/`assigned` y llamar
a `drawSprite(ch)` o `drawBob()`.

## 6. Optimizaciones

| Situación | Técnica |
|---|---|
| Ristras de tiros / formaciones | meta-objeto con canales fijos (preferido, precalculado) |
| Muchos objetos del mismo tipo | **bucket sort por Y** (8-16 bandas) en vez de sort completo |
| Lista casi ordenada | **insertion/gnome sort incremental** (barato) |
| Sobrecarga puntual | **flicker controlado**: si no cabe este frame va a BOB, el siguiente se prioriza |
| Grupos predecibles | **tabla de slots preferidos** por tipo (setup) |

## 7. Variantes

1. **Bandas fijas**: dividir la pantalla en 4-8 bandas y dar 8 canales (reutilizables) a cada
   una. Muy predecible y barato; menos flexible.
2. **Priority-based mapping**: asignar canales altos o bajos según la prioridad del objeto para
   mantener el **orden de dibujo** estable.
3. **Híbrido con Copper**: los asignados a sprite van a la lista DMA + copperlist; los BOB se
   encolan al Blitter; el Copper también puede parchear posiciones/colores de los sprites.

## 8. Coste

- **Setup**: clasificación + tablas de grupos (una vez).
- **Por frame**: sort (casi O(n)) + pase lineal O(n) con `lastY[8]` → muy barato incluso con
  100-150 objetos. El bucle de dibujo es una lectura de byte por objeto. El coste **hardware**
  de los sprites y BOBs es el de [sprite-techniques-catalog.md](sprite-techniques-catalog.md)
  (sprites 16 slots/línea; cada BOB 32×32 ≈ 2 304 ciclos DMA).

## 9. Encaje en el engine (para implementar)

Ya existe buena parte:

- **`graphics/sprite_allocator.hpp`** (`SpriteAllocator`): first-fit **greedy con multiplexado
  vertical** (`busy_until[8]`), tiras horizontales (`strip_id`/`strip_index`/`strip_span`),
  pares **attached** y **fallback `SpriteSlot::as_bob`** — es exactamente este multiplexor
  para el caso general. Tests **HOST-003/416**.
- **`scene/compose_sprites`** + `emit_bob_fallbacks`: una intención por actor, reparto, y los
  degradados se emiten como BOB en el `FramePlan`. Fachada `eng::SpriteScene` (HOST-391).
- **Reparto por franja** (`graphics/sprite_band.hpp`, `SpriteChannelLedger`): permite reservar
  canales a fondos por banda y dejar el resto a objetos (HOST-416).
- **`scene::RepresentationAllocator`**: elige sprite/BOB/CPU al dar de alta el actor.

**Pendiente (lo que aporta esta técnica):**

1. **Grupos con trayectoria** en el asignador: un `SpriteIntent` "de grupo" que reclame una
   corrida de canales para todo el bounding box (análogo a las tiras, pero por trayectoria) y
   `preferred_channel`. Encaja como extensión de `SpriteAllocator`/`SpriteIntent`.
2. **Orden por Y incremental**: hoy `build_sprite_intents` ordena por `top` (el llamador
   garantiza el orden); añadir un **reuso del orden del frame anterior** (insertion sort sobre
   una lista casi ordenada) para no reordenar todo.
3. **Ocupación precisa por línea** (bitfield) como política opcional del allocator para escenas
   densas de objetos cortos (hoy `busy_until[8]` es suficiente para el caso ordenado por Y).
4. **Prioridad de asignación** (fijos/grupos antes que libres) expuesta al allocator.
5. **DMA encadenado por canal**: construir la estructura `[…][sprite][sprite]…` con el gap de 1
   línea (lo hace el driver por segmentos; unificar).

Un test host del caso de grupos (ristra asignada junta, o degradada entera) cerraría la pieza.

## 10. Referencias

- [sprite-techniques-catalog.md](sprite-techniques-catalog.md) (inventario y costes),
  [sprite-layer.md](sprite-layer.md) (multiplexado vertical, gap, attached),
  [sprite-horizontal-multiplex.md](sprite-horizontal-multiplex.md) (mux horizontal).
- AHRM 3.ª cap. 4 (reuso de canales DMA de sprites).
- Engine: `graphics/sprite_allocator.hpp`, `scene/actor_sprite.hpp`,
  [OBJECT_SYSTEM.md](../../engine/architecture/OBJECT_SYSTEM.md),
  [SPRITE_BANDS.md](../../engine/architecture/SPRITE_BANDS.md); HOST-003/391/416.
