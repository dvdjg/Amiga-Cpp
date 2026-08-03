# Fuentes técnicas objetivas para desarrollo Amiga

Documento de referencia para justificar cambios de hardware/kernel en engine y tests.

## Alcance por plataforma

- **Base por defecto**: Amiga 500 + Kickstart 1.3.
- Si un cambio usa comportamiento de A600/A1200/ECS/AGA, debe declararlo en la técnica del caso.

## Fuentes primarias recomendadas

1. **Amiga Hardware Reference Manual (AHRM, 3rd ed.)**
   - Indice local: [amiga-hardware-manual-index.md](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-hardware-manual-index.md)
   - Texto local completo: [AHRM cat](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/Amiga Hardware Reference Manual 3rd edition (Commodore Amiga Inc.) (Z-Library).cat.md)
   - Uso: custom registers (`BPLCONx`, `DDF*`, `DIW*`, `BPLxPT`, `DMACON`, `INTENA`, `BLT*`, `SPR*`, `AUD*`), timing de video y DMA.

2. **RKM / NDK autodocs e includes oficiales**
   - Uso: Exec/DOS/Intuition/devices, contrato de APIs del sistema, loader y ciclo de vida de proceso.
   - Referencia local de contexto: [amiga-kernel-loader-notes.md](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-kernel-loader-notes.md)

3. **ABI y calling convention 68000**
   - Referencia local: [m68k-stack-and-calling-notes.md](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/m68k-stack-and-calling-notes.md)

## Fuentes didácticas y código de referencia (fiables)

Material **contrastable** con el AHRM/RKM: tutoriales reconocidos y repositorios de ejemplo bien documentados. Sirven para patrones, secuencias didácticas y lectura de código; **las decisiones de hardware** siguen validándose contra fuentes primarias cuando haya discrepancia.

| Recurso | URL | Notas |
|---------|-----|--------|
| **Amiga Machine Code Course** (Mark Wrobel) | [markwrobel.dk/project/amigamachinecode/](https://www.markwrobel.dk/project/amigamachinecode/) | Curso por cartas: setup, 68000, copper, DMA, sprites, blitter, scroll, audio, interrupciones, memoria, ficheros. Mapa al repo: [roadmap-amc-wrobel-engine-docs-and-debug.md](../roadmap-amc-wrobel-engine-docs-and-debug.md). |
| **PowerPrograms — Amiga** | [powerprograms.nl/amiga/amiga.html](https://www.powerprograms.nl/amiga/amiga.html) | Artículos y material sobre programación Amiga (ensamblador, sistema, gráficos según índice del sitio). |
| **Coppershade — Code / Articles** | [coppershade.org/articles/Code/Articles/](http://coppershade.org/articles/Code/Articles/) | Artículos de código y técnicas (incl. temas copper/demoscene según catálogo del sitio). |
| **alpine9000 / amiga_examples** | [github.com/alpine9000/amiga_examples](https://github.com/alpine9000/amiga_examples) | Repositorio completo como referencia: ejemplos ensamblador/C, Makefiles Amiga; **también** enlaces y lecturas citadas en el README del proyecto. |
| **cahirwpz / demoscene** | [github.com/cahirwpz/demoscene](https://github.com/cahirwpz/demoscene) | Repositorio completo: efectos y técnicas demoscene con código reproducible; útil para patrones avanzados y comparar con nuestra batería de tests. |

## Fuentes secundarias permitidas (complemento)

- Implementaciones estables (por ejemplo ACE) para contrastar fórmulas practicas de scroll/copper/blitter.
- Nunca usar una implementación de terceros como única evidencia técnica.

## Reglas de uso en tests y engine

1. Cada `docs/technique.md` debe tener sección **References** con enlaces concretos.
2. Cada técnica debe explicar:
   - registros tocados,
   - secuencia de inicialización,
   - diferencia entre lógica horizontal/vertical cuando aplique.
3. Si se corrige una implementación previa, documentar por qué la fórmula anterior era incorrecta según fuente primaria.

