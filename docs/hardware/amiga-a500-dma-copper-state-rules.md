# Reglas De Estado A500: DMA, Copper Y Playfields

Documento general para fijar conocimiento reusable del Amiga 500 en este proyecto.
No describe un solo caso de bateria: resume reglas operativas que deben seguir
motor, tests y futuras features de juego cuando dependan de bitplanes, copper,
sprites DMA o scroll de playfield.

## 1. Principio base

El Amiga 500 no funciona como un framebuffer moderno que la CPU "presenta"
cuando quiere. La maquina mantiene un estado visible que se construye con:

- DMA de bitplanes y sprites,
- copper ejecutando instrucciones durante el barrido,
- modulos y punteros que afectan al fetch linea a linea,
- y CPU preparando el estado del siguiente frame.

La pregunta correcta no es solo "que dibujar", sino:

- quien es duenio del dato visible,
- cuando lo lee el DMA,
- cuando cambia el copper,
- y cuando puede tocarlo la CPU sin romper el frame en curso.

## 2. Regla de residencia DMA

Si un recurso lo va a consumir DMA, debe residir en CHIP RAM.

Esto incluye:

- bitplanes (`BPLxPT`),
- streams de sprites hardware (`SPRxPT`),
- copperlists (`COP1LC`, `COP2LC`),
- audio Paula (`AUDxLC`),
- y cualquier bloque que el chipset lea directamente.

Consecuencias practicas:

- no basta con que el dato exista en el ejecutable DOS;
- no basta con que un `BitmapT` tenga dimensiones plausibles;
- no basta con que una captura puntual "parezca bien".

Antes de depurar layout, modulo o palette, hay que verificar:

- direccion de punteros activos,
- banco de memoria al que apuntan,
- y si ese banco esta realmente dentro de CHIP.

En este repo, eso debe quedar automatizado siempre que el caso marque
`requires_chip_bitplanes = true`.

## 3. Regla de ciclo de copper

Hay que distinguir entre tres acciones diferentes:

1. construir una nueva copperlist en memoria CHIP;
2. apuntar `COP1LC` a esa lista;
3. reiniciar/arrancar el copper (`COPJMP1`).

No son equivalentes.

### 3.1. Cola para el siguiente frame

En tecnicas como `layers`, la CPU:

- reconstruye la lista de back,
- actualiza `COP1LC`,
- y deja que la nueva lista entre en el siguiente barrido.

Eso es "queue for next frame".

### 3.2. Reinicio inmediato

Forzar `COPJMP1` en mitad del frame puede:

- reentrar en la lista activa,
- reiniciar el raster script en una linea inesperada,
- desplazar waits o moves,
- y crear franjas horizontales o corrupcion dependiente de fase.

Por tanto:

- `set COP1LC` es una operacion de cola;
- `COPJMP1` es una operacion de arranque/reinicio;
- no deben mezclarse en una sola API de ciclo normal.

## 4. Regla de frame para copper dinamico

Cuando una escena usa copper dinamico, el flujo correcto suele ser:

1. CPU calcula estado del siguiente frame.
2. CPU reconstruye o parchea la copperlist de back.
3. CPU apunta `COP1LC` a esa lista.
4. La maquina entra en el siguiente VBlank / siguiente barrido.
5. El copper ejecuta la nueva lista durante scanout.

No tocar la lista que el copper esta ejecutando ahora mismo.

Si la tecnica no puede garantizar eso:

- usar doble buffer de copperlist,
- o aislar la parte dinamica en bloques seguros de parcheo.

## 5. Regla de WAIT seguro

Un `WAIT` de copper no es solo "linea y columna".
Hay que tener presente:

- overflow del contador vertical por encima de `255`,
- truncado del horizontal a color clocks,
- y la semantica exacta del helper usado.

Para tecnicas con lineas visibles altas:

- usar una variante segura de `WAIT`,
- y no asumir que un helper simplificado vale para toda la pantalla PAL.

Si no se maneja bien el overflow vertical:

- cambios de `BPL1MOD/BPL2MOD`,
- paleta por banda,
- o cambios de punteros

pueden caer una linea antes o despues y verse como "basura" o franjas.

## 6. Regla de playfield dual

En dual playfield OCS:

- los planos impares forman un playfield,
- los planos pares forman el otro,
- `BPLCON1` reparte scroll fino entre ambos,
- `BPL1MOD` y `BPL2MOD` afectan a grupos alternos de planos.

Eso implica:

- no tratar `3+3` o `3+2` como "cinco bitplanes cualesquiera";
- y documentar siempre que planos pertenecen a `PF1` y a `PF2`.

Para futuros juegos, por ejemplo un shooter vertical con `3+2`:

- definir primero que capa vive en `PF1` y cual en `PF2`,
- decidir prioridad relativa con `BPLCON2`,
- documentar scroll fino/coarse de cada uno,
- y fijar como se gestionan `BPL1MOD/BPL2MOD` cuando haya wrapping o bandas.

## 7. Regla de scroll de playfield

El scroll correcto de un playfield ancho o alto se reparte en:

- scroll fino en `BPLCON1`,
- desplazamiento coarse mediante punteros `BPLxPT`,
- y, si hay wrap vertical especial, cambios temporales de `BPL1MOD/BPL2MOD`.

Errores tipicos:

- cambiar solo `BPLCON1` y olvidar el coarse,
- cambiar punteros sin dejar margen de fetch,
- reprogramar modulos en la linea incorrecta,
- o reiniciar el copper mientras la transicion esta ocurriendo.

## 8. Regla de init vs runtime

No todo debe arrancar ya "en movimiento".

Muchos originales del demoscene-repo hacen:

- init con estado base sencillo,
- primera lista estable,
- y movimiento real a partir del primer `Render()`.

Si el port local arranca ya con una orbita o con un wrap activo que el original
todavia no aplica, puede parecer un fallo de dimensiones cuando en realidad es
un fallo de secuencia temporal.

## 9. Regla de evidencia

Si una tecnica depende de fase, una imagen unica no basta.

Hay que combinar:

- captura puntual,
- muestreo temporal o secuencia,
- registros custom,
- punteros activos,
- y, cuando aplique, ring de eventos / runtime markers.

Cuando una corrupcion aparece "solo en algunos instantes", lo correcto es:

1. muestrear varias fases temporales;
2. correlacionarlas con runtime;
3. y reducir el problema a una transicion concreta.

## 10. Checklist minima antes de efectos similares

Antes de implementar otro efecto similar en A500, comprobar siempre:

1. ¿Todos los recursos DMA residen en CHIP?
2. ¿La nueva copperlist se construye en back buffer?
3. ¿Actualizar `COP1LC` y `COPJMP1` se estan tratando como operaciones distintas?
4. ¿Los `WAIT` manejan correctamente lineas >255?
5. ¿`PF1/PF2` y `BPL1MOD/BPL2MOD` estan asignados al grupo correcto de planos?
6. ¿El estado inicial replica el orden temporal del original?
7. ¿La evidencia cubre varias fases si la tecnica depende de wrap o raster?

## 11. Aplicacion al engine

Este conocimiento debe influir en:

- diseño de APIs del engine,
- skill low-level del proyecto,
- bateria de tests,
- y futuras features de juego.

Objetivo:

- que un nuevo caso no tenga que redescubrir estas reglas;
- y que una futura peticion como "shooter vertical con dual playfield 3+2 y efectos del copper"
  ya parta de un modelo correcto de estado, DMA, scroll, prioridades y ciclo de copper.
