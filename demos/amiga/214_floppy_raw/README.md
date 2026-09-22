# Demo 214 — disquete a bajo nivel (DMA crudo + decode MFM)

Lee la pista 0 (cara 0) de `DF0:` **sin `trackdisk.device` ni `dos.library`**: control mecánico por
CIA-B PRB, DMA crudo de Paula (`DSKPT`/`DSKLEN` doble + `WORDSYNC`/`DSKSYNC`) y decodificación MFM
en CPU. Valida el bootblock leyendo sus dos primeros sectores: firma `DOS\0`/`DOS\1` y los
**checksums propios de cada sector AmigaDOS** (`hck`/`dck`).

Es la vía que **sí** funciona sin Workbench (el `Open("df0:")` se bloquea; ver
`docs/debugging/investigaciones/consulta-grok-disco-y-loader.md`).

## Estado: **validada en A500**

```
motor: OK   palabras DMA: 12668
sectores bootblock 0/1: OK (hck/dck)   hdr track=0 sector=0
bootblock: DOS1 (FFS)   firma: OK
```

Puntos que resuelve el decode, documentados en `docs/reference/emulators/winuae/trackdisk.md` §5.1:

- **Bits de reloj**: el encoder OR-ea relojes en las posiciones impares (`mfmcode`); hay que
  enmascararlos (`& 0x5555`) antes de reconstruir `dodd`/`deven`.
- **Sync de arranque**: la DMA arranca en el **segundo** `$4489` de la pareja y el primer sync se
  lo consume la detección, así que el sector del arranque puede aparecer con **un solo** sync; el
  decode prueba la cabecera a 1 o 2 palabras del sync.
- **Alineación de bit**: el flujo puede no estar alineado a palabra (jitter/hueco de pista); el
  sync se busca a nivel de **bit**.
- **Cara**: WinUAE calcula `cara = 1 - SIDE` (`disk.cpp:3489`), por eso el bit se escribe invertido.
- **Checksums**: `hck` (cabecera+etiqueta) y `dck` (datos) se validan, así que un decode erróneo se
  rechaza. El ADF se deja **no arrancable** a propósito (un bootblock válido haría arrancar de él).

La fase de rotación varía entre lecturas y el sector del sync de arranque puede quedar partido, así
que el arranque **reintenta** la lectura (como `trackdisk.device`) hasta verificar ambos sectores.

## Compilar / ejecutar

```bash
node tools/fs/make-volume.mjs
bash ./tools/build/build-demo.sh demos/amiga/214_floppy_raw --debug --clean
bash ./tools/run/run-demo.sh demos/amiga/214_floppy_raw --disk out/fs/211_fs_test.adf --wait-ms 12000
```

## Qué usa

- `eng/os/floppy.hpp`: `floppy_motor`/`floppy_present`/`floppy_read_track` (backend
  `amiga_minimal_floppy.cpp`) y el decode puro `mfm_decode_long`/`floppy_find_sector`.
- Ficha del emulador: `docs/reference/emulators/winuae/trackdisk.md` (registros, DMA y layout).
- Formato de pista: `WinUAE-DBG/disk.cpp:2168-2277`. CIA-B PRB `$BFD100` / CIA-A PRA `$BFE001`
  (AHRM Table 8-5).
