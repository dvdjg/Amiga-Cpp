# Plantilla de contrato tecnico para tecnicas low-level Amiga

Plantilla para obligar a la IA o al desarrollador a explicitar el modelo de ejecucion antes de implementar una tecnica de hardware o un caso compuesto.

Objetivo: reducir cambios a ciegas y separar claramente:

- init una sola vez
- actualizacion por frame
- trabajo del copper o DMA durante el barrido
- paralelismo real entre chips mientras avanza el tiempo de video
- ownership de superficies y politicas de persistencia
- evidencias obligatorias
- autopsia cuando falle

## Cuando usarla

Usarla antes de cualquiera de estos trabajos:

- nuevo caso de bateria
- composicion de varias tecnicas ya cerradas
- migracion de una tecnica de `tests/` a `engine/`
- depuracion de una tecnica con varios subsistemas acoplados

---

## 1. Identificacion del trabajo

- **Nombre del caso o tecnica:**
- **Objetivo visual o funcional:**
- **Maquina objetivo:** `a500` / `a600` / `a1200` / `cd32`
- **Base ROM esperada:** por defecto `Kickstart 1.3`
- **Artefacto objetivo:** `ADF/OS-loader` / `devfs` / `dos_hunk_exe` / `metal/direct`
- **Ruta prevista en el repo:**
- **Casos de referencia reutilizados:**
- **APIs del engine reutilizadas:**

## 1.1 Inventario tecnico previo

Completar antes de escribir codigo:

| Elemento necesario para la demo | Ya lo aporta el engine | Ya existe caso validado | Hay que implementarlo nuevo | Referencia elegida |
|---------------------------------|------------------------|--------------------------|-----------------------------|-------------------|
| Display base | | | | |
| Copper base | | | | |
| Paleta | | | | |
| Punteros / layout bitplane | | | | |
| `BPL1MOD/BPL2MOD` | | | | |
| Scroll / viewport | | | | |
| Sprites / BOB / overlay | | | | |
| Politica por frame | | | | |

Regla:

- si una fila ya esta resuelta por engine o por un caso estable, no reimplementarla sin justificar por que la referencia no sirve.

## 1.2 Tecnica documentada y busqueda previa

Completar antes de escribir codigo:

- **Tecnica elegida:**
- **Por que es valida para esta configuracion de bitplanes, layout y timing:**
- **Referencia local del repo usada primero:** `doc/`, `tests/amiga-battery/`, APIs del engine o caso vivo equivalente
- **Si no hay referencia local suficiente, fuentes externas autoritativas consultadas:**
- **Que parte exacta es conocimiento heredado y que parte es hipotesis nueva del caso:**

Reglas:

- no empezar a implementar hasta que esta seccion este escrita;
- buscar primero en documentacion y casos locales del repo;
- solo acudir a fuentes externas si la tecnica no esta ya documentada localmente;
- una vez validado el efecto, decidir despues si procede promocionarlo al engine como API reusable.

## 2. Alcance de la fase actual

- **Que se implementa ahora:**
- **Que no se implementa aun:**
- **Criterio exacto para dar la fase por cerrada:**

## 2.1 Modelo de estado y superficies

Completar obligatoriamente antes de escribir codigo:

- **Que estado persistente existe ya en maquina:**
- **Superficie de cada elemento visible:** `bitplanes compartidos` / `plano overlay` / `sprite hardware` / `sprite CPU` / `blitter BOB` / `copper` / `otra`
- **Anclaje de cada elemento:** `pantalla` / `mundo` / `viewport` / `mixto`
- **Quien es dueno de sus pixeles o registros:**
- **Politica de persistencia:** `redibujado por frame` / `restore-under` / `doble buffer` / `stream DMA` / `copper fija` / `copper parcheada`
- **Que ocurre si cambia scroll, viewport o texto debug:**
- **Que chips o subsistemas actuan en paralelo durante este efecto:** `CPU` / `DMA bitplanes` / `sprites hardware` / `copper` / `blitter` / `Paula` / `otros`
- **Que recurso puede ser reprogramado durante el propio frame sin detener la maquina:**
- **Que dependencia temporal critica existe entre chips:**

Si alguna de estas respuestas no es evidente y cambia la arquitectura, parar y preguntar al usuario.

## 2.2 Modelo de paralelismo del hardware

Explicar explicitamente el caso como evolucion de estado en paralelo, no como una secuencia CPU-only.

Preguntas minimas:

- **Mientras avanza el barrido, que esta leyendo Agnus/Denise por DMA?**
- **Que hace el copper mientras esos datos se estan usando?**
- **Que puede preparar o parchear la CPU sin romper el frame actual?**
- **Hay algun recurso que pueda reprogramarse varias veces en el mismo frame?**
- **El efecto depende de reutilizar hardware a mitad de pantalla o incluso varias veces por linea?**

Ejemplos que la IA debe contemplar cuando apliquen:

- sprites hardware reutilizados varias veces en pantalla;
- copper reprogramando sprites despues de que terminen de dibujarse;
- copper cambiando colores, modulos o punteros durante scanout;
- blitter actualizando memoria que luego consumira el copper;
- blitter o copper entrando en bucles de mantenimiento sin CPU continua.

Regla:

- no describir el efecto solo como "la CPU dibuja X";
- describir siempre como interaccion temporal de CPU + DMA + copper + blitter si el caso lo requiere.

## 3. Desglose temporal obligatorio

### 3.1 Init una sola vez

Describir aqui lo que ocurre solo al arrancar:

- reserva de memoria CHIP/FAST
- construccion de bitplanes, copper list o sprite streams
- configuracion inicial de `BPLCONx`, `DIW`, `DDF`, `DMACON`, `INTENA`
- configuracion inicial de `BPL1MOD/BPL2MOD` cuando aplique
- carga de paleta inicial
- punteros iniciales (`BPLxPT`, `SPRxPT`, `COP1LC`)

### 3.2 Trabajo por frame o por VBL

Describir aqui lo que se recalcula en cada frame:

- posiciones
- animacion
- scroll
- repoblado de streams DMA
- cambios de paleta por software
- sincronizacion con `WaitTOF`, `VBL` o similar
- redibujado o restauracion de HUD / overlays / texto debug
- adaptacion a viewport o camara

### 3.2.a Bucle principal por frame

Describir explicitamente si existe un bucle por frame y que responsabilidades asume.

Preguntas minimas:

- **Existe un bucle por frame?**
- **Que tareas hace cada frame aunque no cambie la logica?**
- **Que tareas solo se hacen si cambia algo?**
- **Que parte de la imagen debe mantenerse viva o recomponerse?**

Regla por defecto: si la escena es dinamica, asumir que hace falta al menos un bucle por frame salvo demostracion en contra.

### 3.3 Trabajo del copper durante el barrido

Describir aqui lo que no hace la CPU en runtime sino la copper list:

- cambios de color por linea
- waits
- cambios de modulo o scroll
- cambios de prioridades o registros visuales

Si aplica, responder ademas:

- **La lista es estatica, parcheada por CPU o doble-buffered?**
- **Que registros cambia dinamicamente mientras se pinta la pantalla?**
- **Que parte cambia una vez por frame y que parte cambia por scanline?**
- **Que riesgo hay de actualizar la lista activa a destiempo?**
- **La lista se limita a encolarse para el siguiente frame o se esta reiniciando el copper de inmediato?**

### 3.4 Recursos "vivos" que no deben tocarse sin cuidado

Listar buffers o registros sensibles:

- streams DMA consumidos por Agnus
- copper list activa
- copper list preparada para el siguiente frame pero aun no consumida
- bitplanes visibles
- punteros que avanzan por fetch DMA
- estructuras de control en RAM compartidas con el harness
- fondos sobre los que se dibujan overlays temporales
- save-under buffers si existen

### 3.5 Coste por frame y reparto de trabajo

Declarar explicitamente para toda tecnica grafica:

- **Coste aproximado CPU por frame:**
- **Que usa bus DMA y una idea del riesgo de saturacion:**
- **Que hace el blitter:**
- **Que hace el copper:**
- **Que hace la CPU:**

Notas:

- no dar por buena una tecnica solo porque produzca la imagen correcta si su coste es obviamente desproporcionado para hardware Amiga;
- no usar rasterizado interactivo pixel a pixel por CPU salvo justificacion explicita de test, debug, overlay generico o herramienta;
- un overlay/HUD CPU puede ser razonable en tests, depuracion o overlays de WinUAE si su objetivo es generalidad y bajo acoplamiento;
- para un juego real o una tecnica reusable de runtime, preferir la via acelerada o mas autentica al hardware siempre que el contexto lo pida.

## 4. Tabla de subsistemas e invariantes

| Subsistema | Fuente de verdad | Invariante esperada | Como comprobarla |
|-----------|------------------|---------------------|------------------|
| Loader / entry | | | |
| Display bitplanes | | | |
| Copper | | | |
| Sprite DMA | | | |
| CPU sprite / BOB | | | |
| HUD / overlay | | | |
| Scroll / viewport | | | |
| Blitter | | | |
| VBL / timing | | | |
| Runtime markers | | | |

Ejemplos de invariantes utiles:

- `BPLCON0` coincide con el numero de bitplanes esperados
- `BPL1MOD/BPL2MOD` coinciden con el layout de memoria esperado
- `COP1LC` apunta a una lista valida y coherente
- `SPRxPT` apunta a CHIP RAM valida
- `runtime-state.stage_id` ha pasado el umbral esperado
- `assert_failures == 0`
- el HUD no destruye permanentemente el fondo si comparte superficie
- un elemento anclado a pantalla no deriva con el scroll
- un elemento anclado al mundo si se desplaza con el viewport
- la copper activa no se parchea a mitad de uso sin politica segura
- `COP1LC` y `COPJMP1` no se confunden como si fueran la misma operacion
- un recurso reprogramado a mitad de frame respeta el tiempo de ejecucion del copper o del blitter
- si se reciclan sprites hardware, la ventana temporal entre una instancia y la siguiente esta justificada

## 5. Memoria y registros a vigilar

- **Simbolos clave del `.map` o ELF:**
- **Direcciones o bloques CHIP relevantes:**
- **Custom regs que hay que inspeccionar:**
- **Si aplica, direccion de copper list:**
- **Si aplica, direccion base de sprite stream:**
- **Si aplica, mailbox de harness / control block:**

## 6. Evidencia obligatoria para esta fase

Marca lo que debe existir antes de cerrar la fase:

- [ ] build correcto
- [ ] `runtime-state.json` o `runtime-state.md`
- [ ] `evidence-log.json` o `evidence-log.md`
- [ ] captura interna `live-screen.png`
- [ ] analisis visual `vision-*.json` y `vision-*.md`
- [ ] `custom_registers`
- [ ] `machine_snapshot`
- [ ] `copper_disassembly`
- [ ] `postmortem` si hubo stop anomalo
- [ ] evidencia temporal de secuencia si la tecnica depende del movimiento

### 6.1 Regla de aceptacion visual

Una fase grafica no puede darse por cerrada solo porque:

- `assert_failures == 0`
- `runtime-state` avance
- los registros parezcan plausibles
- el modelo de vision no detecte un error grave en una captura aislada

Para cerrar visualmente una tecnica se exige ademas:

- que la imagen sea coherente con el objetivo declarado, no solo "no rota"
- que el contenido sea legible y diagnostico, con landmarks o geometria distinguible
- que una tecnica basada en movimiento tenga evidencia temporal creible
- que la secuencia muestre cambio visual real si el efecto depende de scroll, animacion o wrap

Reglas de bloqueo:

- si la imagen es oscura, ambigua, repetitiva en exceso o no demuestra claramente la tecnica, la fase no se cierra
- si `pixel delta` o una metrica equivalente de la secuencia no detecta cambio visual real, no se puede usar `runtime_stage_detail` como sustituto para dar por buena la animacion
- si el humano observa incoherencia visual clara, prevalece esa observacion hasta rehacer la autopsia

## 7. Plan de fases recomendado

No escribir una fase grande si puede dividirse mejor. Usar esta tabla:

| Fase | Objetivo minimo | Reutiliza | Evidencia de cierre |
|------|------------------|-----------|---------------------|
| 1 | | | |
| 2 | | | |
| 3 | | | |
| 4 | | | |

Ejemplo para "8 sprites en circunferencias sobre fondo 4 bitplanes":

| Fase | Objetivo minimo | Reutiliza | Evidencia de cierre |
|------|------------------|-----------|---------------------|
| 1 | fondo 4 bitplanes estable | T01/T02 | pantalla correcta + `BPLCON0` |
| 2 | un sprite hardware visible | S01 | sprite visible + `SPRxPT` |
| 3 | ocho sprites visibles | S01 extendido | ocho sondas visibles |
| 4 | movimiento orbital | fase 3 | secuencia de frames |

## 8. Politica de fallo y autopsia

Si la fase falla, responder primero a estas preguntas:

1. El binario ha entrado realmente en su entry o primer marker?
2. El display base esta activo?
3. La copper list que creemos viva es la que realmente esta en `COP1LC`?
4. Los punteros DMA apuntan a memoria valida y estable?
5. El recurso que inspeccionamos ya fue consumido por DMA y por eso aparenta estar vacio?
6. El fallo pertenece al loader, al runtime, al display o a la validacion?
7. Estamos olvidando una politica de persistencia o restauracion por frame?
8. Confundimos un elemento anclado a pantalla con uno anclado al mundo?
9. Estamos tratando un sprite hardware, un sprite CPU y un BOB blitter como si fueran intercambiables?
10. La copper esta cambiando registros en el momento correcto del barrido?

### Prohibiciones durante la autopsia

- no reescribir multiples subsistemas a la vez
- no cambiar de artefacto (`dos_hunk_exe` a `metal`) sin documentarlo
- no concluir "no funciona" solo por una captura ambigua
- no cerrar por vision si los markers o registros contradicen la imagen
- no cerrar por markers o registros si la imagen contradice claramente el objetivo visual
- no tratar una secuencia sin cambio visual medible como prueba valida de animacion

## 9. Cierre de fase

Completar solo cuando todo esto sea cierto:

- **Fase cerrada:** si / no
- **Primera invariante satisfecha:**
- **Ultima invariante satisfecha:**
- **Evidencia principal:**
- **Riesgo residual conocido:**
- **Siguiente fase recomendada:**

---

## Uso recomendado con la IA

1. Rellenar secciones 1-4 antes de escribir codigo.
2. Implementar solo la fase actual.
3. Adjuntar evidencia real en 6.
4. Si falla, completar 8 antes de redisenar.

Complementa esta plantilla con [amiga-lowlevel-agent-prompt.md](amiga-lowlevel-agent-prompt.md).

Segun el tipo de trabajo, complementar ademas con:

- [amiga-display-setup-checklist.md](amiga-display-setup-checklist.md)
- [engine-parametric-api-and-cpp-notes.md](engine-parametric-api-and-cpp-notes.md)
- [engine-cpu-sprites-api-proposal.md](engine-cpu-sprites-api-proposal.md)
- [engine-cpu-sprites-implementation-plan.md](engine-cpu-sprites-implementation-plan.md)
- [engine-dynamic-copper-scene-notes.md](engine-dynamic-copper-scene-notes.md)
