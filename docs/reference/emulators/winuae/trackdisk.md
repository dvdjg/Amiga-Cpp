# WinUAE — disco a nivel de device (`DSKPT`/`DSKLEN`/`DSKBYTR`)

Cómo implementa WinUAE el **controlador de disco** (DMA de floppy), leído de su fuente
(`../WinUAE-DBG/`): `custom.cpp` (registros) y `disk.cpp` (DMA, MFM y ciclo del device). Es la
referencia de facto para el backend `trackdisk` del engine (ver `AGENTS.md` §1.11).

> Alcance: esto es el **hardware/device** (registros y DMA). El que `df0:` exista como **volumen
> DOS** no es cosa del emulador: lo decide Kickstart/`dos.library` (ver §6).

## 1. Registros y handlers

| Registro | Dirección | Handler | Fuente |
|---|---|---|---|
| `DSKPTH`/`DSKPTL` | `$DFF020`/`$DFF022` | `DSKPTH`/`DSKPTL` | `custom.cpp:7300-7301`, `disk.cpp:5199-5207` |
| `DSKLEN` | `$DFF024` | `DSKLEN` | `custom.cpp:7302`, `disk.cpp:5134` |
| `DSKDAT` (escritura) | `$DFF026` | ignorado | `custom.cpp:7303` |
| `DSKBYTR` (lectura) | `$DFF01A` | `DSKBYTR` | `custom.cpp:7114`, `disk.cpp:4551` |
| `DSKSYNC` | `$DFF07E` | `DSKSYNC` | `custom.cpp:7343`, `disk.cpp:5153` |

`DSKPT` es un puntero de 32 bits **alineado a palabra** (`dskpt &= ~1`, `disk.cpp:5192-5197`;
`DSKPTL` fuerza `v & 0xfffe`, `disk.cpp:5206`).

## 2. `DSKLEN`: hay que **escribirlo dos veces**

`DSKLEN` (`disk.cpp:4838-4905`) separa el arranque de la DMA del primer valor escrito:

```
bit 15 (0x8000) = DMAEN   (habilitar DMA de disco)
bit 14 (0x4000) = WRITE   (1 = escritura, 0 = lectura)
bits 13-0       = longitud en WORDS (dsklength = dsklen & 0x3fff)
```

- **Lectura**: arranca cuando llega un segundo valor con `DMAEN` ya puesto
  (`(v & 0x8000) && (prevlen & 0x8000)`, `disk.cpp:4853-4864`).
- **Escritura**: igual pero con `WRITE` (`(v & 0x4000) && (prevlen & 0x4000)`, `disk.cpp:4892-4905`).
- Longitud 0 con DMA habilitada → **fin inmediato** (`disk.cpp:4887-4890`).
- Bajar `DMAEN` aborta la DMA en curso (`disk.cpp:4865-4882`).

Es exactamente el «escribir `DSKLEN` dos veces para arrancar» del AHRM; el primer valor **carga**,
el segundo **dispara**.

## 3. `DSKBYTR`: qué contiene y cuándo se recarga

Layout devuelto por `DSKBYTR` (`disk.cpp:4551-4573`):

```
bit 15 : DSKBYT     (byte disponible; la LECTURA lo pone a 0 -> se autolimpia)
bit 14 : DMAON      (DMA de disco activa: dskdmaen != OFF y dmaen(DMA_DISK))
bit 13 : DISKWRITE  (dsklen & 0x4000)
bit 12 : SYNC       (word == dsksync)
bits 7-0 : ultimo byte leido del flujo
```

- Se recarga cada **8 bits** (`canloaddskbytr`: `(bitoffset & 7) == 7`, `disk.cpp:3993-3997`).
- **No** se recarga durante una escritura (`(dsklen & 0xc000) == 0x4000` → `false`).
- `loaddskbytr`: `dskbytr_val = (word & 0xff) | 0x8000` (`disk.cpp:3978-3991`).
- El bit `SYNC` se enciende comparando la palabra actual con `DSKSYNC`; además `DSKSYNC` genera la
  interrupción de sync (`INTREQ_INT(12, ...)`, `disk.cpp:5153-5165`).

## 4. Fin de DMA → interrupción `DSKBLK`

`disk_dmafinished()` hace `INTREQ_INT(1, 1)` (`disk.cpp:3842-3844`): **`INTREQ` bit 1 = DSKBLK**,
la señal que espera el `trackdisk.device`. El canal de DMA es `DMA_DISK = 0x0010` (`DMACON` bit 4,
`include/custom.h:150`).

## 5. MFM crudo: el DMA mueve la pista ya codificada

WinUAE reconstruye la pista completa en `bigmfmbuf` (`disk.cpp:196`, `MAXMFMBUF`) y la codifica a
MFM en software:

- `mfmcode` (`disk.cpp:2059-2070`): inserta los bits de reloj (regla MFM).
- `mfmencodetable` (`disk.cpp:2072-2075`) y `dos_encode_byte` (`disk.cpp:2078-2088`): byte → 16 bits.
- `mfmcoder` (`disk.cpp:2090-2100`): recorre el buffer y encadena la codificación.

El DMA transfiere ese **bitstream MFM**, no bytes decodificados: coincide con el AHRM («el
controlador no decodifica MFM en hardware»). `tracklen` es la longitud de la pista en bits
(`disk.cpp:203, 1032`).

### 5.1 Pista AmigaDOS y arranque de la DMA

`decode_amigados` (`disk.cpp:2168-2277`) construye una pista DD de `11 * 544 + FLOPPY_GAP_LEN`
palabras (`FLOPPY_GAP_LEN = FLOPPY_WRITE_LEN - 11*544 = 350` con el valor PAL por defecto). Cada
sector son **544 palabras**:

```text
  [0,1]     $AAAA $AAAA     pad
  [2,3]     $4489 $4489     sync (dos palabras)
  [4..7]    cabecera        dodd_hi, dodd_lo, deven_hi, deven_lo   (format, track, sector, to_gap)
  [8..23]   etiqueta        16 B (dodd, luego deven)
  [24..27]  hck             XOR de los longs **crudos** de [4..23]
  [28..31]  dck             XOR de los longs **crudos** de [32..543]
  [32..287] datos dodd      256 words
  [288..543] datos deven     256 words
```

Después, `mfmcode(mfmbuf + 4, 541)` (`disk.cpp:2264`) **OR-ea los bits de reloj** en las
posiciones impares de todo lo que va tras el sync; por eso el decoder debe **enmascarar
`& 0x5555`** antes de reconstruir `dodd`/`deven`. Los `$4489` del sync quedan fuera del `mfmcode`.

El arranque de la DMA fija la alineación: `wordsync_detected` (`disk.cpp:4317-4319`) pone
`bitoffset = 15`, y `doreaddma` (`disk.cpp:4254-4284`) transfiere cuando `bitoffset == 15`, de modo
que **la primera palabra transferida es la siguiente al sync detectado**. En una pareja `$4489
$4489`, la DMA engancha el **primero** y el buffer empieza en el **segundo**: la pareja del sector
de arranque queda partida y ese sector solo reaparece completo en la **vuelta siguiente** (de ahí
`kMfmReadWords`, dos revoluciones). Alrededor del hueco de pista el flujo puede aparecer además
**desalineado a bit**, así que el decode busca el sync a nivel de bit y prueba la cabecera tanto a
**2** palabras del sync (se volcaron los dos) como a **1** (solo el segundo).

**Selección de cara**: `fetch_DISK_select` (`disk.cpp:3482-3491`) calcula `side = 1 - ((data >> 2)
& 1)`: el bit `SIDE` de CIA-B PRB va **invertido** respecto a la cara lógica (cara 0 → `SIDE = 1`).

**Rearme**: al terminar una DMA hay que **borrar** el bit de disco de `DMACON` (`disk.cpp:3842`,
`disk_dmafinished` deja `dskdmaen = OFF`, pero el bit de `DMACON` sigue puesto). El backend escribe
`DMACON = DMADISK` (SETCLR=0) y `DSKLEN = 0` antes de rearmar para que la primera escritura cargue
y la segunda dispare.

## 6. Lo que **no** es del emulador: montar `df0:`

Que `Open("df0:...")` funcione o se quede esperando depende de Kickstart/`dos.library` y del
handler de `trackdisk.device`, **no** de WinUAE. Insertar un ADF en `floppy0` monta el **medio**,
pero un programa arrancado solo desde `DH0` con Kickstart 1.3 mínimo puede no tener `df0:` como
volumen y `Open` **espera** al volumen. Para datos en disquete sin depender de DOS, la vía es
`trackdisk.device` a nivel de sector (§2-§5).

## 7. Implicación para el engine

- El backend `trackdisk` debe **escribir `DSKLEN` dos veces** (cargar y disparar), con la longitud
  en **words** y `WRITE` en el bit 14.
- Buffers **Chip RAM** (la DMA no ve Fast RAM) y `DSKPT` alineado a palabra.
- Sondeo de estado con `DSKBYTR` (bit 15 disponible, bit 14 DMAON) o espera de **`DSKBLK`**
  (`INTREQ` bit 1) — encaja con el patrón ISR del mini-SO (`MsgQueue::push_isr`).
- Si algún día se lee MFM crudo (`TD_RAWREAD`, 2.0+), el formato es el de §5.

## Referencias

- `docs/reference/ahrm/` (cap. del controlador de disco) + [ERRATA_Y_NOTAS.md](../../ahrm/ERRATA_Y_NOTAS.md).
- `docs/reference/amiga/techniques/` (técnicas de disco, si aplica).
- Fuente: `../WinUAE-DBG/custom.cpp`, `../WinUAE-DBG/disk.cpp`, `../WinUAE-DBG/include/custom.h`.
