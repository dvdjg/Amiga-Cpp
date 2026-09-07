# Guías de optimización

Optimización de código C++ y del chipset para las plataformas soportadas
(68000/Amiga primero). Organización canónica en `docs/STRUCTURE.md` §10.

## Documentos

| Documento | Contenido |
|-----------|-----------|
| [OPTIMIZACION_GPP_68000.md](OPTIMIZACION_GPP_68000.md) | Cómo compila g++/elf2hunk para 68000: reglas de tamaño vs velocidad, `-Os`, cuándo `-O1` engaña y warnings de `-Wextra`. |
| [_probe_gpp68000.cpp](_probe_gpp68000.cpp) | Sonda de compilación usada para medir el código generado (complementa el doc). |

## Regla permanente
- Regla de rendimiento global del repo: `AGENTS.md` («Regla permanente de
  rendimiento») y `docs/engine/architecture/CODING_STYLE.md`.