# Coppenheimer: interfaz y uso desde la IA

Coppenheimer es una **interfaz alternativa para vAmigaWeb**: emulador Amiga en el navegador con **monitor de memoria en tiempo real**, monitores de DMA y shell de depuración (vAmiga Retro Shell). Pensado para Amigas clásicos: OCS, 68000, 512+512 KB.

- **Demo**: [https://coppenheimer.heckmeck.de](https://coppenheimer.heckmeck.de)
- **Página del autor (funcionalidad)**: [https://heckmeck.de/amigastyle/coppenheimer/](https://heckmeck.de/amigastyle/coppenheimer/)
- **Código**: [https://github.com/losso3000/coppenheimer](https://github.com/losso3000/coppenheimer)

Basado en [vAmiga](https://dirkwhoffmann.github.io/vAmiga) (Dirk Hoffmann) y [vAmigaWeb](https://vamigaweb.github.io/doc/about.html) (mithrendal).

---

## 1. Funcionalidad de los elementos en pantalla

Descripción según la UI en [coppenheimer.heckmeck.de](https://coppenheimer.heckmeck.de) y la [página del autor](https://heckmeck.de/amigastyle/coppenheimer/).

### 1.1 Controles de ejecución (arriba)

| Elemento | Función |
|--------|--------|
| **Reset** | Reinicia la Amiga virtual (cold reset). |
| **Running / Paused** | Play/Pause de la emulación. "Running" = ejecutando; "Paused" = parada (se puede usar Step). |
| **Step** | Avanza un paso (solo cuando está Paused). |
| **Take snapshot** | Guarda un snapshot del estado actual del emulador. |
| **Snapshots…** | Abre diálogo para cargar/guardar snapshots (incl. "Load from file…", ejemplos Transhuman, 9 Fingers, Rink-A-Dink). |
| **Debugger** | Abre/cierra la ventana del **vAmiga Retro Shell** (consola de comandos del emulador). |

### 1.2 Opciones de depuración visual

| Elemento | Función |
|--------|--------|
| **Warp** | Acelera la emulación (menos realismo, más velocidad). |
| **DMA** | Muestra uso de DMA (según versión: visualización de actividad DMA). |

### 1.3 ROM y medios

| Elemento | Función |
|--------|--------|
| **Kickstart ROM…** | Abre diálogo para elegir ROM: "Kickstart" (principal), "Kickstart ext. (optional)", "Drop file or click", "Delete slot", botón **"Install AROS m68k ROMS"**. **Procedimiento por defecto**: AROS (Install AROS m68k ROMS) → arranque sin subir archivos. **Kickstart 1.3** (KICK13.rom) solo para casos muy específicos que requieran la ROM original; entonces el usuario sube la ROM manualmente. |
| **Game port 1 / Game port 2** | Selector de dispositivo: *none*, *cursor key (move) space (fire)* (teclado), *mouse*. |
| **DF0: / DF1:** | Unidades de disquete. "Drop file or click" para cargar un ADF; **Eject** para expulsar. La carga de ADF es manual (no hay MCP de subida de archivos). |

### 1.4 Opciones de pantalla y sonido

| Elemento | Función |
|--------|--------|
| **Disable Sprites** | Desactiva sprites en la salida visual. |
| **Disable playfield 1** | Desactiva el playfield 1. |
| **Disable playfield 2** | Desactiva el playfield 2. |
| **Mute** | Silencia el audio. |

### 1.5 Bloque "Coppenheimer" (información y ejemplos)

| Elemento | Función |
|--------|--------|
| **About** | Información sobre Coppenheimer. |
| **Examples** | Acceso a demos de ejemplo (snapshots precargados). |
| **Live overview (chip+fast)** | Vista en vivo de memoria chip y fast (área grande arrastrable; ver "Possible bitplane areas"). |

### 1.6 DMA usage / CPU reads and writes

Secciones expandibles (►) que muestran:

- **DMA usage**: desglose por tipo (Blitter, Copper, Disk, Audio, Sprite, Bitplane, etc.).
- **CPU reads and writes**: Chipmem reads/writes, Fastmem reads/writes, ROM reads (para analizar accesos a memoria).

### 1.7 Monitor de memoria (panel derecho)

| Elemento | Función |
|--------|--------|
| **Live monitor** | Activa el monitor en vivo del área de memoria seleccionada. |
| **Adapt to window** | Ajusta la vista al tamaño de la ventana. |
| **Amber / Beige / …** | Paleta del monitor (Amber, Beige, White on black, Black on white, Kick 1.x, Kick 2.x). |
| **Guess!** | **Bitplane guesser**: localiza posibles áreas de bitplanes en memoria (grabando accesos DMA por frame). Muy útil para ver dónde se dibuja. |
| **Address** (ej. 000000) | Dirección de inicio del bloque mostrado (hex). |
| **Width** (ej. 94) | Ancho en bytes (u otra unidad según la vista) del bloque. |
| **Chip / Fast** | Cambia entre memoria chip y fast. |
| **Reset** (junto a Chip/Fast) | Vuelve a dirección/ancho por defecto. |
| **40** (botón) | Preset de ancho (ej. 40 bytes). |

Según el autor: se puede **arrastrar** el área grande del monitor, **rueda del ratón** para cambiar el ancho, **clic en el overview** para saltar a una dirección, y **flechas arriba/abajo y Shift** en los campos de dirección/ancho.

---

## 2. vAmiga Retro Shell (Debugger)

Al pulsar **Debugger** se abre la consola **vAmiga Retro Shell** (vAmiga 2.6). Permite inspeccionar y controlar componentes del emulador por texto.

### 2.1 Cómo usar desde la IA (Cursor Browser)

- **Abrir**: clic en el botón "Debugger".
- **Escribir comando**: el campo de entrada (ref en snapshot) acepta texto; enviar con Enter.
- **Leer salida**: el área de log (textbox de solo lectura) contiene el banner y las respuestas; la IA puede leer su valor con `browser_get_input_value` en ese elemento.
- **Cerrar**: clic en "Close".

### 2.2 Comandos probados (ejemplos de salida)

#### `help`
Lista de comandos disponibles: `.`, `clear`, `close`, `help`, `source`, `regression`, `screenshot`, `amiga`, `memory`, `cpu`, `ciaa`, `ciab`, `agnus`, `blitter`, `denise`, `paula`, `rtc`, `serial`, `dmadebugger`, `monitor`, `keyboard`, `joystick`, `mouse`, `dfn`, `df0`–`df3`, `hdn`, `hd0`–`hd3`, `server`.

#### `cpu`
Muestra configuración de la CPU 68k:

```
Configuration:
            CPU revision : 68000
           DASM revision : 68000
             DASM syntax : MOIRA
            Overclocking : 0
    Register reset value : 0x00000000
```

#### `agnus`
Estado/configuración del chip Agnus (DMA, chip revision):

```
Configuration:
            Chip Revison : ECS_2MB
         Slow Ram mirror : yes
           Pointer drops : yes
```

#### `df0`
Estado de la unidad de disquete 0 (tipo, mecánica, tiempos, volúmenes):

```
Configuration:
                      Nr : 0
                    Type : DD_35
               Mechanics : A1010
  Revolutions per minute : 300
         Disk swap delay : 50400000
           Insert volume : 50
            Eject volume : 50
             Step volume : 50
             Poll volume : 0
                     Pan : 100
             Search path : ""
             Start delay : 380 msec
              Stop delay : 80 msec
        Step pulse delay : 40 usec
Reverse step pulse delay : 40 usec
    Track to track delay : 3 msec
        Head settle time : 9 msec
```

Otros comandos útiles para inspección: `memory`, `blitter`, `denise`, `paula`, `ciaa`, `ciab`, `monitor`, `amiga`. El punto `.` o **SHIFT+RETURN** entra/sale del modo debug más bajo nivel.

---

## 3. Diálogos modales (resumen)

- **Welcome / Select Kickstart ROM**: aparece al entrar sin ROM; permite elegir ROM (Kickstart y opcional ext.) o cargar snapshot. "Close" cierra el diálogo.
- **Select snapshot**: "Load from file…" para cargar un snapshot guardado.
- **Example snapshots**: Demos rápidas (Transhuman, 9 Fingers, Rink-A-Dink) que cargan snapshot sin tener que subir ROM/ADF.
- **Possible bitplane areas**: resultado del "Guess!" para posibles regiones de bitplanes; "Guess again" / "Close" según la versión.

---

## 4. Uso por la IA (Cursor IDE Browser)

| Acción | Cómo |
|--------|------|
| Abrir Coppenheimer | `browser_navigate` a `https://coppenheimer.heckmeck.de` |
| Cargar ROM sin subir archivo | Si aparece el modal de bienvenida: clic en "Kickstart ROM…" → en el diálogo "Select Kickstart ROM", clic en el botón **"Install AROS m68k ROMS"** → esperar unos segundos (descarga aros.bin y aros_ext.bin) → clic en "Close". La emulación arranca con AROS (pantalla con "ojos" azules). |
| Cerrar modales | Clic en "Close" del modal o Escape |
| Play/Pause, Reset, Step | Clic en los botones correspondientes |
| Abrir Debugger | Clic en "Debugger" |
| Enviar comando al shell | Escribir en el campo de entrada del Debugger y enviar (Enter) |
| Leer salida del shell | Leer el valor del textbox de salida (área de log) |
| Cerrar Debugger | Clic en "Close" del panel Debugger |
| Expandir DMA / CPU reads | Clic en "DMA usage" o "CPU reads and writes" |
| Cambiar Game port, paleta, etc. | Combos y botones visibles en el snapshot |

**Procedimiento estándar**: usar **Install AROS m68k ROMS** en el diálogo de Kickstart (botón al abrir "Kickstart ROM…"). Arranque sin subir archivos. **Kickstart 1.3** (KICK13.rom) solo cuando haga falta algo muy específico que requiera la ROM original; en ese caso el usuario sube la ROM manualmente (Drop file or click).

**Limitación**: la subida de archivos (ADF en DF0/DF1, o ROM propia) **no** está disponible vía MCP del Cursor Browser; el usuario debe arrastrar el archivo o usar "Drop file or click".

---

## 5. Referencias

- Coppenheimer: [coppenheimer.heckmeck.de](https://coppenheimer.heckmeck.de), [GitHub losso3000/coppenheimer](https://github.com/losso3000/coppenheimer).
- Página del autor (funcionalidad, changelog, capturas): [heckmeck.de/amigastyle/coppenheimer](https://heckmeck.de/amigastyle/coppenheimer/).
- vAmiga: [dirkwhoffmann.github.io/vAmiga](https://dirkwhoffmann.github.io/vAmiga).
- vAmigaWeb: [vamigaweb.github.io/doc/about.html](https://vamigaweb.github.io/doc/about.html).
- Integración con flujo de depuración del proyecto: [debug-with-ai.md](debug-with-ai.md) (Plan B: Coppenheimer).
