# Prompt maestro para IA low-level Amiga

Prompt reutilizable para pedir a una IA trabajo **close-to-the-metal** en este repo sin caer en implementaciones "big bang" ni iteraciones a ciegas.

Usarlo cuando la tarea incluya una o varias de estas piezas:

- bitplanes y display custom
- copper
- blitter
- sprites hardware
- IRQ/VBL
- loader DOS vs payload metal
- validacion visual o postmortem en WinUAE

## Idea central

La IA no debe tratar una tecnica Amiga como "codigo grafico normal". Debe trabajar como:

1. planificador por fases verificables;
2. integrador de tecnicas ya demostradas;
3. verificador de invariantes de hardware;
4. forense de fallos antes de reescribir.

Ademas, debe asumir que la maquina tiene **estado persistente** y que cada elemento visible necesita una politica explicita de:

- ownership de pixeles o registros;
- anchura temporal (init, por frame, durante barrido);
- mantenimiento frente a scroll, redibujado o restauracion;
- separacion entre CPU, DMA y copper.
- modelo de paralelismo real entre chips mientras el frame se esta llevando al CRT.
- tecnica documentada valida para la configuracion real de bitplanes, layout y timing antes de escribir codigo.
- una optimizacion minima compatible con hardware retro, evitando soluciones visualmente correctas pero absurdamente caras.

## Prompt listo para copiar

```text
Trabaja en este repo como desarrollador low-level Amiga con foco en A500 + Kickstart 1.3 salvo que se indique lo contrario.

Objetivo:
[DESCRIBIR AQUI LA TECNICA O CASO]

Contexto obligatorio:
- No hagas una implementacion big bang.
- Divide el trabajo en fases verificables y no des una fase por cerrada sin evidencia.
- Reutiliza primero casos de bateria y APIs del engine ya demostrados antes de escribir inicializacion cruda nueva.
- Si la tarea compone varias tecnicas, separa claramente que parte viene de cada referencia estable.

Referencias base del repo:
- doc/amiga-test-battery-spec.md
- doc/mcp-live-coding-workflow.md
- doc/amiga-lowlevel-technique-contract-template.md
- [AQUI ANADE CASOS CONCRETOS, por ejemplo T01/T02/S01/C01/B01]

Modo de trabajo obligatorio:
1. Antes de escribir codigo, haz un inventario tecnico:
   - lista todos los elementos necesarios para que la demo funcione
   - indica si cada elemento ya lo aporta el engine
   - indica si ya existe un caso de bateria validado que lo demuestre
   - separa claramente que parte es base reutilizada y que parte es la hipotesis nueva del trabajo
   - si existe un caso base vivo para display, copper o sprites, hereda su contrato antes de tocar nada
   - identifica la tecnica concreta que vas a usar y donde esta documentada
   - busca primero en la documentacion local y en casos de bateria vivos del repo
   - solo si no existe una tecnica local valida, busca fuentes externas autoritativas antes de implementar
   - no empieces a programar hasta dejar por escrito por que esa tecnica es valida para la configuracion de bitplanes y layout del caso
2. Despues redacta un contrato tecnico breve:
   - artefacto objetivo: dos_hunk_exe, ADF/OS-loader, devfs o metal/direct
   - maquina objetivo
   - tecnica base reutilizada
   - superficie de cada elemento visible: bitplanes, overlay, sprite DMA, blitter BOB, copper, etc.
   - si cada elemento esta anclado a pantalla o al mundo
   - quien es dueno de sus pixeles o registros
   - init una sola vez
   - actualizacion por frame/VBL
   - trabajo del copper durante el barrido
   - politica de persistencia: redibujado, restore-under, doble buffer, capa fija o stream DMA
   - registros o buffers que no deben tocarse fuera de su fase
   - coste aproximado CPU por frame
   - que usa bus DMA y una idea del riesgo de saturacion
   - que hace el blitter
   - que hace el copper
   - que hace la CPU
   - como interactuan en paralelo CPU, DMA, copper, sprites y blitter mientras el barrido sigue avanzando
3. Implementa solo la fase actual.
4. Tras cada fase, verifica con la mejor evidencia disponible:
   - build
   - runtime-state / evidence-log
   - custom regs relevantes
   - screenshot interna
   - vision LM Studio
   - postmortem si hay crash
5. Si falla, no rehagas el sistema entero:
   - primero captura autopsia
   - localiza la primera invariante rota
   - corrige solo el subsistema responsable
6. No mezcles sin justificar:
   - loader DOS con expectativas metal
   - inicializacion de display con actualizacion por frame
   - datos DMA vivos con buffers aun no estabilizados
   - HUD o debug overlay con world rendering sin declarar ownership y restauracion
   - hardware sprites, CPU sprites y blitter BOBs como si fueran la misma tecnica
   - copper fijo y copper dinamico por scanline como si tuvieran el mismo coste o riesgos

Formato de entrega esperado:
- Fases
- Contrato tecnico resumido
- Cambios implementados
- Evidencia recogida
- Si algo falla, diagnostico por invariantes rotas y siguiente correccion concreta

Criterios de disciplina:
- No cierres una fase por intuicion visual solamente si faltan registros o runtime markers.
- No des por "basico" un caso que combine varios subsistemas Amiga.
- Cuando compongas tecnicas, parte de referencias vivas del repo y cita exactamente cuales.
- Ante duda de timing, explica que ocurre en init, en VBL y durante el barrido.
- Asume por defecto que una escena dinamica necesita al menos un bucle por frame para mantener coherencia visual, salvo que demuestres lo contrario.
- Si aparece un HUD, overlay, texto debug, scroll o viewport movil, explica como se conserva o se recompone en cada frame.
- Si hay copper, describe con precision que registros cambian, en que tramo del barrido y si esos cambios son estaticos o se regeneran por frame.
- Si el efecto depende de reutilizar hardware en el mismo frame, explica si se reprograman sprites, punteros, colores o listas y por que el tiempo disponible alcanza.
- Si el efecto usa doble buffer de copperlist, distingue explicitamente "actualizar COP1LC para el siguiente frame" de "forzar COPJMP1 ahora mismo".
- Si el caso usa display interleaved, declara expresamente como heredas o calculas `BPL1MOD/BPL2MOD`.
- Si el resultado visual parece ruido o rayado absurdo, sospecha primero del contrato base de display antes de reescribir la tecnica nueva.
- No aceptes una tecnica grafica solo porque "se ve bien" si su coste es obviamente desproporcionado para hardware Amiga.
- No rasterices graficos interactivos pixel a pixel por CPU salvo justificacion explicita de test, debug u overlay generico temporal.
- Si el objetivo real menciona scroll hardware, blitter, copper o DMA, no sustituyas silenciosamente esa tecnica por una aproximacion ingenua de CPU sin advertirlo de forma explicita.
- Si no esta claro donde debe vivir un elemento visual, para y pregunta al usuario antes de implementar.
```

## Como adaptarlo segun el tipo de tarea

### 1. Tecnica base aislada

Usar un solo caso de referencia y pedir una sola responsabilidad visual:

- `T01` o `T02` para display
- `C01` para copper simple
- `B01` o `B02` para blitter
- `S01` para sprite DMA

### 2. Caso compuesto

Pedir composicion explicita, nunca "desde cero":

- base display: `T01` o `T02`
- capa sprite: `S01`
- animacion o bandas: `C01` o `V03`

Ejemplo correcto:

```text
Implementa un caso compuesto a partir de T01_lores_16c + S01_hardware_sprite.
Fase 1: fondo 4 bitplanes estable.
Fase 2: un sprite hardware visible.
Fase 3: ocho sprites hardware visibles.
Fase 4: movimiento orbital.
No pases de fase sin evidencia.
```

### 3. Debug de un fallo

Pedirle a la IA que diagnostique por invariantes, no que "pruebe otra version":

```text
No reescribas aun. Captura build, runtime-state, evidence-log, custom regs, screenshot y postmortem.
Identifica la primera invariante rota entre: display activo, copper vivo, DMA sprite, punteros CHIP validos, stage alcanzado.
```

### 4. HUD, overlays, scroll y elementos persistentes

Si la tarea incluye UI, debug o composicion visual persistente, anadir:

```text
Antes de implementar, explica:
- si el elemento vive en bitplanes compartidos, plano dedicado, sprite hardware, sprite CPU, blitter BOB o copper
- si esta anclado a pantalla o al mundo
- quien restaura el contenido previo si comparte superficie con el mundo
- que se recalcula o redibuja en cada frame
- como afecta el scroll o cambio de viewport

No escribas codigo hasta declarar la politica de ownership y persistencia.
```

### 5. Copper dinamico

Si la tarea toca copper, anadir:

```text
Describe con detalle:
- que registros cambia la copper list
- si cambian una vez por frame o durante el barrido
- si la lista es estatica, parcheada por CPU o doble-buffered
- que invariantes deben mantenerse para no corromper la imagen a mitad de frame
```

## Anti-patrones que este prompt evita

- pedir "una demo entera" como si fuera una sola tecnica
- tratar un `dos_hunk_exe` como payload metal
- modificar display, copper y sprites a la vez sin baseline intermedio
- validar solo con vision cuando el fallo es de runtime o loader
- reescribir completo tras un crash sin autopsia previa

## Relacion con otros docs

- El contrato que el prompt exige se rellena con [amiga-lowlevel-technique-contract-template.md](amiga-lowlevel-technique-contract-template.md).
- El inventario previo y la validacion del setup de display se apoyan en [amiga-display-setup-checklist.md](amiga-display-setup-checklist.md).
- El flujo de evidencia y depuracion real se apoya en [mcp-live-coding-workflow.md](mcp-live-coding-workflow.md) y [amiga-test-battery-spec.md](amiga-test-battery-spec.md).
- Para roles y supervision multiagente, ver [agent-system-roadmap.md](agent-system-roadmap.md).
- Para APIs parametricas y capa retained, ver [engine-parametric-api-and-cpp-notes.md](engine-parametric-api-and-cpp-notes.md) y [engine-cpu-sprites-api-proposal.md](engine-cpu-sprites-api-proposal.md).
- Para cobre dinamico dependiente de escena, ver [engine-dynamic-copper-scene-notes.md](engine-dynamic-copper-scene-notes.md).
