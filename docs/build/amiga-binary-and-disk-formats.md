# Formatos Amiga y artefactos del toolchain

## Documentos padre

- [README principal](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/README.md)
- [Indice de documentación](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/engine-docs-index.md)
- [Kernel y loader del Amiga 500](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-kernel-loader-notes.md)
- [Cargador de desarrollo Amiga](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-dev-harness-loader.md)

## Objetivo

Dejar claro que produce nuestro build y como se usa cada artefacto dentro del flujo de carga.

## Alcance por maquina y kernel

Este documento se interpreta con la misma prioridad que el resto de la documentación del loader:

- **base principal:** Amiga 500 + Kickstart 1.3
- **compatibilidad probable:** Amiga 600, salvo que una herramienta o filesystem requiera algo más nuevo
- **extensión posterior:** Amiga 1200, AGA, hardfiles más complejos y caminos DOS más modernos

Esto importa especialmente en la parte de disco:

- `ADF` es la ruta más conservadora y natural para A500/Kick 1.3
- `HDF`/hardfile es una mejora posible de iteracion, no la ruta base

## Flujo real del repo

En este proyecto el camino normal es:

```text
.c / .s
  -> .o
  -> .elf
  -> .exe (Amiga Hunk)
  -> .adf (cuando se empaqueta para arranque por floppy)
```

## `.o`

Objeto relocatable de compilación.

En nuestro toolchain:

- lo genera `m68k-amiga-elf-gcc`
- vive en `obj/` u `obj/<battery-case>/`
- aún no es ejecutable AmigaDOS

## `.elf`

Artefacto principal de link con:

- simbolos
- secciones
- información de debug
- relocaciones emitidas

En este repo:

- salida tipica: `out/a.elf`, `out/battery_M03.elf`, etc.
- es el mejor artefacto para:
  - `objdump`
  - resolución de simbolos
  - mapping de direcciones en MCP

No es el formato final que AmigaDOS carga desde disco en nuestro flujo actual.

## `.exe`

En este repo, `.exe` significa ejecutable Amiga en formato Hunk, no PE/Windows.

Se genera con `elf2hunk` a partir del `.elf`.

Es el artefacto que:

- `exe2adf` empaqueta para disco
- `LoadSeg()` espera cargar por la vía DOS
- `winuae_load` intenta reubicar/escribir cuando hacemos diagnóstico directo

Regla importante:

- este `.exe` es el candidato natural para `LoadSeg`
- no deberiamos tratarlo por defecto como un blob “fijo” de código plano

## `.map`

Fichero de mapa del linker.

Nos sirve para:

- conocer la disposicion de secciones
- ver simbolos y direcciones link-time
- entender por qué GDB o MCP necesitan `qOffsets` o bases reales cuando el loader mueve los segmentos

Ejemplos del repo:

- [out/battery_M00.map](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/out/battery_M00.map)
- [out/battery_M03.map](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/out/battery_M03.map)

## `.out`

No es el artefacto actual del repo, pero aparece mucho en documentación antigua Amiga/Unix.

Conviene distinguir:

- `a.out` historico o nombres `.out` genericos del host/toolchain
- nuestro `.elf` de debug
- nuestro `.exe` Hunk para AmigaDOS

Para este proyecto, la equivalencia practica es:

- si una guia vieja habla de “binary output” o `.out`, hoy hay que comprobar si se refiere a un binario intermedio del compilador o al ejecutable Hunk final.

## `ADF`

Imagen cruda de disquete Amiga de 3.5" DD:

- tamano típico: `901120` bytes
- representa pistas/sectores del floppy sin encapsulado adicional complejo

En el repo:

- `scripts/create-adf.bat` y `scripts/create-adf.sh`
- salida tipica: `out/disk.adf`, `out/battery_M03.adf`

Uso:

- boot de `DF0:`
- fallback más conservador y cercano al flujo real del sistema

Compatibilidad:

- **A500/Kick 1.3:** ruta base recomendada
- **A600:** compatible en general
- **A1200:** compatible, pero ya no es la única opcion razonable

## HDF / hardfile

Alternativa a ADF cuando necesitamos:

- más espacio
- menos friccion para iterar payloads
- filesystem persistente de desarrollo

Dos modelos practicos:

1. hardfile entero con **RDB** y particiones Amiga
2. directorio del host montado como volumen/hard drive virtual por el emulador

Para un método robusto de desarrollo, un hardfile puede ser mejor que ADF si queremos:

- un harness residente fijo
- y payloads actualizables en el filesystem sin reconstruir un floppy cada vez

Compatibilidad:

- **A500/Kick 1.3:** posible, pero no debería convertirse en supuesto por defecto sin smoke test propio
- **A600/A1200:** más natural como ruta de iteracion extendida

## RDB y filesystem

En discos duros Amiga es habitual:

- **RDB** (Rigid Disk Block) como tabla de particiones/metadata
- filesystem tipo **FFS** u otro handler

Implicacion para este proyecto:

- si el fallback por ADF sigue siendo demasiado estrecho, un hardfile con una particion de desarrollo y `PROGDIR:`/`SYS:` bien definidos puede simplificar mucho la carga de payloads DOS.

## Conclusiones operativas

- **Para debug simbólico**: usar `.elf` y `.map`
- **Para AmigaDOS real**: usar `.exe` y cargarlo vía `LoadSeg`
- **Para boot sencillo y conservador en A500/Kick 1.3**: empaquetar en `.adf`
- **Para iteracion más comoda a medio plazo**: valorar hardfile/HDF con filesystem persistente, marcado como extensión a la ruta base

## Fuentes consultadas

- [Amiga ROM Kernel Reference Manual: DOS](https://developer.amigaos3.net/sites/default/files/downloads/2024-10/Amiga_ROM_Kernel_Reference_Manual_DOS.pdf)
- [cabeceras `dos/doshunks.h` y `devices/hardblocks.h` del SDK Amiga incluido en el toolchain local]
