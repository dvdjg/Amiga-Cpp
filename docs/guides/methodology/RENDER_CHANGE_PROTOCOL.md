# Protocolo de cambios de render

Este documento define el método obligatorio para tocar cualquier camino de render del engine: Blitter, Copper, playfields, BOBs, sprites, buffer de display, cierre de caja, beam-safety, save-under y cualquier cambio que pueda alterar píxeles o memoria DMA. No sustituye a `PROCEDIMIENTO_DEMOS_Y_JUEGOS.md` ni a `DEMO_VISUAL_DEBUG.md`; fija el orden de trabajo y los gates que deben cumplirse antes de tocar la demo grande.

## Por qué existe

El patrón de fallo repetido no es un bug concreto: es **confirmar una implementación con evidencia débil**. Primero se escribe el cambio, luego se busca una captura que parezca correcta, se prueba con un caso favorable y se da por bueno. En master y en una rama limpia se repitió el mismo patrón: una optimización de BOBs con un footprint distinto al del Blitter produjo restos, objetos parciales y «vibración» de la escena; las capturas sueltas no lo detectaron y se atribuyó a corrupción de registros.

El error de clase es:

1. Implementar antes de reproducir el fallo.
2. Validar con un caso estático o una sola imagen, no con una secuencia.
3. Mezclar varias variables (geometría, padding, stride, colisión, scheduler) en la misma iteración.
4. Confiar en nombres y fórmulas (`width`, `words`, una respuesta de IA) sin comprobar el layout físico.
5. No dejar un test que falle con el comportamiento incorrecto.
6. No tener un baseline verde al que volver.

## Orden obligatorio

### 0. Baseline verde y una sola variable

Antes de tocar nada, identificar el commit/estado verde, el binario, la configuración y los comandos de build/run/measure. El cambio debe ser una sola variable. Si se necesita corregir geometría y scheduler, se hacen dos cambios separados con su propio gate y su propio revert.

### 1. Ficha de invariantes antes del código

Escribir en la nota de trabajo, antes de editar:

- espacio de coordenadas (pantalla, bitmap, anillo, mundo);
- unidades (píxeles, palabras, bytes, líneas);
- layout físico (planar/interleaved, paso de fila, paso de plano);
- huella de escritura real (incluida guarda y padding);
- límites de la reserva y canarios;
- relación con el haz (caja superior/inferior, deadline, atomicidad);
- qué estado debe sobrevivir si se difiere.

Si una fórmula no se puede derivar del layout, no se implementa: se contrasta con la fuente de hardware o con una sonda.

### 2. Sonda mínima que falle primero

Ningún cambio de render empieza sin una sonda mínima, determinista y reproducible que **falle con el código actual**. La sonda debe incluir:

- un caso negativo que reproduzca el síntoma;
- un caso positivo de control conocido;
- canarios antes y después de cada reserva;
- borde, desplazamiento fino y vecino cuando el fallo dependa de la huella;
- un `detail`/assert que distinga fallo de éxito.

La sonda debe ser lo más pequeña posible: un BOB, una posición, un frame; después dos BOBs; después movimiento y vecino. No se depura sobre la demo completa. Si la sonda no reproduce el fallo, se simplifica más; no se vuelve a la escena grande.

### 3. Contrato y única fuente de verdad

El API debe hacer difícil el estado incorrecto:

- una sola función calcula la huella real (`bob_footprint`, bounds, rows, bytes);
- el productor no duplica el cálculo de palabras, guardas, módulos o cajas;
- el setup rechaza hojas sin el padding requerido para BOBs opacos no alineados;
- colisión, beam, save-under y límites usan la misma huella;
- el tipo distingue memoria Chip/DMA y no acepta punteros crudos;
- un helper nuevo de Blitter nace con test host de geometría y test on-target.

Si el API permite que el llamador invente la geometría, el API está incompleto aunque compile.

### 4. Validación por capas

El orden es obligatorio y no se salta ninguna capa:

1. **Host**: geometría, módulos, palabras, huella, límites y casos de borde.
2. **On-target estático**: un BOB, una posición, canarios y píxeles esperados.
3. **On-target desplazado**: `x & 15 != 0`, borde derecho/inferior y guarda.
4. **On-target con vecino**: dos o más BOBs que puedan solaparse o borrarse.
5. **Secuencia**: varios frames consecutivos, objetos completos o ausentes, sin restos ni barras.
6. **Rendimiento**: `measure-fps`, campos/frame y estabilidad; sin picos ni cuantización.
7. **Flicker/visión**: `flicker-check` y revisión visual; Ollama es una capa secundaria, nunca el juez principal.

Una capa no cubierta se declara **SIN VERIFICAR**; no se extrapola del caso favorable.

### 5. Evidencia válida

Para render no vale una captura única. La evidencia mínima es:

- secuencia de frames con frame-step;
- conteo/bbox de componentes y diff entre frames;
- canarios y lectura de memoria/registros en el momento correcto;
- comparación A/B contra el comportamiento anterior;
- medidas de fps/cadencia y, si aplica, perfil de ciclos.

Las lecturas de registros de display deben hacerse en VBlank o desde la copperlist; los registros `BPLxPT` vivos son punteros DMA corrientes y no la base estable. Un watchpoint sin impacto no es prueba de que no haya corrupción: hay que mirar la fuente y el estado en la fase correcta.

### 6. A/B, bisección y regla de parada

Cada iteración cambia una sola cosa y se compara con el baseline. Si la sonda no se arregla en 3–4 intentos, se para, se documenta lo observado y se pide decisión; no se sigue parcheando la demo. Si un cambio verde se rompe, se revierte al último estado verde y se biseca; no se acumulan parches.

### 7. Portar desde master

No se porta un bloque «porque está en master». Cada mecanismo se mapea a:

- el fallo que ya se reprodujo en la sonda;
- el invariante que lo protege;
- el test host/on-target que lo cubre;
- el gate de fps/flicker.

Si no hay fallo reproducible ni test, el port no entra. Los cambios de master se revisan línea a línea y se separan de las correcciones locales.

### 8. IA externa

Las respuestas de Grok u otro modelo son **hipótesis**, no especificación. Toda fórmula se contrasta con el layout real del engine/hardware y con un test. Si una respuesta contiene una fórmula dudosa, se corrige en un `follow-up` y se deja constancia; no se edita en silencio una consulta ya enviada.

### 9. Definition of Done de un cambio de render

Un cambio solo está terminado cuando:

- existe una sonda que fallaba antes y pasa después;
- hay test host de geometría y test on-target de memoria/display cuando aplique;
- la secuencia no muestra objetos parciales, restos, barras ni saltos;
- canarios y límites están limpios;
- fps/cadencia cumplen el presupuesto y son estables;
- la documentación de la demo y la referencia del engine están actualizadas;
- el commit explica el invariante y cita el test, no solo el síntoma.

Gate automatico: `bash tools/check/render-gates.sh` (HOST-504 + L1-007).

El checklist operativo de commit/PR esta en [PR_CHECKLIST.md](PR_CHECKLIST.md).

## Antipatrones prohibidos

- «Compila y se ve bien en una captura» como criterio de aceptación.
- Añadir un helper de Blitter sin test y usarlo directamente en la demo.
- Calcular palabras, guardas o módulos en la demo.
- Mezclar padding, stride, colisión y scheduler en un mismo commit.
- Usar una respuesta de IA como especificación sin test.
- Leer `BPLxPT` en display activo y concluir que la base está corrupta.
- Tocar la demo grande para depurar un fallo de geometría.
- Seguir iterando después de 3–4 fallos sin cambiar de hipótesis.

## Checklist rápido

```text
[ ] baseline verde identificado
[ ] invariante escrito (espacio, unidades, layout, huella, límites, haz)
[ ] sonda mínima falla antes del cambio
[ ] test host de geometría
[ ] test on-target estático y desplazado
[ ] test con vecino
[ ] secuencia frame-step sin restos/parciales
[ ] canarios y límites limpios
[ ] fps/cadencia estables
[ ] documentación y commit con invariante y test
```
