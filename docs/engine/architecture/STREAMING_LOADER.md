# Diseño del Loader (carga de chunks desde almacenamiento)

Referencia de diseño para cerrar el **paso 5** del roadmap de streaming
(`docs/guides/roadmap/REFACTOR_PLAYFIELD_SCROLL.md`, Fase 7): cómo el `Loader` del
`ChunkCache` consigue los chunks cuando no caben todos en RAM. El diseño de streaming de
más arriba (prefetch, solo-residentes, `TileMapView`) **no cambia**; este documento solo
cubre la procedencia de los datos.

Contexto de hardware: `docs/reference/amiga/techniques/trackloading.md`. Formato del
mundo: `docs/engine/architecture/WORLD_FORMAT.md`.

## 1. Contrato que debe cumplir el Loader

`ChunkCache<ChunkSize,Capacity>` (`engine/include/eng/field/chunk_cache.hpp`) llama al
`Loader` solo cuando un chunk no está residente:

```text
bool load(void* user, s32 cx, s32 cy, u16* dst);   // dst = ChunkSize*ChunkSize u16
```

Reglas que el `Loader` debe respetar:

- Recibe la celda de destino **ya reservada** en el pool del llamador (Chip RAM); no asigna.
- Devuelve `true` cuando el chunk queda escrito y listo; el `ChunkCache` lo marca residente.
- No debe bloquear el frame más de lo imprescindible: el dibujo depende de él.

**Contrato de tres estados (implementado).** El `Loader` devuelve
`eng::field::LoadResult` (`engine/include/eng/field/chunk_cache.hpp`):

| Valor | Significado | Efecto en el `ChunkCache` |
|---|---|---|
| `Ready` | el `Loader` escribió los datos del chunk | queda residente |
| `Empty` | el chunk está ausente de verdad (celdas a `empty_tile`) | queda residente (no reintenta) |
| `Pending` | carga asíncrona aún no lista | **no** queda residente; se reintenta |

`ChunkCache::get` solo marca residente en `Ready`/`Empty` y expone los contadores
`empties()`/`pendings()` (además de `loads`/`evictions`/`hits`). Sin este contrato, un chunk
no listo se marcaría como residente-vacío y nunca se reintentaría.

## 2. Restricción clave: el takeover apaga el disco y las interrupciones

`MinimalBackend::takeover_display` escribe `INTENA=0x7FFF` (apaga la cadena de
interrupciones de AmigaDOS/exec) y `DMACON=0x7FFF` (apaga **todo** el DMA, incluido el de
disco). Ver `docs/engine/architecture/HARDWARE_AND_ROM_KERNEL_POLICY.md`.

Consecuencia directa: **después del takeover no se puede usar `trackdisk.device`** (su
completado es por interrupción, ya deshabilitada) ni `dos.library` en el path crítico. Por
tanto:

- la carga basada en el sistema operativo solo es válida **antes** del takeover (init/preload
  o fases OS-friendly/mixed);
- el streaming durante el gameplay, con el display tomado, exige **trackloader de hardware
  puro** (gestión propia de CIA-B + Paula + Disk DMA).

## 3. Opciones de implementación

| Opción | Vía | Cuándo es válida | Reintenta en el frame | Chip RAM |
|---|---|---|---|---|
| **A. Preload a RAM** | leer el mundo completo (DOS/trackdisk) antes del takeover o entre niveles | siempre; por defecto | — (todo residente) | alta (todo el mundo) |
| **B. `trackdisk.device` asíncrono** | `SendIO`/`CheckIO` sobre `IOStdReq` con el SO vivo | solo antes del takeover | n/a | buffers de pista + datos |
| **C. Trackloader de hardware** | CIA-B + Paula Disk DMA + decode MFM | cualquier momento tras el takeover | sí (cola + doble buffer) | buffers de pista + datos |
| **D. Worker de HD** | `dos.library`/PIO o DMA del controlador | SO vivo o HD-friendly | n/a | buffer de bloque |

**Recomendación por fases:**

1. **Ahora (A)**: el `Loader` copia chunks desde un **mundo ya en RAM** (incbinado o leído
   entero al inicio). Cierra el paso 5 sin trackloader y valida todo el camino de streaming
   (prefetch, eviction, `TileMapView`) con datos reales.
2. **Después (C)**: trackloader de hardware para disquete, manteniendo el mismo contrato de
   `Loader`. Es la única vía compatible con el takeover y con un A500 real.
3. **Desarrollo (B/D)**: útiles para herramientas, tests y arranque bajo AmigaDOS, no para el
   path de juego.

El `Loader` de A y C comparten interfaz: **solo cambia de dónde salen los bytes**. El resto
del streaming no se reabre.

## 4. Opción A — Loader sobre RAM (por defecto)

```text
  Blob de mundo (UAF-R o raw .bin)
        |  incbin / Read() al init
        v
  Fast RAM o Chip RAM: Span<const u16> cells + directorio de chunks
        |
        |  Loader(user, cx, cy, dst): busca (cx,cy) en el directorio y copia
        v
  pool de chunks (Chip RAM, del ChunkCache)
```

- Directorio **ordenado por `(cy,cx)`** para búsqueda binaria O(log n); el chunk vacío no se
  copia (se marca ausente).
- Si el mundo entero cabe en RAM, `prefetch` no es necesario: basta montar
  `SparseTileMap<16>` directamente (el `Loader` ni se usa).
- El `Loader` es una **copia de memoria** acotada (ChunkSize² u16), sin decodificación.

## 5. Opción C — Trackloader de hardware (diseño)

### 5.1 Control mecánico (CIA-B salidas, CIA-A entradas)

Bits de `hardware/cia.h`, activos a nivel bajo:

| Puerto | Bits | Función |
|---|---|---|
| CIA-B `PRA` | `CIAF_DSKMOTOR` (7), `CIAF_DSKSEL0..3` (3..6) | motor y selección de unidad |
| CIA-B `PRA` | `CIAF_DSKSIDE` (2) | cara (cabeza) |
| CIA-B `PRA` | `CIAF_DSKDIREC` (1) | dirección del *step* |
| CIA-B `PRA` | `CIAF_DSKSTEP` (0) | pulso de *step* |
| CIA-A `PRA` | `CIAF_DSKRDY` (5), `CIAF_DSKTRACK0` (4), `CIAF_DSKPROT` (3), `CIAF_DSKCHANGE` (2) | estado |

- El motor debe estar activo **mientras** se selecciona la unidad.
- Entre pulsos de `/STEP` hay que esperar ~3 ms (mecánica); el retardo se hace por temporizador
  CIA o bucle calibrado, **no** por contador fijo de instrucciones (falla en CPUs rápidas).

### 5.2 Registros de Paula

| Registro | Dirección | Uso |
|---|---|---|
| `DSKPTH`/`DSKPTL` | `$DFF020`/`$DFF022` | puntero a Chip RAM de destino |
| `DSKLEN` | `$DFF024` | bit 15 `DMAEN`, bit 14 `WRITE` (0 = leer), bits 13–0 = nº de words |
| `DSKSYNC` | `$DFF07E` | palabra de sincronía (estándar `$4489`) |
| `ADKCON` | `$DFF09E` | `ADKF_WORDSYNC` (bit 10) arranca el DMA al ver `DSKSYNC` |
| `DSKBYTR` | `$DFF01A` | byte/sync a demanda (lectura sin DMA) |

Secuencia de arranque (AHRM cap. 8, “Disk DMA Channel Control”):

1. Activar `DSKEN` en `DMACON`.
2. Escribir `DSKLEN = $4000` (fuerza el apagado preventivo).
3. Escribir el valor deseado en `DSKLEN`.
4. **Escribir el mismo valor otra vez**: el doble escrito es el que realmente arranca el DMA.
5. Al terminar, volver a `DSKLEN = $4000`.

`ADKF_WORDSYNC` hace que Paula no empiece a volcar hasta encontrar `DSKSYNC`, lo que garantiza
la **alineación de palabra** de los datos (recomendado por el AHRM frente a sincronizar a mano).

### 5.3 Doble buffer de pista

Dos buffers de pista en **Chip RAM** (obligatoria: el Disk DMA solo escribe Chip), de
`track_words` words cada uno (≈ `$1900` words por pista DD, 11 sectores ≈ 5,5 KB útiles):

```text
        pista N                          pista N+1
   +------------------+            +------------------+
   |  buffer A (Chip) |            |  buffer B (Chip) |
   +------------------+            +------------------+
        ^                                   ^
        | Disk DMA (pista N)                | Disk DMA (pista N+1)
        |                                   |
     CPU DECODIFICA A                    CPU DECODIFICA B
        (mientras B se llena)              (mientras A se llena)
```

- Mientras el Disk DMA llena un buffer, la CPU **decodifica el otro** y publica los chunks
  que contiene en la cola de datos listos.
- La CPU cede el bus de Chip RAM al Disk DMA (prioridad alta) pero sigue trabajando en Fast
  RAM; el sistema no se bloquea como con el Blitter en modo *nasty*.
- El coste dominante es **mecánico**: ~200 ms por revolución de pista más el *seek*. El diseño
  debe pedir con **margen de varias pistas** por delante de la ventana visible.

### 5.4 Decodificación MFM

Paula entrega **MFM en crudo**; la CPU (o un rutina rápida) decodifica:

```text
  raw MFM -> buscar sync $4489 -> separar pares par/impar -> juntar bits de datos
          -> comprobar checksum (XOR) -> estructurar en sectores -> copiar a chunks
```

- Alternativa byte a byte: usar `ADKF_MSBSYNC` + `DSKBYTR` para leer bits/bytes sincronizados
  sin DMA; útil para leer solo cabeceras de sector y decidir si interesa la pista.
- El checksum define qué sectores son válidos; un sector inválido reintenta (la pista sigue
  girando).
- Si el mundo está comprimido, la decodificación puede terminar en un depacker (el repo ya
  incluye `support/depacker_doynax.s`; ver `WORLD_FORMAT.md` §7).

### 5.5 Integración con `ChunkCache`

```text
  prefetch(banda)  ->  encola las pistas que contienen los chunks de la banda
                                |
                       cola de pistas (estado: pedida / cargando / decodificando / lista)
                                |
  Loader(cx,cy,dst)  ->  si el chunk esta listo: copia (Ready)
                         si el sector/pista fallo:     Empty
                         si aun no llego:              Pending (se reintenta)
```

- El **nombre de chunks a pista** lo da el directorio del mundo (`WORLD_FORMAT.md`): cada
  chunk conoce su pista/sector de origen.
- El `prefetch` debe ir **más de una pista por delante** de la ventana; a 2 px/frame, una
  pista (~5,5 KB) cubre muchos frames de avance.
- La telemetría (`loads`/`evictions`/estado de la cola) se expone en
  `g_eng_run_status.detail` para el profiling.

## 6. Presupuesto y límites

| Recurso | Coste | Nota |
|---|---|---|
| Chip RAM | 2 buffers de pista (~12,8 KB) + pool de chunks | el Disk DMA exige Chip |
| Fast RAM | código de decode, directorio, cola | CPU libre mientras el DMA llena Chip |
| Tiempo | ~200 ms/pista + *seek* (~3 ms/paso) | pedir con pistas de margen |
| Contención | Disk DMA compite con bitplane/audio/blitter | margen menor con muchos planos |
| CPU | decode MFM + checksum | solapa con el dibujo, no con el mismo bus Chip |

## 7. Fases de implementación

1. **Loader-RAM** (paso A): directorio + copia sin decode. Cubre el paso 5 sin hardware.
2. **Contrato de tres estados** en `ChunkCache`/`StreamingWorldMap` (**hecho**: `LoadResult`).
3. **Trackloader**: motor/seek + lectura de una pista + decode MFM + checksum (una pista
   aislada, verificada contra `DSKBYTR`).
4. **Doble buffer + cola** y `prefetch` con margen de pistas.
5. **Integración con `BackgroundQueue`** y telemetría; medir con `out/tmp/fps.mjs`.
6. **HD/worker** (opción D) para el entorno de desarrollo (el runner monta el `.exe` en
   `dh1:`), si se quiere streaming sin disquete.

## 8. Criterios de aceptación

- `ChunkCache`/`StreamingWorldMap` con `Loader`-RAM: mismo resultado visual que un mundo
  denso equivalente (test de equivalencia denso↔streaming, paso 4 del roadmap).
- Trackloader: leer una pista y recuperar los 11 sectores con checksum válido, verificado
  contra el contenido del ADF.
- Streaming bajo presupuesto: `loads`/`evictions` acotados, sin huecos (chunks no residentes)
  en la banda visible, y sin regresión de fps.
- Sin heap en gameplay y con el pool de chunks en Chip RAM contabilizado en el modelo de
  recursos (`SCENE_AND_RESOURCES.md`).
