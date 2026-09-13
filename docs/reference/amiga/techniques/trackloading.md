# Carga en segundo plano desde disquete (trackloading) y equivalente desde HD

El Amiga puede **seguir ejecutando el programa mientras lee datos de disquete**. La técnica se
conoce como *trackloading*, *background loading* o *streaming from floppy*, y es la base de los
cargadores de niveles de muchos juegos y demos: música, lógica y efectos continúan mientras el
controlador de disco rellena memoria.

## 1. Dos niveles de implementación

### 1.1 `trackdisk.device` (con el sistema operativo vivo)

- Se conserva el sistema operativo en marcha y se usa el device estándar de disco.
- La I/O se lanza con `SendIO` (asíncrona) o `DoIO` (síncrona) sobre un `IOStdReq`, y se consulta
  con `CheckIO` / `WaitIO`.
- `SendIO` + `CheckIO` permiten **lanzar una lectura y continuar ejecutando** mientras el DMA del
  disco rellena el buffer; el `WaitIO` solo se hace cuando el dato es necesario.
- Caso clásico: *Faery Tale Adventure*, cuyo terreno se guardaba como *tracks* en crudo y se
  cargaba en segundo plano con el driver de bajo nivel.

### 1.2 Trackloader de hardware puro (lo habitual en demos y juegos «system-killing»)

Se toma el control total del hardware y se abandona el sistema operativo:

1. Se programa el **CIA-B** para motor, selección de unidad, *step* de cabezal y lado.
2. Se configuran los registros de disco de **Paula** (`DSKPTH`/`DSKPTL`, `DSKLEN`, `ADKCON`,
   `DSKSYNC`…).
3. Se activa el **Disk DMA** (bit correspondiente de `DMACON`).
4. Paula lee la pista completa (MFM) a Chip RAM de forma autónoma.
5. La CPU (o el Blitter) decodifica el MFM a datos útiles.
6. Mientras tanto la CPU puede seguir ejecutando juego/demo, siempre que no necesite el bus de
   Chip RAM en los mismos ciclos que el Disk DMA.

El Disk DMA tiene prioridad alta, pero **no bloquea la CPU de forma total** como el Blitter en modo
*nasty*: se puede solapar bastante trabajo.

## 2. Flujo típico de un trackloader de demo

```text
                 arranque
                    |
        Bootblock carga el "resident loader" en RAM
                    |
        El loader mantiene una cola de peticiones
        ("necesito las pistas X-Y en la direccion Z")
                    |
   +----------------+-----------------+
   |                                  |
 parte actual del juego/demo          peticiones atendidas en los huecos
 (musica, logica, efectos)            (softint / VBlank): pide y decodifica
                                        las pistas siguientes
   |                                  |
   +----------------+-----------------+
                    |
        descompresion on-the-fly si hace falta
                    |
        datos listos en Chip RAM -> consumidor (nivel, graficos, audio)
```

## 3. Limitaciones

| Limitación | Detalle |
|---|---|
| **Velocidad** | ~1 vuelta de disco ≈ 200 ms por pista (11 sectores ≈ 5,5 KB útiles). Buscar y leer varias pistas es lento. |
| **Chip RAM** | El buffer MFM y los datos deben estar en Chip RAM: el Disk DMA solo ve Chip. |
| **Contención de DMA** | Disk DMA + Bitplane DMA + Blitter + Audio compiten por el bus; con muchos planos el margen se reduce. |
| **CPU durante la lectura** | La CPU puede trabajar, pero si su código está en Chip RAM o accede mucho a Chip, se ralentiza. |
| **Precisión de timing** | Los *steps* del cabezal y el motor tienen tiempos mínimos (AHRM). Los bucles de espera por contador fallan en CPUs rápidas. |
| **Sistema operativo** | Un trackloader de hardware puro mata el SO: después no están disponibles `dos.library` ni `trackdisk.device`. |
| **Multitarea** | En la práctica incompatible con multitarea real del sistema operativo. |

## 4. Equivalente desde disco duro

Desde HD **no existe un equivalente directo y «gratis»** al Disk DMA del floppy:

- Los controladores IDE/SCSI clásicos (Gayle, WD33C93, etc.) son mayoritariamente **PIO** (la CPU
  mueve los datos) o tienen un DMA propio peor integrado con el chipset.
- En la práctica se opta por:
  - lecturas asíncronas con `dos.library` / `SendIO` manteniendo el SO vivo (más sencillo que en
    floppy);
  - o un sistema de **doble buffer + worker** que lee bloques mientras el juego corre (patrón de
    WHDLoad y de los juegos instalables en HD).
- En aceleradoras modernas (FastATA, SCSI DMA, etc.) el solapamiento mejora mucho porque el DMA
  del controlador libera a la CPU.

La ventaja del HD es la velocidad y la ausencia de *seeks* mecánicos largos; la desventaja, no
tener un DMA de disco tan simple y predecible como el de Paula.

## 5. Relación con el engine (streaming de chunks)

El consumidor natural de esta técnica es el **streaming de chunks** del motor de tiles
(`docs/engine/architecture/CONTENT_AND_TILEMAP.md` §2):

- `ChunkCache` / `StreamingWorldMap` ya separan *qué chunk hace falta* (`prefetch`) de *dónde se
  lee* (el `Loader`). Ese `Loader` puede leer del disco en segundo plano.
- El `prefetch` por frame marca la banda entrante; el trabajo de disco se solapa con el dibujo y
  se sincroniza antes de que el chunk sea visible.
- Encaja con el presupuesto de Chip RAM del modelo de ocupación y con una `BackgroundQueue`.
- Diseño de implementación del `Loader` (preload a RAM, `trackdisk.device` vs. trackloader,
  doble buffer de pista, decodificación MFM y contrato de tres estados): 
  `docs/engine/architecture/STREAMING_LOADER.md`; formato del mundo:
  `docs/engine/architecture/WORLD_FORMAT.md`.

## 6. Referencias

Amiga Hardware Reference Manual, 3.ª edición (local: `docs/reference/ahrm/`):

- **Capítulo 8 – Interface Hardware** (líneas ~6784–9140): floppy disk controller, registros
  (`DSKPTH`/`DSKPTL`, `DSKLEN`, `ADKCON`, `DSKSYNC`…), *disk interrupts* y *disk timing*.
- **Apéndice F – 8520 CIA** (~10709–11186): bits de motor, *step*, lado y *ready*.
- **Apéndice H – External Disk Connector** (~11408–11574): interfaz física del disco externo.
- Índice y términos de búsqueda: `docs/reference/ahrm/amiga-hardware-manual-index.md`.

Enlaces externos:

- The Tech Behind Eon: Track Loader (The Black Lotus): https://tbl.nu/2019/09/09/TrackLoader/
- Tutorial clásico de trackloader hardware (a partir de Amiga News Tech): http://cyberpingui.free.fr/tuto_trackloader.htm
- Ejemplo de bootblock + trackdisk: https://github.com/alpine9000/amiga_examples/tree/master/000.trackdisk
- Trackloader de deplinenoise (bootblock + interfaz de carga): https://github.com/deplinenoise/trackloader
- Hilos de EAB sobre «trackloader», «background load» y «Rob Northen loader».
