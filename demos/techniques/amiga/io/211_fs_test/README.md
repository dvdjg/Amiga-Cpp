# Demo 211 — sistema de archivos (`dos.library`) + carga dinámica de código

Lee un **volumen DH1:** generado por el host (directorios + texto + imagen + sonido + código
relocatable en dos formatos), **carga y ejecuta** ambos módulos, prueba la **escritura** (crea
`out/result.txt`, lo escribe y lo relee) y **transmite por rebanadas** el fichero de **512 KB**
(`data/audio/tone_8k_512k.raw`) con `FileChunkFeeder` (M8). Toda la E/S va por `eng::os::file_*`
(implementado sobre `dos.library`).

## Generar el volumen

```bash
node tools/fs/make-volume.mjs
```

Escribe el contenido en `out/fs/content` (el runner lo monta en el `DH1:` de **cualquier** demo) y
el ADF: `data/text/hello.txt`, `data/images/logo.raw` (16×16), `data/audio/beep.raw` (256 B),
`data/audio/tone_8k_512k.raw` (512 KB) y dos módulos con una función `answer()` que devuelve 42:
`data/code/answer.englib` (formato propio, con una celda relocable) y `data/code/answer.hunk`
(formato **nativo HUNK**).

## Compilar / ejecutar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/io/211_fs_test --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/io/211_fs_test --wait-ms 8000
```

## Resultado de referencia (A500)

```
texto: OK        (lee data/text/hello.txt)
imagen bytes: 256   sonido bytes: 256
englib: OK (answer=42)      (carga dinámica + ejecución, formato propio)
hunk: OK (answer=42)        (carga dinámica + ejecución, formato nativo HUNK)
escritura: OK   releidos: 23   mkdir out: ya existia
stream 512 KB: OK   bytes: 524288   chunks: 128
```

El run-status (`detail = 0x00021100 | flags`) lleva un bit por prueba; en la validación da
`flags = 0x1F` (texto, englib, escritura, hunk y **stream**).

## Qué prueba

- **Lectura**: texto, imagen y sonido (tamaños) desde directorios del volumen.
- **Carga dinámica**: `DynLoader` **detecta el formato** y carga los dos módulos, relocaliza y
  llama a `answer()` → 42 en ambos: `.englib` (propio) y **HUNK** (nativo AmigaOS, con segmentos
  reservados en una `LinearArena`).
- **Escritura**: `file_open(..., Create)` + `file_write_sync` + relectura (`file_read_sync`).
- **Streaming (M8)**: lee `data/audio/tone_8k_512k.raw` (512 KB) en **rebanadas de 4 KB** con
  `eng::os::FileChunkFeeder` (doble buffer `ChunkStream<2>` sobre `file_read_async`); al llegar el
  `FileDone` (por el puerto del sistema) libera el buffer y lanza la siguiente lectura hasta el
  **EOF** (128 chunks). `ok` exige `bytes == tamaño` y `chunks > 1`.

Diseño: `docs/engine/architecture/MINI_OS_IO.md` y `RESOURCE_SYSTEM.md`. Contrato: `eng/os/file.hpp`;
feeder: `eng/os/file_stream.hpp`; loaders: `eng/res/dynloader.hpp` (`.englib` + detección) y
`eng/res/hunk.hpp` (HUNK).
