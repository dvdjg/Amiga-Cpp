# Herramientas del pipeline de aventuras (`tools/adventure/`)

Infraestructura **host** (PC) del pipeline de conversión de aventuras clásicas al engine: el plan y las decisiones están en [`ROADMAP_ADVENTURE_IMPORT.md`](../../docs/guides/roadmap/ROADMAP_ADVENTURE_IMPORT.md) (hitos I0–I5) y el diseño de la capa destino en [`ADVENTURE_LAYER.md`](../../docs/engine/architecture/ADVENTURE_LAYER.md).

## Estado

- **Hecho**: compilación local del oráculo (`build-scummvm-tools.sh`, por ahora `descumm`).
- **Planificado (I0–I5)**: esquema del SIM (`tools/adventure/sim/`), extractores por familia (`tools/adventure/frontends/`), traductor de guiones (`tools/adventure/translate/`) y validador de presupuesto (`tools/adventure/report/`).

## `build-scummvm-tools.sh`

Compila herramientas de **scummvm-tools** (GPL-3.0) desde las fuentes locales (`../SCUMM/scummvm-tools-2.9.0/`, ver «Fuentes locales» del roadmap) con el toolchain del host. Motivos frente a alternativas: los binarios oficiales de Windows dan **falsos positivos de antivirus**; el árbol completo `configure`+`make` arrastra todas las tools y sus librerías (wx, png, zlib, freetype); y una receta propia es **modificable** (parchear una fuente y reconstruir es inmediato).

```bash
# Desde la raíz del repo (Git Bash): compila descumm en ../SCUMM/scummvm-tools-2.9.0/descumm.exe
bash tools/adventure/build-scummvm-tools.sh

# Opciones: --src <dir> (fuentes), --out <dir> (binario), --help; entorno: CXX, CXXFLAGS
```

Notas:

- El binario se escribe **junto a las fuentes** (nunca en el repositorio; no hay binarios en git).
- La receta fija `-std=gnu++17` y enlaza estático (`-static`), para un ejecutable portable que no dependa de DLLs del toolchain.
- La herramienta se usa como **proceso externo** (los scripts del pipeline la invocan): nada de GPL se enlaza en `engine/` ni en el binario Amiga.

## Uso esperado en el pipeline

`descumm -5 -u script.bin` (o la versión correspondiente: `-3`, `-4`, `-6`, `-gNNN` para HE) descompila scripts SCUMM a texto legible; el frontend de I0/I1 lo invoca y parsea su salida hacia el SIM. Smoke test sin juego: `printf '\xde\xad\xbe\xef\x80\xa0' > t.bin && descumm -5 -u t.bin` debe imprimir `breakHere();` y `stopObjectCode();`.
