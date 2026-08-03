# Hardware Amiga (bajo nivel)

Conocimiento reutilizable del hardware Amiga 500 (OCS/ECS): reglas de DMA y copper,
invariantes, ABI 68000, loader/kernel, formatos binarios y de disco. Este material aplica
por igual a tests, engine y juegos, y es la base para las fichas de técnicas
([../techniques/](../techniques/README.md)).

> **Procedencia:** la mayoría de estos documentos proceden del repo hermano `Cursor-Amiga-C`
> y se incorporan aquí por tema.

## Documentos

| Documento | Contenido |
|-----------|-----------|
| [amiga-a500-dma-copper-state-rules.md](amiga-a500-dma-copper-state-rules.md) | Reglas reutilizables del A500: DMA en CHIP, ciclo de copper, WAIT seguro, dual playfield, scroll fino/coarse. |
| [amiga-hardware-invariants-microtests.md](amiga-hardware-invariants-microtests.md) | Microtests por invariante hardware (MI01-MI08). |
| [amiga-postmortems-to-rules.md](amiga-postmortems-to-rules.md) | Postmortems convertidos en reglas (assets DMA fuera de CHIP, WAIT inseguro, COP1LC vs COPJMP1). |
| [amiga-chipset-matrix.md](amiga-chipset-matrix.md) | Matriz rápida OCS/ECS/AGA/CD32 y registros a revisar primero. |
| [m68k-stack-and-calling-notes.md](m68k-stack-and-calling-notes.md) | ABI GCC/m68k: registros scratch vs callee-saved, convención de llamada, excepciones 68000. |
| [amiga-kernel-loader-notes.md](amiga-kernel-loader-notes.md) | Modelo de Exec + AmigaDOS para cargar binarios en A500/Kick 1.3. |
| [input-device-rkm.md](input-device-rkm.md) | Resumen del `input.device` según RKM: handlers, IECLASS_RAWMOUSE/RAWKEY. |

## Enlaces relacionados

- Fuente autoritativa (AHRM) e índices: [../reference/](../reference/README.md).
- Fichas de técnicas de composición: [../techniques/](../techniques/README.md).
- Formatos de artefactos y ADF: [../build/amiga-binary-and-disk-formats.md](../build/amiga-binary-and-disk-formats.md).
