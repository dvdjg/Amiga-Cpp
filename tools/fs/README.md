# `tools/fs` — volúmenes de prueba para el sistema de archivos

Genera el contenido que usan las demos de sistema de archivos (`eng::os::file_*` sobre
`dos.library`), en dos formas:

- un **árbol de directorios** en disco (el `DH1:` que monta el runner), y
- una **imagen de disquete ADF** (FFS/OFS) con el mismo contenido (para `DF0:`).

## Uso

```bash
node tools/fs/make-volume.mjs [--out <dir>] [--adf <path>]
```

Defectos: `out/run/211_fs_test/A500_debug/dh1` y `out/fs/211_fs_test.adf`.

Contenido: `data/text/hello.txt`, `data/images/logo.raw` (16×16), `data/audio/beep.raw` (256 B) y dos
módulos con `answer()` → 42: `data/code/answer.englib` (formato propio, con celda relocable) y
`data/code/answer.hunk` (formato **nativo HUNK**, con símbolo `answer`).

La imagen ADF se construye con **`xdftool`** (paquete `amitools`, `python -m amitools.tools.xdftool`).

## Montar la imagen en el runner

```bash
bash ./tools/run/run-demo.sh demos/amiga/211_fs_test --disk out/fs/211_fs_test.adf
```

`--disk <adf>` añade `floppy0=<adf>` al `runner.uae` (disquete en `DF0:`, lectura/escritura).

## Estado (limitación conocida)

El **disquete se monta** (el harness arranca de `DH0` y la demo 211 llega a READY con el ADF
insertado), pero **leer `df0:` desde una demo se bloquea**: en este entorno **sin Workbench** el
volumen del disquete no queda montado como `df0:` (Kickstart solo lo monta al arrancar de él o bajo
Workbench), así que `Open("df0:...")` espera indefinidamente.

Vías para cerrarlo:
- un **`Mount`/`Device`** explícito del volumen en el arranque (dos.library), o
- arrancar de un Workbench/ADF preparado que monte el disquete, o
- una opción del runner para no arrancar del disquete y montarlo como unidad de datos.

Mientras tanto, la lectura de un volumen se prueba con el **`DH1:`** (demo 211), que cubre
directorios, ficheros (texto/imagen/sonido), carga dinámica de `.englib` y **escritura**.
