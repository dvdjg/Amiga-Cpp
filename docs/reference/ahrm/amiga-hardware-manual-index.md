# Índice del Amiga Hardware Reference Manual (3rd ed.)

Índice para consultas rápidas del manual completo. El texto completo está en:

**`doc/Amiga Hardware Reference Manual 3rd edition (Commodore Amiga Inc.) (Z-Library).cat.md`**

## Cómo usar este índice

- **Ir a una sección:** abre el `.cat.md` y salta a la línea indicada (Ctrl+G en la mayoría de editores).
- **Buscar en el manual:** usa búsqueda por texto (Ctrl+F) con los términos de la tabla "Términos de búsqueda" o los nombres de registros.
- **Referencia de registros:** Appendix A = orden alfabético; Appendix B = orden por dirección. Para un registro concreto (p. ej. `BPLCON0`), busca el nombre en el .cat.md o ve al Appendix A (~línea 10148).

---

## Capítulos (líneas aproximadas en el .cat.md)

| Capítulo | Líneas aprox. | Contenido |
|----------|----------------|-----------|
| **1. Introduction** | 1–482 | Componentes (68000, Agnus, Denise, Paula), Chip RAM, periféricos, memoria, guías de programación (no tocar hardware sin arbitraje OS, MEMF_CHIP, no delay loops, 68020/30 compat). |
| **2. Coprocessor (Copper)** | 482–1090 | Instrucciones MOVE, WAIT, SKIP; posición del haz; registros COP1LC, COP2LC, COPJMP; bucles; uso con Blitter e interlaced. |
| **3. Playfield** | 1090–2911 | Bitplanes, resolución, ventana de display (DIWSTRT/DIWSTOP), data fetch (DDFSTRT/DDFSTOP), módulo, dual playfield, HAM, EHB, scroll, color registers. |
| **4. Sprite** | 2911–3943 | Posición (SPR0POS…), forma, colores, end-of-data, reutilización de canales DMA, sprites unidos, prioridad. |
| **5. Audio** | 3943–4973 | Canales DMA, waveform, volumen, periodo (AUDxPER), modulación, calidad, tabla temperada, decibelios. |
| **6. Blitter** | 4973–8628 | DMA, minterms, máscaras, relleno, líneas; BLTCON0/BLTCON1, BLTSIZE; prioridad (BLITHOG); ejemplos ClearMem, SimpleLine. |
| **7. System Control** | 8628–9175 | Prioridad de objetos (BPLCON2, BPLCON3), colisiones (CLXDAT, CLXCON), DMACON, INTENA, interrupciones. |
| **8. Interface** | 9175–10148 | Puertos de control, teclado, audio, serie/paralelo, disco, expansión. |

---

## Apéndices (líneas aproximadas)

| Apéndice | Líneas aprox. | Contenido |
|----------|----------------|-----------|
| **A. Register Summary – Alphabetical** | 10148–11077 | Todos los registros por nombre (descripción y bits). |
| **B. Register Summary – Address Order** | 11077–11457 | Registros por dirección ($DFF000…). |
| **C. Enhanced Chip Set (ECS)** | 11457–12053 | ECS: más Chip RAM, nuevos registros, DENISEID, COPCON, etc. |
| **D. System Memory Maps** | 12053–12142 | Mapa de memoria A1000/A500/A2000 y A3000 (evitar depender de direcciones fijas). |
| **E. I/O Connectors and Interfaces** | 12142–13151 | Pines RS232, paralelo, teclado, vídeo, disco externo, SCSI (A3000), expansión 86 pines. |
| **F. 8520 CIA** | 13151–13628 | Puertos y temporizadores de las CIAs. |
| **G. Keyboard Interface** | 13628–13850 | Protocolo teclado. |
| **H. External Disk Connector** | 13850–14016 | Interfaz disco externo. |
| **I. Hardware Example Include File** | 14016–14363 | Listado de `hw_examples.i` (y referencia a `hardware/custom.i`). |
| **J. Custom Chip Pin Allocation** | 14363–14507 | Asignación de pines de los custom chips. |
| **K. Zorro Expansion Bus** | 14507–fin | Bus Zorro II/III para expansión. |

---

## Registros más usados (buscar por nombre en el .cat.md)

| Registro(s) | Uso |
|-------------|-----|
| **BPLCON0** | Control bitplanes (número de planos, DBLPF, HIRES, etc.). |
| **BPLCON1**, **BPLCON2**, **BPLCON3** | Scroll, prioridad dual playfield, EHB. |
| **BPLxPTH/BPLxPTL** | Punteros a bitplanes (x=1..6). |
| **BPL1MOD**, **BPL2MOD** | Módulo bitplanes. |
| **DIWSTRT**, **DIWSTOP** | Esquina superior izquierda e inferior derecha de la ventana de display. |
| **DDFSTRT**, **DDFSTOP** | Inicio y fin del data fetch horizontal. |
| **COLOR00–COLOR31** | Paleta (12 bit RGB). |
| **SPR0PTH/SPR0PTL** … **SPR7PTH/SPR7PTL** | Punteros a datos de sprites. |
| **SPR0POS**, **SPR0CTL**, **SPR0DATA**, **SPR0DATB** | Posición, control y datos de sprite 0 (y 1–7). |
| **AUD0LCH/AUD0LCL**, **AUD0LEN**, **AUD0PER**, **AUD0VOL**, **AUD0DAT** | Canal de audio 0 (y 1–3). |
| **DMACON** / **DMACONR** | Habilitar/deshabilitar canales DMA; DMACONR incluye BBUSY, BZERO. |
| **INTENA**, **INTREQ** | Habilitar y pedir interrupciones. |
| **BLTCON0**, **BLTCON1**, **BLTAFWM**, **BLTALWM**, **BLTxPTH/PTL**, **BLTxMOD**, **BLTSIZE** | Control y punteros del Blitter. |
| **COP1LCH/COP1LCL**, **COP2LCH/COP2LCL**, **COPJMP1**, **COPJMP2** | Copper: lista de instrucciones y saltos. |
| **CLXDAT**, **CLXCON** | Colisiones (lectura y configuración). |

---

## Términos de búsqueda útiles

Para búsqueda textual (Ctrl+F) o búsqueda semántica en el manual:

- **Chips:** Agnus, Denise, Paula, ECS, Fat Agnus.
- **Memoria:** Chip memory, Chip RAM, Fast memory, MEMF_CHIP, slow memory.
- **DMA:** DMA channel, bitplane DMA, sprite DMA, blitter DMA, audio DMA, DMACON.
- **Display:** playfield, bitplane, dual playfield, HAM, EHB, overscan, interlaced, NTSC, PAL.
- **Resolución:** low resolution, high resolution, 320, 640, 512, 256.
- **OS / arbitraje:** graphics.library, audio.device, AllocMem, exclusive access, hardware arbitration.
- **Include files:** hardware/custom.i, hardware/custom.h, hw_examples.i, dmabits.i, blit.i.
- **Dirección base:** _custom, $DFF000, chip registers.

---

## Módulos del OS que controlan el hardware (tabla del manual)

| Hardware | Módulo del sistema |
|----------|---------------------|
| Copper, Playfield, Sprite, Blitter | graphics.library |
| Audio | audio.device |
| Trackdisk | trackdisk.device, disk.resource |
| Serial | serial.device, misc.resource |
| Parallel | parallel.device, cia.resource, misc.resource |
| Gameport | input.device, gameport.device, potgo.resource |
| Keyboard | input.device, keyboard.device |
| System Control | graphics.library, exec.library (interrupts) |

---

## Matriz por chipset (OCS / ECS / AGA / CD32)

Resumen operativo: [amiga-chipset-matrix.md](amiga-chipset-matrix.md). Complemento al AHRM (principalmente OCS/ECS); para AGA fino añadir fuentes explícitas cuando estén en el repo.

## Referencia cruzada con el proyecto

- **Engine / blitter:** ver también `engine/`, `doc/engine-roadmap.md`, `doc/demoscene-effects-integration.md`.
- **Técnicas demoscene / juegos (fichas + labs):** `doc/techniques/README.md`.
- **Depuración / overlay:** `doc/debug-with-ai.md`, `doc/demoscene-effects-integration.md` (overlay WinUAE).
- **Índice general de docs:** `doc/engine-docs-index.md`.
