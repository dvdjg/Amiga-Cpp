# Referencia — `res/`

El módulo de **recursos** (`eng::res`): administrar los assets y el código en memoria bajo presupuesto, cargarlos de disco de forma **asíncrona** y decodificarlos. La **caché** decide qué vive en cada banco (Chip/Fast) y desaloja por LRU; el **loader** carga overlays de código relocatable; el contenedor `.engz` comprime y verifica. Ver `docs/engine/architecture/RESOURCE_SYSTEM.md`.

```
   AssetTable (blobs en memoria) ─┐
   AssetRuntime (ficheros) ───────┼─► AssetCache (slots, presupuesto, LRU) ─► Block<Tag>
   AsyncRead/AsyncOverlay ────────┘        │                                   │
                                    on_load_done(IoUser, gen)           DynLoader (overlay)
   .engz ─► decode (Raw/Zx0) ─► HUNK/englib ─► .englib 'ENGL' · HUNK nativo AmigaOS
```

## Páginas

| Página | Qué documenta |
|---|---|
| [`cache.md`](cache.md) | `AssetCache`/`AssetHandle`/`AssetView`/`AssetSlot`/`AssetLease`/`CacheConfig`, `Budget`, `load<Tag>`/`DomainAsset`, `AssetTable`, `AssetRuntime`, `route_io`. |
| [`code.md`](code.md) | `DynLoader`/`ImportTable`/`.englib`, `HunkImage`, `symbol_hash`, `engz` (contenedor), `decode`/`zx0`. |
| [`async.md`](async.md) | `AsyncRead` (R6.7) y `AsyncOverlay` (E/S + decode + carga, sin bloquear el frame). |

## Reglas

- **El medio y la alineación los fija el dominio**, no el juego: `DomainAsset<Tag>` (una sola verdad) decide que un plano/BOB/copper va a Chip con su alineación, `load<Tag>` lo aplica.
- **Presupuesto antes de pedir**: `Budget::can_fit` permite **rechazar** un recurso en vez de descubrir el fallo por una reserva que no cabe.
- **Una sola cola de E/S** sirve a todos los recursos: `route_io` discrimina por `IoUser::tag` ('A' caché / 'L' loader / 'S' stream) y la generación rechaza respuestas tardías (R6.2).
- El módulo es **puro** respecto a la E/S y la memoria: recibe un `Backend` (Amiga usa `MemorySystem` + `os::file_read_async`; host, arena falsa).

Volver a [Referencia](../README.md) · [índice del manual](../../README.md).
