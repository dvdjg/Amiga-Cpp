# Referencia (programación, técnicas y hardware por plataforma)

Referencia permanente por plataforma objetivo: hardware, técnicas de
programación y fuentes autoritativas. Este contenido es de consulta, no de
diseño propio.

> **Procedencia:** parte del contenido procede del repo hermano `Cursor-Amiga-C`.

## Estructura

```
reference/
├── amiga/
│   ├── hardware/      → chipset Amiga: DMA, copper, ABI 68000, invariantes, reglas
│   └── techniques/    → técnicas de programación Amiga (módulos, DPF, chunky…)
├── atarist/           → (futuro) hardware y técnicas Atari ST
├── megadrive/         → (futuro) hardware y técnicas Megadrive
├── ahrm/              → AHRM 3.ª edición (texto OCR) + índice navegable
├── amc-wrobel/        → curso Amiga Machine Code (Mark Wrobel)
└── amiga-authoritative-sources.md
```

## Documentos

| Documento | Contenido |
|-----------|-----------|
| [amiga-authoritative-sources.md](amiga-authoritative-sources.md) | Inventario de fuentes técnicas objetivas (AHRM, RKM/NDK/autodocs, ABI 68000) y reglas de uso. |
| [ahrm/](ahrm/amiga-hardware-manual-index.md) | AHRM 3.ª edición (`.cat.md`, texto OCR) + índice navegable por capítulos y registros. |
| [amc-wrobel/](amc-wrobel/README.md) | Curso Amiga Machine Code (Mark Wrobel): plan de ingesta y destino de artefactos (topic-map, gaps, crosswalk). |
| [amiga/hardware/](amiga/hardware/README.md) | Conocimiento de bajo nivel del Amiga 500: DMA, copper, ABI 68000, loader, invariantes. |
| [amiga/techniques/](amiga/techniques/README.md) | Fichas de técnicas de programación Amiga (módulos, dual playfield, copper chunky, audio, sprites…). |

## El AHRM

El manual completo en texto plano es
[ahrm/Amiga Hardware Reference Manual 3rd edition (Commodore Amiga Inc.) (Z-Library).cat.md](ahrm/Amiga%20Hardware%20Reference%20Manual%203rd%20edition%20%28Commodore%20Amiga%20Inc.%29%20%28Z-Library%29.cat.md).
Para localizar capítulos y registros usa
[ahrm/amiga-hardware-manual-index.md](ahrm/amiga-hardware-manual-index.md).

## Enlaces relacionados

- Matriz rápida de chipsets: [amiga/hardware/amiga-chipset-matrix.md](amiga/hardware/amiga-chipset-matrix.md).
- Fichas de técnicas que citan el AHRM: [amiga/techniques/](amiga/techniques/README.md).
