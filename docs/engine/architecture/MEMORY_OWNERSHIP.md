# Propiedad y ciclo de vida de la memoria

Este documento fija la política coherente para reservar, usar y liberar memoria del engine. La aplicación describe recursos y su vida útil; el engine elige el banco compatible, conserva el ownership y entrega vistas no propietarias únicamente mientras el recurso siga vivo.

## Objetivos

- Un recurso que el Blitter o el Copper consumen vive siempre en Chip RAM.
- Una reserva de Chip no puede proceder accidentalmente del stack, Fast RAM o Slow RAM.
- El recurso propietario libera su bloque al destruirse o al desalojarse.
- Existe una única fachada de reserva; los consumidores no conocen `LinearArena`, `BlockPool`, `AllocMem` ni `FreeMem`.
- Las reservas temporales de frame y de setup tienen una semántica distinta y explícita.
- El cierre global del backend sigue siendo posible, pero no es la única forma de recuperar memoria.

## Política recomendada

La API pública debe tener una única puerta de recursos, por ejemplo `ResourceStore`/`Assets`, con
operaciones tipadas como `load<T>`, `create<T>`, `retain`, `release` y `reset_phase`. La elección
del asignador queda dentro del store y no se ofrece como dos APIs alternativas.

```text
Juego / escena / sistema
          │  load<Sprite>, create<Bitmap>, make<SaveUnder>
          ▼
ResourceStore  ─── ownership + banco + tamaño + alineación + vida
          │
          ├── recurso persistente ──► pool de bloques liberables
          └── scratch de fase/frame ─► arena reiniciable
```

La unificación debe hacerse en la interfaz y en las reglas de ownership, no forzando el mismo
algoritmo interno para todas las vidas útiles. Un pool es adecuado para assets y buffers que salen
individualmente; una arena es adecuada para scratch que se libera por fase o por frame. Exponer ambos
asignadores a los consumidores es la duplicación que debe eliminarse.

## Bancos y contratos

| Recurso | Banco obligatorio | Propietario recomendado | Uso |
|---|---|---|---|
| Framebuffer y bitplanes visibles | Chip | `Scene`/`Bitmap` | Bitplane DMA y Blitter |
| Hoja de BOB | Chip | `Asset<Sprite>`/`SpriteResource` | Canal A/B del Blitter |
| Máscara cookie-cut | Chip | `Asset<Sprite>` | Canal A del Blitter |
| Hoja intercalada `[máscara][imagen]` | Chip | `Asset<Sprite>` | Cookie-cut de un blit |
| Save-under | Chip | `BobInstance`/gestor de BOBs | Fuente y destino de copias |
| Copperlist | Chip | `copper::Plan`/`Scene` | DMA del Copper |
| Descriptores, actores y metadatos | Fast si existe; Slow como fallback | escena o sistema | Solo CPU |
| Datos comprimidos o staging de carga | Fast si existe; Slow como fallback | loader/cache | Solo CPU |
| Scratch de preparación de frame | Fast si existe; Slow como fallback | frame context | Se reinicia por frame |

`Fast` y `Slow` no son alternativas válidas para un recurso DMA. Slow RAM puede tener el mismo
aspecto de memoria del sistema, pero Agnus no la alcanza; Fast RAM es privada de la CPU. La decisión
de banco debe derivarse del dominio del recurso, no de una preferencia del llamador.

## Prohibición de memoria no certificada para DMA

Una API que acepta `Address<MemoryKind::Chip>`, `ChipView<Tag>` o `Block<Tag, Chip>` expresa una
precondición de hardware en el tipo. No debe existir en la frontera pública un constructor que
convierta cualquier `T*` en una dirección Chip, porque permitiría pasar una variable de stack o un
buffer de Fast/Slow mediante un cast.

```text
válido:   ResourceStore -> Block<Tag, Chip> -> ChipView<Tag> -> BlitJob
inválido: stack/ Fast/ Slow -> reinterpret_cast -> Address<Chip>
```

La conversión desde puntero crudo debe quedar limitada a adaptadores internos del backend o a tests
que declaren explícitamente que el bloque está en Chip. `BlitSource` y `BlitDest` deben recibir en
la API normal vistas o direcciones certificadas, no `u16*` arbitrarios.

## Ownership y destrucción

Un recurso persistente debe ser un handle propietario o un objeto movible que contenga el bloque y
una referencia al store que puede devolverlo al banco. Copiar el recurso debe copiar una referencia
contada o estar prohibido; nunca debe producir dos propietarios que liberen el mismo bloque.

```text
Asset<Sprite> nave
    ├── bloque de hoja Chip
    ├── bloque de máscara Chip
    ├── geometría y layout
    └── owner/resource store

destrucción o release final
    ├── quita el slot de la caché
    ├── devuelve hoja y máscara al pool Chip
    └── invalida las vistas no propietarias
```

Antes de liberar un recurso, el store debe garantizar que no existe una operación DMA pendiente que
lo use y que ningún `FramePlan`, Copperlist, actor o `AssetView` lo conserva como referencia activa.
El `FramePlan` debe contener referencias de ejecución válidas hasta `execute` y el propietario debe
vivir hasta que termine esa ejecución.

El destructor global del backend libera los bloques raíz entregados por Exec. Esa operación es el
último nivel de seguridad y no sustituye la liberación individual de recursos persistentes.

## Framebuffer y doble buffer

El framebuffer pertenece a `Scene`/`Bitmap`, no al allocator genérico de assets. Para `320 × 256 ×
4` planos y `row_bytes = 40`, un buffer ocupa `40 × 256 × 4 = 40.960` bytes, más la guarda y el
alineamiento configurados. Dos buffers ocupan dos bloques Chip independientes. El destructor de la
escena devuelve los bloques al store de escena cuando el asignador admite liberación; el cierre del
backend libera el bloque raíz aunque la escena no se haya destruido limpiamente.

El doble buffer y la publicación del Copper deben tener una regla de vida común: no se libera un
buffer hasta que deja de ser el buffer visible y el Copper no puede volver a leerlo.

## Setup, runtime y frame

La reserva debe clasificarse por fase:

1. **Setup**: se reservan framebuffer, Copperlists, assets estáticos, hojas de BOB y máscaras. Los
   fallos son errores de inicialización y no se ocultan con fallback a otro banco incompatible.
2. **Runtime persistente**: se cargan/desalojan assets por referencia, prioridad y presupuesto. El
   release final devuelve los bloques Chip o CPU al pool correspondiente.
3. **Frame scratch**: se reservan descriptores, listas compactas y staging temporal en el scratch
   elegido. `reset_frame` invalida todas sus vistas y no intenta liberarlas individualmente.
4. **Teardown**: se destruyen escenas, caches y stores en orden inverso al uso DMA; después se
   desactiva display/DMA y finalmente el backend libera los bloques raíz.

No se debe reservar un BOB durante el render ni mantener una vista de scratch dentro de un actor,
asset o Copperlist persistente.

## Unificación de las políticas actuales

La implementación debe escoger una única ruta pública. La opción recomendada es:

- `ResourceStore` como fachada única.
- `BlockPool` o un pool equivalente como implementación de reservas persistentes liberables.
- `LinearArena` como implementación privada de scratch y reservas de fase completa.
- `MemoryManager` como detalle interno del backend, no como API alternativa para juegos.
- `AssetCache` como cliente del store, no como segundo propietario de bloques.

La ruta directa `LinearArena::allocate_block` para assets persistentes y la ruta directa
`MemBank::reserve` desde consumidores deben retirarse progresivamente. No se deben mantener ambas
como formas soportadas de hacer la misma reserva.

## Consistencia y validaciones adicionales

- Cada recurso debe declarar su banco, alineación, tamaño útil, headroom de DMA y layout en una sola
  receta tipada.
- El tamaño reservado y el tamaño visible deben distinguirse; la guarda no puede quedar implícita.
- Cada vista no propietaria debe poder comprobar que su owner sigue vivo en builds de diagnóstico.
- `AssetCache`, `AssetRuntime` y los handles deben compartir una única semántica de referencia,
  pinning, desalojo y liberación.
- Los errores de reserva deben usar `Result`/`Expected` en las APIs nuevas, no combinar `bool`,
  bloque inválido y `nullptr` según el subsistema.
- Los presupuestos deben contabilizar Chip persistente, Chip scratch, Fast y Slow por separado, con
  picos y bytes retenidos.
- El loader debe cargar primero en staging CPU solo cuando el recurso no sea todavía DMA y copiarlo
  al bloque Chip propietario antes de publicarlo.
- La liberación debe tener una comprobación de DMA pendiente para evitar use-after-free del Blitter
  o del Copper.
- Los tests host deben rechazar tipos Fast/Slow en APIs Chip; las demos Amiga deben comprobar
  reserva, publicación, destrucción y reutilización del bloque.
- El backend debe exponer diagnósticos de banco, owner, tamaño, alineación, estado y causa de fallo,
  sin exponer punteros a la aplicación.

## Criterios de aceptación

1. Un BOB cargado por la API pública obtiene hoja y máscara en Chip sin que el consumidor elija el
   allocator.
2. Una API DMA no acepta un puntero de stack ni una vista Fast/Slow.
3. La destrucción o liberación final de un recurso devuelve su memoria cuando no hay DMA pendiente.
4. `reset_frame` solo invalida scratch y no libera recursos persistentes.
5. El cierre global sigue liberando todo el bloque raíz aunque queden handles mal gestionados, y
   produce un diagnóstico.
6. Solo existe una ruta pública de reserva de recursos y una semántica documentada de ownership.

## Referencias

- [`MEMORY_MODEL.md`](MEMORY_MODEL.md)
- [`RESOURCE_SYSTEM.md`](RESOURCE_SYSTEM.md)
- [`ROADMAP_API_COHERENCE.md`](ROADMAP_API_COHERENCE.md)
- [`INTERNAL_TYPE_SYSTEM.md`](INTERNAL_TYPE_SYSTEM.md)
- [`RASTER.md`](RASTER.md)
