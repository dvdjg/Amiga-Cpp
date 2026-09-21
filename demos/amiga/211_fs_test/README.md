# Demo 211 — sistema de archivos (`dos.library`) + carga dinámica de código

Lee un **volumen DH1:** generado por el host (directorios + texto + imagen + sonido + un `.englib`),
**carga y ejecuta** el código relocatable, y prueba la **escritura** (crea `out/result.txt`, lo
escribe y lo relee). Toda la E/S va por `eng::os::file_*` (implementado sobre `dos.library`).

## Generar el volumen

```bash
node tools/fs/make-volume.mjs
```

Escribe en `out/run/211_fs_test/A500_debug/dh1` (el dir que el runner monta como `DH1:`):
`data/text/hello.txt`, `data/images/logo.raw` (16×16), `data/audio/beep.raw` (256 B) y
`data/code/answer.englib` (una función `answer()` que devuelve 42, con una celda relocable).

## Compilar / ejecutar

```bash
bash ./tools/build/build-demo.sh demos/amiga/211_fs_test --debug --clean
bash ./tools/run/run-demo.sh demos/amiga/211_fs_test --wait-ms 8000
```

## Resultado de referencia (A500)

```
texto: OK        (lee data/text/hello.txt)
imagen bytes: 256   sonido bytes: 256
englib: OK (answer=42)      (carga dinámica + ejecución)
escritura: OK   releidos: 23   mkdir out: ya existia
```

## Qué prueba

- **Lectura**: texto, imagen y sonido (tamaños) desde directorios del volumen.
- **Carga dinámica**: `DynLoader` carga `answer.englib`, relocaliza y llama a `answer()` → 42.
- **Escritura**: `file_open(..., Create)` + `file_write_sync` + relectura (`file_read_sync`).

Diseño: `docs/engine/architecture/MINI_OS_IO.md` y `RESOURCE_SYSTEM.md`. Contrato: `eng/os/file.hpp`;
loader: `eng/res/dynloader.hpp`.
