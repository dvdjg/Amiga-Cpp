# Checklist de setup de display Amiga

Checklist operativa para evitar reinvenciones y fallos repetidos en escenas que usan display custom.

Objetivo: que antes de implementar una tecnica nueva se verifique si el repo ya dispone de una base de display valida y que invariantes low-level deben respetarse.

## Cuando usarla

Usarla antes de cualquier trabajo que haga una de estas cosas:

- configurar bitplanes;
- preparar una copper list de display;
- cambiar modo lores/hires o numero de bitplanes;
- usar bitplanes interleaved;
- mezclar playfield base con sprites CPU, BOBs, HUD o copper dinamico.

## Regla principal

Si el caso nuevo necesita un setup de display que ya existe validado en el repo, no reinventarlo.

Primero hay que declarar:

1. cual es la referencia viva reutilizada;
2. que parte del display queda heredada sin cambios;
3. cual es la hipotesis nueva del caso.

Ejemplo:

- referencia viva: `T02_lores_32c`
- display heredado: `320x256`, interleaved, `BPLxMOD` correcto, paleta, `DIW/DDF`
- hipotesis nueva: dibujo CPU masked sobre ese playfield

## Inventario tecnico minimo previo

Antes de codificar, completar:

- **Modo objetivo:** `lores` / `hires` / `dual playfield` / `HAM` / `otro`
- **Numero de bitplanes:**
- **Layout de memoria:** `planar separado` / `interleaved`
- **Referencia viva base del repo:**
- **APIs del engine reutilizadas para display:**
- **Partes nuevas que si hay que implementar:**
- **Partes que no deben tocarse respecto a la referencia base:**

Si no existe referencia viva, declararlo explicitamente y abrir un caso base primero.

## Invariantes de setup de display

### 1. Geometria base

- `DIWSTRT/DIWSTOP` coherentes con la ventana visible esperada
- `DDFSTRT/DDFSTOP` coherentes con el modo y anchura
- resolucion objetivo declarada en pixels visibles

### 2. Modo de bitplanes

- `BPLCON0` coincide con el numero de bitplanes real
- si aplica, `BPLCON1/BPLCON2` estan en el estado esperado
- si se usa dual playfield, prioridades y reparto de planos estan declarados

### 3. Punteros y memoria

- `BPLxPT` apuntan a CHIP RAM valida
- el layout de memoria coincide con lo que espera la copper list
- si el caso usa interleaved, los punteros y el stride se han calculado con ese contrato

### 4. Modulos de bitplane

Comprobar siempre:

- `BPL1MOD`
- `BPL2MOD`

Preguntas obligatorias:

1. El layout es interleaved?
2. El numero de planos hace que el modulo no sea cero?
3. Hay una referencia viva que ya muestre el valor correcto?
4. El caso futuro cambiara `BPLxMOD` dinamicamente por copper o CPU?

Regla importante:

- si la pantalla es interleaved y el caso base ya usa modulos distintos de cero, asumir que copiarlos mal produce artefactos visuales aunque el resto del setup parezca correcto.

### 5. DMA y copper

- DMA raster y copper realmente activados
- `COP1LC` apunta a la lista que creemos activa
- la lista termina correctamente
- si la lista es parcheada, la politica de parcheo es segura

## Tabla recomendada antes de codificar

| Elemento necesario para la demo | Lo aporta engine | Lo aporta un caso base validado | Hay que implementarlo nuevo | Referencia |
|---------------------------------|------------------|----------------------------------|-----------------------------|-----------|
| Modo display | | | | |
| Copper base | | | | |
| Paleta | | | | |
| Punteros bitplane | | | | |
| `BPL1MOD/BPL2MOD` | | | | |
| Fondo base | | | | |
| Primitive de sprite/BOB | | | | |
| Overlay/HUD | | | | |
| Politica por frame | | | | |

La regla es simple:

- si una fila ya esta cubierta por engine o por un caso base, no reescribirla "por comodidad".

## Sintomas de fallo que deben disparar esta checklist

Si aparece cualquiera de estos sintomas, revisar primero el setup de display antes de cambiar la tecnica nueva:

- bandas o rayas absurdamente densas;
- patron visual que parece ruido al usar varios bitplanes;
- colores correctos pero geometria ilegible;
- sprites o BOBs que "existen" pero sobre una base visual incoherente;
- el modelo de vision dice que "mas o menos coincide" pero a ojo humano la escena se ve mal.

## Reutilizacion recomendada del repo

Para display base, mirar primero:

- [T01_lores_16c](../tests/amiga-battery/T01_lores_16c/README.md)
- [T02_lores_32c](../tests/amiga-battery/T02_lores_32c/README.md)
- [T06_dual_pf_3p3_offset](../tests/amiga-battery/T06_dual_pf_3p3_offset/README.md)
- [T07_dual_pf_scroll_independent](../tests/amiga-battery/T07_dual_pf_scroll_independent/README.md)

## Leccion incorporada al proyecto

El caso `CS02_cpu_sprite_4bpl_masked` mostro que una primitive nueva puede ser correcta y aun asi producir una escena confusa si se reabre sin querer un detalle ya resuelto del display base.

La causa concreta fue no heredar explicitamente el contrato interleaved completo del caso de referencia, incluyendo `BPL1MOD/BPL2MOD`.

La politica del proyecto a partir de aqui debe ser:

- separar la hipotesis nueva del caso del display base heredado;
- hacer inventario tecnico previo;
- copiar primero la base viva y cambiar despues solo la parte bajo prueba.
