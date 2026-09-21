# Consulta a IA externa — disco a bajo nivel (`trackdisk`/`df0:`) y carga dinámica

> Documento **autocontenido** para una IA externa (Grok). Reúne el contexto del proyecto, el
> estado real verificado y las preguntas abiertas. No requiere acceso al repositorio.

## 0. Contexto del proyecto

- **Engine C++23 para Amiga 500 (m68k)**, compilado con `-nostdlib`: **sin heap, sin excepciones,
  sin RTTI**. Salida HUNK (`elf2hunk`). Pensado para juegos/demos, no para aplicaciones AmigaDOS.
- **Mini-SO `eng::os`**: puertos de mensajes de capacidad fija, latches de VBlank, timers,
  productores de entrada (joystick/ratón/teclado), E/S.
- **E/S por `dos.library`** (`eng::os::file_*`), ya implementada y verificada:
  `file_open/close`, `file_read_sync`/`file_write_sync`, `file_read_async`/`file_write_async` +
  `file_pump` (diferido), `file_size`, `file_make_dir`, `file_delete`, `file_rename`.
- **Recursos `eng::res`**: `AssetCache` (presupuesto/prioridad/LRU/refcount), `DynLoader`.
- **Entorno de pruebas**: WinUAE-DBG (GDB en puerto 2345 + **canal lateral** para leer el estado de
  la demo). Config: **Kickstart 1.3**, `quickstart=a500,1`, `cpu_cycle_exact=true`. **No hay
  Workbench**.
- **Arranque**: el runner escribe `dh0:s/startup-sequence` = `stack 131072` / `cd dh1:` / `:a.exe`.
- **Datos**: `DH1:` es un directorio host montado por el runner; `tools/fs/make-volume.mjs` también
  genera un **ADF** (FFS) con `xdftool` (amitools). `run-demo.sh --disk <adf>` lo inserta como
  `floppy0` (`DF0:`).
- **Referencias**: AHRM; `amiga-bootcamp/10_devices/trackdisk.md`, `07_dos/`,
  `03_loader_and_exec_format/`; `dos/doshunks.h` (NDK).

Evidencia reciente: demo `211_fs_test` (lee `DH1:`: texto/imagen/sonido, carga **dos** módulos
dinámicos y ejecuta `answer()` → 42, escribe y relee un fichero) llega a READY con y sin ADF
insertado.

---

## 1. Leer `df0:` se bloquea (entorno sin Workbench)

**Síntoma**: con el ADF insertado (`--disk`), una demo que hace `Open("df0:data/...")` **no llega a
READY** (se cuelga, sin error). La misma demo leyendo `DH1:` funciona. Y la demo 211, que **no** toca
`df0:`, llega a READY **con el ADF insertado** (luego el montaje del disquete no rompe el arranque).

**Hipótesis**: en Kickstart 1.3 sin Workbench, el volumen del disquete **no queda montado** como
`df0:`, y `Open` espera indefinidamente a que el handler/volumen exista.

**Preguntas**:
1. ¿Cómo se monta `df0:` **programáticamente** desde un proceso CLI sin Workbench (KS 1.3)?
   ¿`Mount`/`Device` de `dos.library`? ¿Hace falta el handler `trackdisk`/`FileSystem.resource`?
2. ¿`Open("df0:...")` **bloquea** o devuelve error si no hay volumen? ¿Cómo distinguir "no hay
   disco" de "no hay volumen montado" sin bloquear?
3. ¿Es mejor (a) arrancar de un ADF con Workbench, (b) hacer que el runner **no** arranque del
   disquete y lo monte como unidad de datos, o (c) montar el volumen explícitamente al arrancar?

---

## 2. `trackdisk.device` a nivel de device

Objetivo: leer/escribir sectores y pistas **sin** pasar por el sistema de ficheros (streaming de
datos, bootblock, formatos no-DOS).

**Lo que sabemos** (de `trackdisk.md` + AHRM): el controlador de disco **no decodifica MFM en
hardware**: hace DMA del bitstream MFM crudo a **Chip RAM**; el decode lo hace `trackdisk.device`.
Registros `DSKBYTR $DFF01A`, `DSKPT $DFF020`, `DSKLEN $DFF024` (se escribe **dos veces** para
arrancar la DMA); motor/lado/step por **CIA-B PRA/PRB `$BFD100`**. Interfaz `IOExtTD` +
`DoIO`/`SendIO`; comandos `CMD_READ/WRITE/UPDATE/CLEAR`, `TD_MOTOR`, `TD_CHANGENUM/STATE/PROTSTATUS`,
`TD_FORMAT`, `TD_RAWREAD/WRITE` (2.0+). **Cache de pista completa** (11 sectores) por acceso.

**Restricciones del engine**: sin heap → buffers estáticos/arena; sin excepciones; el mini-SO ya
tiene `MsgQueue::push_isr` y latch de VBlank.

**Preguntas**:
1. Modelo **mínimo** para leer un sector y una **pista completa** con `trackdisk.device` en KS 1.3:
   `CreateMsgPort`/`CreateIORequest`/`OpenDevice("trackdisk.device", unit, ...)` + `DoIO`. ¿Cuál es
   el ciclo correcto de motor (`TD_MOTOR`), `CMD_UPDATE` y cierre?
2. ¿Cómo detectar de forma **no bloqueante** que no hay disco (`TD_CHANGESTATE`) y cómo usar
   `TD_CHANGENUM` para evitar leer del disco equivocado?
3. ¿Merece la pena la cache de pista completa para streaming, o es mejor pista-a-pista?
4. Diferencias relevantes KS 1.3 ↔ 2.0+ (`TD_RAWREAD`, `TD_ADDTRACK`, `TD_GETDRIVETYPE`).
5. ¿Conviene `DoIO` (síncrono) dentro del bucle de juego, o `SendIO` + `DSKBLK`/señal para no
   bloquear el frame?

---

## 3. Buffers DMA en Chip RAM vs política de Fast RAM

El engine usa **Fast RAM** para trabajo de CPU (regla de diseño). Pero la DMA de disco/blitter/audio
exige **Chip RAM**: usar Fast RAM produce corrupción silenciosa.

**Pregunta**: ¿cuál es el API correcto para que el llamante **no pueda** pasar un buffer de Fast RAM
por error? Opciones que barajamos: un tipo de dominio `ChipBuffer`/`Bytes<ChipTag>` que solo se
construya desde la arena de Chip; o una política de plantilla. ¿Experiencia/precedentes en engines
Amiga reales?

---

## 4. Carga dinámica: `.englib` propio **y** HUNK nativo (ya implementados)

`DynLoader` detecta el formato por el primer longword:
- **`.englib`** (magic `'ENGL'`, `0x454E474C`): contenedor propio compacto en **orden nativo** de la
  máquina; el código vive **in situ** y las relocaciones se aplican sobre la imagen.
- **HUNK** (magic `HUNK_HEADER`, `0x000003F3`): formato **nativo AmigaOS**, **big-endian**; los
  segmentos (`HUNK_CODE`/`HUNK_DATA`/`HUNK_BSS`) se reservan en una `LinearArena` del llamador y se
  copian (la imagen puede liberarse tras cargar).

Soportamos `HUNK_RELOC32`, `HUNK_RELOC32SHORT`, `HUNK_DREL32`, `HUNK_RELOC16/8`,
`HUNK_ABSRELOC16`, `HUNK_RELRELOC32` y `HUNK_SYMBOL` (índice por hash FNV-1a).

**Verificado**: host (HOST-258: segmentos, reloc 32 y 32SHORT, símbolo) y **en la Amiga** (demo 211:
carga un `.englib` **y** un `.hunk`, ambos ejecutan `answer()` → 42).

**Preguntas** (posibles errores de interpretación del formato):
1. El NDK dice `HUNKF_CHIP = 1<<30`, `HUNKF_FAST = 1<<31`, `HUNKF_ADVISORY = 1<<29`; un doc de la
   comunidad decía `HUNKF_FAST = 1<<29`. ¿Confirmas el NDK? ¿La máscara del **tipo** de hunk debe
   ser `0x1FFFFFFF` (limpia 31/30/29) o `0x3FFFFFFF` (conserva advisory)?
2. En la **tabla de tamaños** del `HUNK_HEADER`, ¿el tamaño es `entry & 0x3FFFFFFF` (bits 29-0) o
   hay que limpiar también el bit 29 (advisory)?
3. `HUNK_RELRELOC32`: ¿`*patch += target_base − (base + off + 4)` o `*patch = target_base − (base +
   off)`? (dos fuentes discrepan).
4. `HUNK_RELOC32SHORT`/`DREL32`: campos de 16 bits y **relleno a longword si el total de words es
   impar**. ¿Correcto?
5. ¿Los ejecutables producidos por `elf2hunk` (toolchain `m68k-amiga-elf`) incluyen `HUNK_SYMBOL`, o
   hay que generarlos/strippearlos? ¿Es fiable indexar símbolos de un HUNK en producción?
6. ¿Ventajas/inconvenientes de usar `LoadSeg()` del OS frente a un loader propio (dado que queremos
   **sin** dependencia de Workbench y overlays cargables/descargables)?
7. ¿Riesgos de seguridad/robustez que deberíamos validar (offsets fuera del hunk, `target >= num`,
   tamaño 0, BSS no cero)?

---

## 5. Stub del `.englib` ensamblado a mano (fragilidad)

Tuvimos un bug real: un `moveq` mal codificado (`0x706A` en vez de `0x702A`) hacía que `answer()`
devolviera 106 en lugar de 42. Hoy el stub se escribe **byte a byte** en un script host.

**Pregunta**: ¿conviene generar el stub con **vasm** (el ensamblador del toolchain) y empaquetar su
salida, en vez de codificar instrucciones a mano? ¿Cómo integraríais el ensamblado en el pipeline
host sin depender de binarios del toolchain en la máquina de build?

---

## 6. Teclado: falta inyección de teclas para verificar

El backend de teclado lee la CIA-A (ISR del SP) + `JOYxDAT`. **No hay forma de inyectar teclas** en
el runner (solo `--automation-key`, que escribe memoria). El teclado está implementado pero **no
verificado de extremo a extremo**.

**Pregunta**: ¿cómo inyectar pulsaciones de tecla reales en WinUAE-DBG? ¿Por GDB (escribiendo
`CIAAPRA`/`SERDAT` o con un comando de entrada), por el **canal lateral**, o por una opción de
configuración de WinUAE? Queremos verificar el camino completo (scancode → ISR → `Msg`).

---

## 7. E/S asíncrona de `dos.library`

`file_read_async`/`file_write_async` encolan y `file_pump` (llamado desde el bucle) comprueba/entrega
los `FileDone`. **Pregunta**: ¿es este modelo fiel a `dos.library` (`DoIO` vs `SendIO` + paquete
`dos`), o hay casos (ficheros grandes, `Seek`, disquete) donde el diferido se comporta distinto?

---

## 8. ADF: datos vs arranque

Si el ADF tiene bootblock válido, Kickstart **arranca de él** en vez de `DH0`. **Pregunta**: ¿cuál es
la forma **fiable** de marcar un ADF como "datos, no arrancable" (bootblock no válido) o de montarlo
como unidad de datos sin que Kickstart intente arrancar?

---

## Anexo — deuda preexistente (no relacionada con lo anterior)

- **F0.8**: la demo 107 muere antes de `XlimitedScene::begin` (necesita sesión GDB).
- **F4.6**: `build_frame` consume ~84 % del frame (pendiente instrumentación/perfilado).
- **HOST-013** (`math3d_mesh`): falla `convex_spans: pentagono == referencia`.
- **4 demos de audio** sin assets `.raw` (quedan en `ASSET`, no en `ok`).

---

## Respuestas verificadas (nuestro lado)

Tras la respuesta de Grok, esto es lo **comprobado en el repo** y lo **decidido**:

| Tema | Respuesta de Grok | Estado en el repo |
|---|---|---|
| Flags HUNK (§4.1-4.2) | NDK: `ADVISORY=1<<29`, `CHIP=1<<30`, `FAST=1<<31`; tipo `& 0x1FFFFFFF`; tamaño `& 0x3FFFFFFF` | **Ya correcto**: `eng/res/hunk.hpp` usa esos valores y máscaras. Verificado en host (**HOST-258**) y en Amiga (demo 211: `.englib` + `.hunk` → `answer()==42`) |
| `HUNK_RELRELOC32` (§4.3) | `*patch += target − (patch+4)` | Implementado así; sin caso real que lo ejercite (raro) |
| `RELOC32SHORT`/`DREL32` (§4.4) | 16 bits + padding si words impar | Implementado y probado (HOST-258) |
| Símbolos HUNK (§4.5) | No depender de `HUNK_SYMBOL`; **exports propios** | **Decidido**: el `.englib` ya lleva exports propios; para HUNK, el pipeline debe emitir una tabla de exports propia (pendiente) |
| Stub `.englib` (§5) | Generar con **vasm/CI**, no a mano | **Decidido** (pendiente): hoy se emite byte a byte en `tools/fs/make-volume.mjs` |
| `df0:` sin Workbench (§1) | `Open` **espera** al volumen; montar en KS 1.3 no es fiable | **Confirmado**: demo 211 llega a READY con ADF insertado (no toca `df0:`); el volumen no queda montado. Ficha nueva: `docs/reference/emulators/winuae/trackdisk.md` |
| `trackdisk` (§2) | `OpenDevice` + `IOExtTD` + `DoIO`, buffers Chip RAM, `DSKBLK` | **Implementado** (`eng/os/trackdisk.hpp` + `amiga_minimal_trackdisk.cpp`). **Bloqueado**: la demo que lo usa se **cuelga dentro de `td_open`** incluso con unidad ausente (`DF3:`) → es el `OpenDevice`/`CreateMsgPort` genérico en nuestro entorno `-nostdlib`, no el disco. Necesita sesión de depuración |
| Teclado (§6) | Inyectar en el **mismo** post que la ISR, o foco+tecla | **Decidido**: inyección por el canal lateral al mismo `post_msg` (pendiente) |
| E/S async (§7) | El diferido es un scheduler sobre DOS, no `SendIO` | Confirmado; `trackdisk` (cuando funcione) irá por `SendIO` + señal → `Msg` |

**Hallazgo del emulador** (ficha nueva, patrón `AGENTS.md` §1.11): `DSKLEN` se escribe **dos
veces** para disparar; `DSKBYTR` se recarga cada 8 bits y **no** durante escritura; el fin de DMA
levanta `INTREQ` bit 1 (**DSKBLK**). Citas `fichero:línea` en
`docs/reference/emulators/winuae/trackdisk.md`.
