# Consulta para IA externa — forzar hunk en Chip RAM con este toolchain

Pega esto a un modelo experto en Amiga/68k.

## Contexto
Compilo demos Amiga con **m68k-amiga-elf-gcc 15.1.0 (toolchain bebbo)** y convierto el
ELF a formato HUNK con **elf2hunk** (AROS, 2017, modificado por Bartman/Abyss 2020,
`C:\...\bin\win32\elf2hunk.exe`). Quiero que un dato (un tilebank de 294 KB, ~1149 tiles)
quede en **Chip RAM** cargado por el AmigaOS loader (`LoadSeg`), para blit-earlo directo
sin copia por CPU en runtime.

## Qué hice y qué observe
1. En C++:
   ```cpp
   __attribute__((section(".chip"))) volatile unsigned char kBank[256] = {0x42};
   ```
2. `elf2hunk file.elf file.hunk -v` mostró:
   ```
   .chip` -> Hunk #2 (ELF section #3 '.chip'), DATA, lsize=65
   ```
   Es decir, se creó un HUNK DATA propio para `.chip`, pero **sin ningún indicio de
   HUNKF_CHIP** (el flag del hunk para obligar Chip RAM).
3. `strings elf2hunk.exe` contiene `, HUNKF_CHIP` y `.MEMF_CHIP`, así que el soporte
   existe, pero no sé qué nombre de sección/atributo lo activa.

## Pregunta
Con este toolchain (gcc 15 + elf2hunk), ¿cuál es la forma CONFIRMADA de conseguir que un
hunk de datos se cargue en Chip RAM (flag **HUNKF_CHIP**) para bigenerlo por `LoadSeg`?
En concreto:

1. ¿Qué nombre de sección reconoce `elf2hunk` como CHIP (`.chip`? `.rocchip`? `.chip.data`?
   `.m68k.sdata`? algo de AROS como `MEMF_CHIP`/atributo)? ¿O hay que usar un linker script
   (`vlink`/`-T`) que reempaque la sección a un hunk marcado CHIP?
2. ¿Cuál es la receta exacta (atributo de sección C y/o asm `.section/.incbin`) para meter
   el `.bin` en esa sección chip de forma que `elf2hunk` la emita como hunk CHIP?
3. ¿Cómo se **verifica** que el hunk es CHIP (campo de flags del HUNK_HEADER: HUNKF_CHIP =
   0x2000; o en tiempo de ejecución en qué memoria lo puso `LoadSeg`)?

Contexto adicional: el resto del binario (código + `.data`) va al hunk por defecto; solo
EL tilebank debe ir forzado a Chip. Si el toolchain no lo permite con secciones, indica la
alternativa estándar (cargar el `.bin` a Chip en runtime con `AllocMem`+`MEMF_CHIP`, o un
linker script específico) y el coste/beneficio de cada opción.**