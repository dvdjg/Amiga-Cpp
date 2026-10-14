# Toma de control absoluto y restauración (procedimiento canónico A500/KS1.3)

Estado: **procedimiento de referencia** (respuesta de Grok, 2026-10, contrastada con la práctica
clásica de la escena). Es lo que hacen los demos/intros "hard" que quieren el chipset completo sin
que Intuition/graphics/input.device/filesystem toquen nada, desde un proceso AmigaDOS lanzado por
`startup-sequence` (el modelo del runner).

Relación con el engine (**implementado desde 2026-10**):
- `AmigaBackend::takeover_system()` (`amiga.cpp`) implementa la entrada completa: `Forbid()` +
  `graphics.library` (`OpenLibrary`, `LoadView(NULL)`, 2×`WaitTOF`, `OwnBlitter`/`WaitBlit`) +
  guarda de `INTENA`/`DMACON`/`ADKCON` (bit15=SET).
- `AmigaBackend::restore_system()` implementa la salida completa: apaga DMA/IRQ, restaura el
  vector de nivel 3 y el copper del sistema (`GfxBase->copinit` + `COPJMP1`), restaura los
  registros guardados, `DisownBlitter`/`LoadView`(antiguo)/2×`WaitTOF`/`CloseLibrary` y
  `Permit()`.
- `takeover_display` (registros + `COPJMP1` del display propio) se llama después de
  `takeover_system`, una vez, con la primera copperlist.
- `set_vblank_sleep(false)` (variante 2 del doc 212) para la espera activa de VBlank bajo el
  runner (AmigaDOS vivo).
- Uso en demos: `backend.takeover_system()` en `init`; `backend.restore_system()` para volver al
  SO. Referencia viva: demo 066.

## 1. Entrada (orden crítico, no invertible)

```asm
; === PREPARACIÓN OS-FRIENDLY ===
move.l  4.w,a6                  ; ExecBase
jsr     _LVOForbid(a6)          ; o -132(a6)
move.l  GfxBase,a6
suba.l  a1,a1
jsr     _LVOLoadView(a6)        ; LoadView(NULL) -> mata el display de Intuition
jsr     _LVOWaitTOF(a6)
jsr     _LVOWaitTOF(a6)         ; dos veces (interlace / cambio de modo)
jsr     _LVOOwnBlitter(a6)
jsr     _LVOWaitBlit(a6)

; === GUARDAR ESTADO HARDWARE ===
lea     $dff000,a5
move.w  DMACONR(a5),d0
or.w    #$8000,d0               ; bit 15 = SET
move.w  d0,oldDMACON
move.w  INTENAR(a5),d0
or.w    #$8000,d0
move.w  d0,oldINTENA
move.w  ADKCONR(a5),d0
or.w    #$8000,d0
move.w  d0,oldADKCON
; (opcional) guardar COP1LC si quieres restaurar exactamente el copper del SO

; === APAGAR TODO ===
move.w  #$7fff,DMACON(a5)       ; bit 15=0 -> CLEAR todos los bits
move.w  #$7fff,INTENA(a5)
move.w  #$7fff,ADKCON(a5)
move.w  #$7fff,INTREQ(a5)       ; limpiar requests pendientes

; === VECTOR DE NIVEL 3 (recomendado para demo puro) ===
; En 68000 puro el VBR está en 0. En 68020+ hay que leerlo.
move.l  $6c.w,oldLevel3         ; autovector nivel 3 (VERTB+COPER+BLIT)
lea     MyLevel3(pc),a0
move.l  a0,$6c.w

; === COPPER PROPIO ===
move.l  #MyCopperList,COP1LC(a5)
move.w  d0,COPJMP1(a5)          ; cualquier valor -> arranca el copper

; === ESPERA AL VBLANK (sincronización limpia) ===
.waitvb:
    move.l  VPOSR(a5),d0
    lsr.l   #8,d0
    and.w   #$1ff,d0
    cmp.w   #311,d0             ; o 262 en NTSC
    bne.s   .waitvb
.wait0:
    move.l  VPOSR(a5),d0
    lsr.l   #8,d0
    and.w   #$1ff,d0
    bne.s   .wait0

; === ENCENDER LO QUE NECESITAS ===
move.w  #$87e0,DMACON(a5)       ; ejemplo: bitplanes + copper + blitter + sprites (ajusta)
move.w  #$c020,INTENA(a5)       ; INTEN + VERTB (ajusta)
; COPCON solo si vas a usar el copper en modo "peligroso" (bit 1 = CDANG)
; move.w  #2,COPCON(a5)
```

**Semántica bit 15 (SET/CLR)**: en `INTENA`/`DMACON`/`ADKCON`/`INTREQ`, bit 15 = 1 → SET de los
bits que valen 1; bit 15 = 0 → CLEAR. Por eso `$7fff` apaga todo y `$8000 | valor` restaura.

## 2. ¿Vector completo o AddIntServer?

| Método | Ventajas | Riesgos para demo |
|--------|----------|-------------------|
| **Reemplazar `$6c`** (autovector) | Control absoluto, cero latencia del dispatcher de la ROM, nadie más toca INTENA/Blitter | RTE propio, guardar/restaurar registros, VBR en 020+ |
| **AddIntServer** (cadena VERTB) | "Legal", el SO sigue vivo, salida limpia | El dispatcher de la ROM y otros servers (graphics) pueden reescribir INTENA y el copper; no fiable para 50 fps exactos y uso exclusivo del Blitter |

**Recomendación canónica para demo/intro: reemplazar el vector entero** (lo que ya hace el engine).
La cadena de servers solo si se quiere seguir "system-friendly".

## 3. Tareas del SO

- `Forbid()` basta para que el scheduler no cambie de tarea.
- `Disable()` (o `INTENA=$7fff`) detiene las IRQs del sistema.
- **No hace falta matar tareas** (CLI, input.device, filesystem): sin IRQs ni multitasking no
  consumen CPU ni tocan el chipset. Si se deja alguna IRQ viva (PORTS/EXTER), input.device puede
  seguir metiendo eventos: por eso se apaga todo.

## 4. Timing (50 fps)

- Con SO vivo (AddIntServer): latencia variable del dispatcher + otros servers; no se garantizan
  50 fps perfectos.
- Sin SO (vector propio): IRQ VERTB propia (efecto dentro de la IRQ o flag) o polling de VPOSR en
  el bucle principal. Con Forbid+Disable no hay jitter del scheduler; el raster es determinista.

## 5. Trampas conocidas

- **Sprite 0 del puntero**: Intuition lo deja habilitado → apagar `SPREN` (bit 5 de DMACON) o
  escribir 0 en los `SPRxPT`.
- **COPCON**: con CDANG el copper puede escribir registros "peligrosos"; restaurar a 0 al salir.
- **Libro de display de graphics.library**: `LoadView(NULL)` + 2×`WaitTOF` lo flushea; si no, el SO
  puede reescribir COP1LC en el siguiente VBlank.
- **ROM escribiendo INTENA**: en KS1.3 (p.ej. `$FCD5AC`) se borra VERTB; por eso se reemplaza el
  vector y se apaga INTENA antes (evidencia propia en
  `docs/debugging/investigaciones/212-vblank-irq-apagada-por-el-so.md`).
- **Camino Wait()/Switch de Exec**: tabla de vectores en offset negativo (`SysBase-$36`); con
  Forbid+Disable nunca se ejecuta.

## 6. Salida (restauración)

```asm
; === APAGAR TODO OTRA VEZ ===
lea     $dff000,a5
move.w  #$7fff,DMACON(a5)
move.w  #$7fff,INTENA(a5)
move.w  #$7fff,ADKCON(a5)
move.w  #$7fff,INTREQ(a5)

; === RESTAURAR VECTOR ===
move.l  oldLevel3,$6c.w

; === RESTAURAR COPPER DEL SISTEMA ===
move.l  GfxBase,a6
move.l  gb_copinit(a6),COP1LC(a5)   ; o el valor guardado
move.w  d0,COPJMP1(a5)

; === RESTAURAR REGISTROS ===
move.w  oldDMACON,DMACON(a5)
move.w  oldINTENA,INTENA(a5)
move.w  oldADKCON,ADKCON(a5)

; === DEVOLVER BLITTER Y DISPLAY ===
move.l  GfxBase,a6
jsr     _LVODisownBlitter(a6)
move.l  oldView,a1                  ; el guardado de ActiView
jsr     _LVOLoadView(a6)
jsr     _LVOWaitTOF(a6)
jsr     _LVOWaitTOF(a6)

; === REACTIVAR MULTITASKING ===
move.l  4.w,a6
jsr     _LVOPermit(a6)
```

**¿Volver al CLI/Workbench sin reset?** Sí, si se guardan/restauran registros y copper. Solo hace
falta reset si se corrompió memoria del SO o se dejaron IRQs inconsistentes.

## 7. Offsets

```
CUSTOM 0xdff000 | DMACONR 0x002 | INTENAR 0x01c | ADKCONR 0x010 | DMACON 0x096
INTENA 0x09a | INTREQ 0x09c | ADKCON 0x09e | COP1LC 0x080 | COPJMP1 0x088
VPOSR 0x004 | COPCON 0x02e
```

Filosofía canónica (resumen): Forbid + LoadView(NULL) + 2×WaitTOF + OwnBlitter → guardar
INTENA/DMACON/ADKCON (`$8000|…`) → `$7fff` a todo → reemplazar `$6c` → copper propio + espera
VBlank → encender solo lo necesario. Al salir, invertir el orden.
