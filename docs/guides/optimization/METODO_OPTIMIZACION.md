# Método de optimización (campaña, presupuesto de bus y A/B en caliente)

**Lectura obligatoria antes de optimizar cualquier cosa y antes de escribir código nuevo con
impacto en rendimiento** (es decir, siempre: en esta máquina el coste es parte de la
especificación). Sintetiza la campaña por fases acordada con una IA externa (consulta en
[`consulta-estrategia-optimizacion-referencia-en.md`](../../debugging/investigaciones/consulta-estrategia-optimizacion-referencia-en.md))
más las lecciones medidas en el repo ([lecciones del port del Blitter](../../debugging/investigaciones/lecciones-porte-blitter-demoscene.md)).
Complementa a [`OPTIMIZACION_GPP_68000.md`](OPTIMIZACION_GPP_68000.md) (coste por operación y
técnicas) y a [`METODOLOGIA_PROFILING.md`](METODOLOGIA_PROFILING.md) (pipeline de medida).

## 1. Principios (por qué)

1. **El coste es la especificación, no un anexo.** En OCS los chips trabajan en paralelo sobre un
   bus compartido con presupuesto por línea; un algoritmo «computacionalmente correcto» que no
   cabe en el presupuesto de bus está **mal**, aunque pase tests y se vea bien en capturas.
2. **Escribe el presupuesto antes de diseñar o medir.** Slots de Copper por línea, palabras ×
   canales del Blitter, DMA de planos/sprites/audio, accesos a Chip del CPU, y el **mapa de
   contención** (qué recurso usa cada acceso y qué más está activo en ese rango de raster).
3. **La espera es tiempo de bus.** «Esperar al Blitter» no es sobrecoste eliminable: es el tiempo
   real que el Blitter tarda bajo arbitraje (~20× su trabajo útil cuando el Copper ocupa ~45 % del
   bus). Un «build sin esperas» que mide menos está midiendo trabajo que **no se hace**.
4. **Toda hipótesis lleva coste predicho antes de medirse.** Si la medida contradice el modelo, se
   corrige el modelo; no se reinterpreta la medida anterior.
5. **La referencia es un oráculo medible**, no un texto estático: se perfilan ambos con las mismas
   herramientas antes de tocar el port.

## 2. Campaña por fases (gates: nada empieza hasta que el gate anterior está verde)

| Fase | Objetivo | Gate de salida |
|---|---|---|
| **F0 Fidelidad** | Ambos binarios producen la misma salida visible en los mismos puntos de captura determinista y tienen el mismo plan de trabajo por frame (copperlists, estructuras de sprite, lista de jobs del Blitter) | Capturas iguales + plan de trabajo emparejado; si no, parar y arreglar fidelidad |
| **F1 Baseline común** | Un número reproducible de «un update / un campo» (ciclos CPU + ocupación de bus) en **ambos** binarios, misma escena, mismo ancla de raster y misma máscara de DMA | Periodo medido en los dos con el profiler + watchpoints + contador de updates del canal lateral |
| **F2 Presupuesto diferencial** | Tabla por unidades de trabajo con columnas `ref / port / delta / confianza`; cada celda medida o de un modelo estático validado con una medida | No queda ninguna celda «creo que…» |
| **F3 Aislamiento** | Experimentos de **una sola variable** con métrica, baseline, delta esperado y criterio de falsación escritos **antes**; hot-patch primero; reversible en un comando | Cada experimento cerrado con resultado y tabla actualizada |
| **F4 Cerrar o declarar irreductible** | Cada delta > ~5 % del hueco global: cerrado o con motivo medido (arbitraje de bus, diferencia algorítmica del modelo C++, etc.) | Hueco global ≤ 15 % o imposibilidad demostrada |
| **F5 Endurecer** | Contadores publicables, entrada de bitácora y test de regresión que re-ejecuta la medida de F1 | Gate verde en regresión |

## 3. Tabla de presupuesto diferencial (el único medidor de progreso)

Filas = unidades de trabajo (columnas de sprites, staging de datos, cada grupo de BOBs, swaps de
copperlist/estructuras, bucles de espera/ancla, overhead de IRQ, CPU residual). Columnas:

| Unidad | Ref (ciclos) | Port (ciclos) | Slots de bus | Contribución medida | Modelo estático | Delta | Confianza |
|---|---|---|---|---|---|---|---|

Reglas: la columna **Slots de bus** se calcula primero (es casi gratis: MOVEs de Copper × 2,
canales de sprite, palabras del Blitter, planos); **Confianza** alta = medida directa en ambos,
media = medida en uno + modelo validado, baja = solo modelo. Una fila sin confianza alta no se
optimiza: primero se mide.

## 4. Protocolo de experimento (A/B en caliente)

Antes de ejecutar, escribir en la bitácora:

```text
Variable única:      _______________
Métrica:             periodo global + contador de sección + checksum visual de la capa afectada
Baseline:            (la medida inmediatamente anterior)
Delta esperado:      (de la fila del presupuesto)
Criterio de falsación: si el delta medido < 50 % del esperado o el checksum visual cambia → hipótesis falsa
Reversión:           poke inverso / imagen de memoria previa / binario side-by-side
```

- **Hot-patch primero** (canal lateral): pokes de registros/memoria, toggles de render
  (`render bpl|spr|blt|cop` y `cop-lines l0-l1`), vaciar/limitar la cola de blits, forzar retorno
  del handler de IRQ, anular el descriptor de un BOB concreto. Si una hipótesis no se puede probar
  con un poke o un toggle, probablemente no está lista.
- Nunca dejar un parche permanente; revertir es gratis y esperado.
- Parar en cuanto se cumple el criterio de falsación; **no reinterpretar**.

## 5. Modelar vs medir

- **Modelar primero** cuando el coste es aritmética pura de bus (MOVEs de Copper, slots de sprite
  DMA, palabras del Blitter con módulos/shift conocidos); validar el modelo con **una** captura
  del profiler.
- **Medir primero** cuando hay arbitraje, latencia de IRQ u overhead de cola.
- **Nunca** perseguir un artefacto que solo aparece en una métrica de escena completa: reducir
  siempre a una capa o a un job.

## 6. La referencia como oráculo

- Reconstruir su presupuesto desde la fuente 68k (estático) **y confirmar cada número grande por
  observación** (watchpoints filtrados por fuente + profiler).
- Comparar **unidades de trabajo**, no líneas de fuente: «¿emite los mismos 9 blits con los mismos
  tamaños/módulos?», «¿regenera las 8 columnas cada campo o cada dos?», «¿cuántas listas rehace?».
- Si la referencia cumple el objetivo (p. ej. 1 campo/iteración), esa es la primera cifra a
  explicar; lo demás es secundario.

## 7. Disciplina multi-sesión

- **Una bitácora de decisiones** por campaña (`docs/debugging/investigaciones/`), con entradas
  `fecha | hipótesis | experimento | métrica antes/después | resultado | decisión | siguiente gate`.
- Congelar el **estado de escena** (captura + volcado de copperlists/estructuras/descriptores de
  BOBs) como artefacto; es la base de los A/B deterministas.
- **No empezar una sesión re-midiendo el periodo global**: se empieza replicando el último gate
  verde.
- Tras tres sesiones sobre el mismo objetivo, la bitácora debe contener **una única tabla de
  presupuesto viva**; si no, parar de programar y reconstruirla.

## 8. Checklist (copiar al inicio de la bitácora)

```text
□ Estado de escena congelado (misma captura + mismos descriptores en ambos binarios)
□ Métrica de periodo común registrada (ref + port)
□ Cota inferior estática de bus calculada
□ Volcado del profiler por scanline tomado en ambos
□ Filas de la tabla de presupuesto pobladas (sin celdas «creo que»)
□ Siguiente experimento:
    – variable única: _______________
    – métrica: _______________
    – baseline: _______________
    – delta esperado: _______________
    – falsación: _______________
□ Resultado registrado; tabla actualizada
□ ¿Gate sigue verde? Si no → parar y rediagnosticar
```

## 9. Anti-patrones (con evidencia medida en el repo)

- **Heurísticas de escena completa como señal primaria** (correlaciones enmascaradas que aliasan
  con tramados periódicos y mezclan capas): la visión y las correlaciones son **oráculo
  secundario**, después de congelar la escena.
- **Cambios en bloque** (varias optimizaciones a la vez): impide atribuir deltas.
- **Medir solo el port**: sin el baseline de la referencia no hay delta que perseguir.
- **Reinterpretar datos ruidosos** en vez de diseñar un experimento decisivo.
- **Confundir el muro del Blitter con un problema de CPU** (o al revés): son el mismo presupuesto
  de bus.
- Casos reales y su lección: el «build sin esperas» inválido, la cola en Slow RAM a ~6k
  ciclos/encolado, la IRQ BLIT disparando en cada finalización, las tiras de BOBs invalidadas por
  el arrastre del barrel shifter — todo en
  [`lecciones-porte-blitter-demoscene.md`](../../debugging/investigaciones/lecciones-porte-blitter-demoscene.md)
  §Lección de proceso (218).
