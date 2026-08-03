# DX39 / Layers - Analisis Tecnico Del Original

Documento de trabajo para dejar por escrito, paso por paso, que hace el efecto `layers` del repositorio `demoscene-repo` y con que proposito. La meta no es solo "portarlo", sino entender bien:

- como monta el dual playfield,
- como sincroniza la copperlist,
- que hace una sola vez y que hace cada frame,
- y que piezas merecen promocion al engine.

Referencias base:

- [Tutorial 39-layers](C:/Users/dvdjg/Documents/programa/AI/Amiga-C++/demoscene-repo/docs/tutoriales/39-layers.md)
- [Codigo origen layers.c](C:/Users/dvdjg/Documents/programa/AI/Amiga-C++/demoscene-repo/effects/layers/layers.c)
- [Contrato tecnico Amiga](C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-lowlevel-technique-contract-template.md)
- [HRM: Activating Dual-Playfield Mode](https://amigadev.elowar.com/read/ADCD_2.1/Hardware_Manual_guide/node007C.html)
- [HRM: Dual-Playfield Priority and Control](https://amigadev.elowar.com/read/ADCD_2.1/Hardware_Manual_guide/node007B.html)
- [HRM: Specifying Amount of Delay (BPLCON1)](https://amigadev.elowar.com/read/ADCD_2.1/Hardware_Manual_guide/node008B.html)

## 1. Que efecto busca el original

El original no es "dos imagenes puestas juntas". Busca una escena compuesta con:

- `PF1` y `PF2` visibles a la vez en modo `dual playfield`,
- scroll omnidireccional independiente para cada playfield,
- paleta propia por playfield,
- gradientes por raster actualizados por copper,
- y wrapping vertical controlado con cambios de `BPL1MOD/BPL2MOD`.

Visualmente, el resultado esperado es una composicion de formas organicas grandes, con rellenos de color y contornos negros, no una pantalla de puntos sueltos o rayas.

## 2. Assets y layout real

El efecto usa cuatro recursos:

- `background.c`
- `foreground.c`
- `bg-gradient.c`
- `fg-gradient.c`

Las dos imagenes principales tienen este contrato:

- `384x384`
- `3 bitplanes` por imagen
- `48 bytes por fila`
- `18432 bytes por plano`

Eso significa que el efecto no dibuja nada por CPU en caliente sobre los bitplanes. La escena ya existe en memoria CHIP y el movimiento sale de cambiar:

- punteros `BPLxPT`
- scroll fino en `BPLCON1`
- y modulos `BPL1MOD/BPL2MOD`

## 3. Init del original, paso por paso

En `Init()` el original hace exactamente esta secuencia conceptual:

1. `SetupDisplayWindow(MODE_LORES, X(0), Y(0), WIDTH, HEIGHT)`
2. `SetupBitplaneFetch(MODE_LORES, X(-16), WIDTH + 16)`
3. `SetupMode(MODE_DUALPF, DEPTH)`
4. Reserva dos copperlists
5. Construye la primera lista con `MakeCopperList()`
6. Activa la copperlist
7. Activa DMA raster

### 3.1 Que persigue cada llamada

`SetupDisplayWindow`

- fija `DIWSTRT/DIWSTOP`
- define donde aparece la ventana visible PAL en coordenadas lowres

`SetupBitplaneFetch`

- fija `DDFSTRT/DDFSTOP`
- fija el valor base de `BPLCON1`
- deja margen horizontal adicional para que el scroll fino y los offsets de palabra tengan datos validos

`SetupMode(MODE_DUALPF, DEPTH)`

- fija `BPLCON0` con `DBLPF` y `6 bitplanes`
- fija prioridad base de playfields/sprites en `BPLCON2`
- fija `BPLCON3` compatible con ese modo

## 4. SetupLayers del original

`SetupLayers()` prepara el estado horizontal/vertical instantaneo de ambos playfields.

Hace cuatro cosas:

1. Calcula scroll fino:
   - `fg_sh = 15 - (fg_x & 15)`
   - `bg_sh = 15 - (bg_x & 15)`

2. Calcula offset coarse en memoria:
   - `offset = y * bytesPerRow + ((x & -16) / 8)`

3. Programa punteros de bitplane:
   - planos pares -> `background`
   - planos impares -> `foreground`

4. Programa:
   - `BPL1MOD = bg_bplmod`
   - `BPL2MOD = fg_bplmod`
   - `BPLCON1 = (fg_sh << 4) | bg_sh`

### 4.1 Idea de hardware detras

En dual playfield OCS:

- los planos `1,3,5` forman un playfield
- los planos `2,4,6` forman el otro
- `BPLCON1` guarda el scroll fino de cada uno
- y `BPL1MOD/BPL2MOD` controlan el salto al final de cada linea para los planos alternos

Esto coincide con el HRM:

- `DBLPF` en `BPLCON0` agrupa planos impares y pares en playfields separados;
- `BPLCON1` usa bits `3-0` para `PF1` y `7-4` para `PF2`;
- cuando compartes `DDFSTRT/DDFSTOP` entre dos playfields con scroll independiente, hay que sobrefetchear y compensar punteros/modulos del playfield que no se desplaza.

Aqui esta una de las claves del efecto: no hay framebuffer "recompuesto". La composicion sale del fetch DMA real.

## 5. SetupRaster del original

`SetupRaster()` es la parte mas delicada. No mueve los bitplanes. Lo que hace es:

- cargar colores base de ambos gradientes,
- detectar si una capa va a sobrepasar el final vertical del bitmap,
- insertar `WAIT`s de copper en lineas concretas,
- cambiar `BPL1MOD/BPL2MOD` justo cuando hace falta envolver,
- y cambiar colores cada 8 lineas aproximadamente.

### 5.1 Proposito de los cambios de modulo

Si el viewport vertical va a salir por abajo del bitmap:

- durante casi toda la pantalla el modulo es el normal
- justo antes de llegar al final del bitmap, la copper cambia temporalmente el modulo
- eso hace que el DMA "salte" al principio de la imagen
- en la siguiente linea critica, la copper restaura el modulo normal

Es decir: el wrapping vertical no se hace por CPU redibujando, sino por un ajuste de modulo sincronizado con el barrido.

### 5.2 Proposito de los cambios de color

Cada gradiente esta organizado por bandas.

La copper:

- espera a una linea concreta
- cambia `COLOR01..06` para `PF1`
- cambia `COLOR09..13` para `PF2`

Asi se obtiene un gradiente vertical aparente sin tocar el bitmap.

## 6. Render del original

Cada frame, `Render()` hace:

1. calcula `bg_x/bg_y/fg_x/fg_y` con seno/coseno
2. reconstruye la copperlist "back"
3. activa esa lista
4. espera VBlank
5. cambia el buffer activo

### 6.1 Que implica eso sobre sincronizacion

La CPU nunca debe parchear "a lo loco" la lista que el copper esta ejecutando.

El original evita eso usando:

- doble buffer de copperlist
- reconstruccion completa del buffer de back
- intercambio una vez por frame

Por eso esta tecnica es un buen ejemplo de "copper dinamico mantenido", no de copper estatica.

## 7. Registros relevantes del original

Contrato base esperable:

- `BPLCON0 = 0x6600`
- `BPLCON2 = 0x0024`
- `BPLCON3 = 0x0C00`
- `DDFSTRT = 0x0030`
- `DDFSTOP = 0x00D0`
- `DIWSTRT = 0x2C81`
- `DIWSTOP = 0x2CC1`

Contrato dinamico:

- `BPLCON1` cambia con el scroll fino de ambos playfields
- `BPL1MOD/BPL2MOD` pueden cambiar en lineas concretas
- `COLOR01..06` y `COLOR09..13` cambian por bandas
- `BPL1PT..BPL6PT` cambian cada frame

## 8. Desglose por responsabilidades

### CPU init

- configura display base
- reserva copperlists
- construye la primera lista

### CPU por frame

- calcula posiciones
- reconstruye copperlist back
- intercambia listas

### Copper durante scanout

- aplica colores por banda
- cambia modulos en lineas de wrap

### DMA raster

- lee 6 bitplanes
- mezcla ambos playfields segun la logica dual playfield

## 9. Riesgos al portarlo

Los fallos tipicos al recrearlo son:

1. Programar mal `BPLCON2/BPLCON3`
2. Usar un contrato de `DDFSTRT/DDFSTOP` distinto del original
3. Calcular mal el scroll fino o coarse
4. Escribir los punteros de plano en el orden equivocado
5. Modificar la copper activa en vez de la de back
6. Capturar una fase del scroll poco representativa y creer que la tecnica esta rota o correcta

## 10. Plan de recreacion local por fases

Para dejar esta tecnica bajo control en Cursor-Amiga-C, la estrategia correcta es:

### Fase 1 - Playfields estaticos

Objetivo:

- mismo contrato base de display que el original
- mismos assets
- punteros estaticos validos
- paleta estatica valida

Cierre:

- ambos playfields visibles a la vez
- siluetas organicas reconocibles
- registros base correctos

### Fase 2 - Scroll sin raster dinamico

Objetivo:

- mover `BPLxPT` y `BPLCON1`
- sin cambios de color por raster
- sin wrap por modulo dinamico

Cierre:
- scroll visible y estable
- dual playfield correcto durante el movimiento

## 11. Estado local y hallazgos actuales

### `DX39P0`

- Caso sintetico `3+3` dual playfield.
- Valida que la ruta reusable del engine para `DIW/DDF/BPLCONx/BPLxMOD/BPLxPT` funciona.
- Conclusion: la base del engine no es el bloqueo.

### `DX39P1`

- Usa los assets importados reales en dual playfield estatico.
- Mantiene registros base correctos, pero la imagen sigue rota.

### `DX39P2` y `DX39P3`

- Muestran `background` y `foreground` por separado en `3bpl` simple.
- Primera observacion: ambos salian mal visualmente cuando se apuntaba directamente a los arrays del ejecutable DOS.
- Verificacion adicional: los hashes SHA-256 de `background.c` y `foreground.c` coinciden exactamente con los del repositorio origen.
- Causa raiz confirmada: el problema no era el layout planar, sino la residencia en memoria.
  Los arrays importados estaban siendo usados desde el segmento del ejecutable, no desde memoria CHIP, asi que el DMA de bitplanes leia datos no validos para pantalla.
- Confirmacion:
  - al clonar `_background_bpl` y `_foreground_bpl` a memoria CHIP, `DX39P2` y `DX39P3` pasan a verse correctamente;
  - y `DX39P1` vuelve a producir la composicion estatica reconocible.
- Conclusion afinada: el bloqueo no era dual playfield ni la copperlist base, sino no haber promovido explicitamente los assets DMA a CHIP.

### `DX39P4`

- Reintroduce scroll por frame sobre la base ya sana de `DX39P1`.
- Mantiene fuera del alcance:
  - gradientes por raster,
  - wrap vertical con cambios dinamicos de `BPL1MOD/BPL2MOD`.
- Valida:
  - assets en CHIP,
  - calculo CPU de offsets,
  - `BPLCON1` dual,
  - reconstruccion de copperlist back,
  - instalacion por doble buffer una vez por frame.
- Conclusion afinada: ya existe una fase segura que demuestra scroll dual sin reabrir todavia el problema del raster dinamico.

### `DX39P5`

- Parte de `DX39P4`, pero reintroduce solo el wrap vertical por modulo.
- La copper inserta `WAIT + MOVE BPL1MOD/BPL2MOD` en las lineas de cruce y restaura despues el modulo normal.
- Sigue sin introducir gradientes por banda, asi que el unico ingrediente nuevo es el wrapping vertical sincronizado con raster.
- Hallazgo importante del port local:
  un helper de `WAIT` simplificado, valido en casos bajos, no era suficiente aqui.
  Cuando la linea visible supera `255`, el copper necesita la secuencia segura de overflow vertical antes del `WAIT` real; si no, los cambios de `BPL1MOD/BPL2MOD` caen en lineas equivocadas y la pantalla muestra basura o patrones corruptos.
- Conclusion afinada: ya estan validados por separado:
  - contrato base dual playfield,
  - scroll por frame,
  - y wrap vertical por modulo.
  Lo pendiente para acercarse al original ya queda acotado a color por raster y composicion final.

### `DX39P6`

- Parte de `DX39P5`, pero reintroduce ya el color por raster del original.
- La copper cambia `COLOR01..06` y `COLOR09..13` durante todo el barrido, tomando `bg-gradient.c` y `fg-gradient.c` como filas base. En nuestro port actual, la rampa se suaviza linea a linea y se cuantiza con difusion de error vertical para repartir mejor los saltos de RGB12.
- Mantiene el mismo scroll orbital, el mismo wrap vertical y la misma disciplina de doble buffer de copperlist.
- Conclusion afinada: con `DX39P6` ya quedan aislados y validados todos los ingredientes grandes del original:
  - dual playfield base,
  - scroll dual por frame,
  - wrap vertical por `BPL1MOD/BPL2MOD`,
  - y gradientes por banda durante el barrido.
  A partir de aqui `DX39_layers_dualpf` puede verse como recomposicion del efecto completo, no como campo de pruebas artesanal.

## 12. Precauciones aprendidas

1. No depurar un efecto compuesto antes de aislar sus assets y contratos base.
2. No lanzar casos WinUAE de este tipo en paralelo para comparar evidencia.
   Una tanda anterior contamino `DX39P0` y `DX39P1`; desde entonces las validaciones buenas se hacen en serie, con limpieza previa de `evidence/` y cierre forzado de WinUAE.
3. No dar por hecho que un asset importado es correcto solo porque su struct `BitmapT` tenga medidas plausibles.
   Antes de culpar al copper o al dual playfield, hay que comprobar tambien si el asset reside en memoria CHIP y, si hace falta, probarlo en playfield unico o decodificarlo offline.
4. Antes de implementar otra fase, dejar escrita la hipotesis exacta bajo prueba.
   Aqui la hipotesis correcta era "dual playfield base", no "todo el efecto layers".
5. En imports desde repositorios externos, no confiar en macros como `__data_chip` si se redefinen localmente.
   Si el dato va a ser leido por DMA, hay que garantizar su residencia en CHIP de forma explicita.
6. En casos DMA, registrar desde evidencia si los `BPLxPT` activos caen o no dentro del banco CHIP.
   Ese guardrail ya merece formar parte del pipeline para no volver a perder tiempo culpando antes al copper o al layout.
7. No paralelizar `build` y `create-adf` cuando el segundo depende del `.exe` recien generado.
   En estas fases de importacion low-level es mejor mantener la cadena `build -> adf -> evidencia` totalmente secuencial.
8. No reutilizar helpers de `WAIT` del copper sin comprobar su rango real de lineas.
   En tecnicas con raster dinamico dentro de una ventana PAL completa, hay que usar siempre la variante segura para `vp_line > 255`; de lo contrario, el bug aparece como "basura visual" aunque el resto de registros base sea correcto.
9. No confundir "instalar la siguiente copperlist" con "reiniciar el copper ahora mismo".
   En el original, `CopListRun` actualiza `COP1LC` y la nueva lista entra en el siguiente frame; si se hace `COPJMP1` en mitad del barrido, aparecen franjas horizontales y corrupciones dependientes de la fase del wrap.

### Fase 3 - Wrap por modulo

Objetivo:

- introducir cambios de `BPL1MOD/BPL2MOD` por linea

Cierre:

- wrapping vertical correcto sin corrupcion

### Fase 4 - Gradientes por copper

Objetivo:

- cambios de `COLORxx` por banda

Cierre:

- escena visual ya muy cercana al original

### Fase 5 - Caso compuesto final

Objetivo:

- reconstruccion por frame de la copperlist con doble buffer

Cierre:

- tecnica completa ya lista para valorar extraccion al engine

## 11. Candidatos claros a reusable del engine

Lo que ya se ve reusable:

- helpers de display/fetch/modo compatibles con ACE
- `engine_copper_wait_safe_*`
- builder de dual playfield con planos pares/impares
- soporte de lista copper doble-buffer con disciplina de frame
- promocion explicita de assets DMA a CHIP (`engine_alloc_chip_copy`)
- pipeline de validacion que comprueba residencia CHIP de los `BPLxPT` cuando el caso lo declara

Lo que aun no deberia fijarse como API publica:

- builder exacto de gradientes de `layers`
- politica final de captura/seleccion de frame de referencia
