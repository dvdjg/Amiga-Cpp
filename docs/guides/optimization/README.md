# Guías de optimización

Optimización de código C++ y del chipset para las plataformas soportadas
(68000/Amiga primero). Organización canónica en `docs/STRUCTURE.md` §10.

## Documentos

| Documento | Contenido |
|-----------|-----------|
| [OPTIMIZACION_GPP_68000.md](OPTIMIZACION_GPP_68000.md) | Cómo compila g++/elf2hunk para 68000: reglas de tamaño vs velocidad, `-Os`, cuándo `-O1` engaña y warnings de `-Wextra`. |
| [METODOLOGIA_PROFILING.md](METODOLOGIA_PROFILING.md) | Pipeline de medida (fps → secciones → muestras de CPU → bus), herramientas, trampas conocidas y optimizaciones identificadas con su evidencia. |
| [_probe_gpp68000.cpp](_probe_gpp68000.cpp) | Sonda de compilación usada para medir el código generado (complementa el doc). |

## Reglas obligatorias
- Reglas de rendimiento, comentarios de optimización y port a asm: §12 de
  [OPTIMIZACION_GPP_68000.md](OPTIMIZACION_GPP_68000.md).
- Estilo y restricciones de diseño: `docs/engine/architecture/CODING_STYLE.md`.