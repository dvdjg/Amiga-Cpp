# Demo 214 — disquete a bajo nivel (DMA crudo + decode MFM)

Lee la pista 0 (cara 0) de `DF0:` **sin `trackdisk.device` ni `dos.library`**: control mecánico por
CIA-B PRB, DMA crudo de Paula (`DSKPT`/`DSKLEN` doble + `WORDSYNC`/`DSKSYNC`) y decodificación MFM
en CPU. Valida el bootblock (firma `DOS\0`/`DOS\1` + checksum).

Es la vía que **sí** funciona sin Workbench (el `Open("df0:")` se bloquea; ver
`docs/debugging/CONSULTA-GROK-DISCO-Y-LOADER.md`).

## Estado: **DMA validado, decode MFM pendiente**

Lo verificado en A500 (emulador, ADF insertado con `--disk`):

```
motor: OK   palabras DMA: 6400          <- motor+select+seek+DMA+DSKBLK OK
syncs: 7    w0..3: 4489 552a 4a89 5529  <- la pista es AmigaDOS ($4489 presentes)
sector 0: NO                            <- el decode aún no aísla el sector
```

- **Funciona**: motor/select de DF0, seek a pista 0 (`/TK0`), lectura DMA de la pista a Chip RAM y
  espera de `DSKBLK`. La pista contiene syncs `$4489` → el medio se lee bien.
- **Pendiente**: el decode busca el sync **alineado a palabra**; 4 de los 11 sectores lo tienen a
  caballo de palabra (el hueco entre sectores no es múltiplo de 16 bits). Hay que buscar el sync
  **bit a bit** y deslizar la ventana. El test host `HOST-259` valida el decode con una pista
  sintética **alineada** (encoder = el del emulador).

## Compilar / ejecutar

```bash
node tools/fs/make-volume.mjs
bash ./tools/build/build-demo.sh demos/amiga/214_floppy_raw --debug --clean
bash ./tools/run/run-demo.sh demos/amiga/214_floppy_raw --disk out/fs/211_fs_test.adf --wait-ms 12000
```

## Qué usa

- `eng/os/floppy.hpp`: `floppy_motor`/`floppy_present`/`floppy_read_track` (backend
  `amiga_minimal_floppy.cpp`) y el decode puro `mfm_decode_long`/`floppy_find_sector`.
- Ficha del emulador: `docs/reference/emulators/winuae/trackdisk.md` (registros y DMA).
- Formato de pista: `WinUAE-DBG/disk.cpp:2185-2260`. CIA-B PRB `$BFD100` / CIA-A PRA `$BFE001`
  (AHRM Table 8-5).
