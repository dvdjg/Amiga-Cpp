# Postmortems Convertidos En Reglas

Este documento recoge bugs reales del proyecto y los convierte en reglas
reusables del Amiga 500. La idea es que cada autopsia deje una leccion
operativa que pueda aplicarse a otros efectos, al engine y al skill.

## DX39 - Assets DMA fuera de CHIP

### Sintoma

- dual playfield aparentemente bien configurado;
- registros base razonables;
- pero la imagen salia rota o como basura.

### Causa raiz

Los bitplanes importados de `layers` se estaban usando desde el segmento del
ejecutable DOS, no desde CHIP RAM.

### Regla reusable

Si un recurso lo consume DMA, su direccion debe verificarse como CHIP antes de
culpar a layout, scroll o copper.

### Guardrail aplicado

- `engine_alloc_chip_copy()`
- `requires_chip_bitplanes`
- comprobacion de `bitplaneResidency` en evidencia

## DX39P5 - WAIT no seguro en lineas altas

### Sintoma

- corrupcion horizontal o "basura" localizada en algunas fases del wrap;
- el resto del efecto parecia razonable.

### Causa raiz

El helper local de `WAIT` no manejaba bien el overflow vertical para lineas
mayores de `255`.

### Regla reusable

En tecnicas con raster dinamico sobre PAL completa, usar `WAIT` seguro.

### Guardrail aplicado

- documentacion explicita de `WAIT` seguro
- preferencia por helpers de overflow vertical

## DX39P5 - Estado inicial distinto del original

### Sintoma

- el primer frame ya salia mal o desalineado;
- parecia un fallo de dimensiones u offsets.

### Causa raiz

La primera copperlist se construia ya con la orbita activa, mientras que el
original arrancaba con estado base y solo entraba en la animacion desde el
primer `Render()`.

### Regla reusable

No asumir que `frame 0` del port debe arrancar ya en movimiento. Antes hay que
replicar la secuencia temporal del original: `init`, primera lista, primer
frame animado.

## DX39P5 - COP1LC no es COPJMP1

### Sintoma

- franjas horizontales dependientes de fase;
- el fallo aparecia y desaparecia segun el momento del muestreo.

### Causa raiz

La API del engine mezclaba dos acciones distintas:

- encolar la siguiente lista mediante `COP1LC`,
- y reiniciar el copper mediante `COPJMP1`.

Forzar el reinicio en mitad del barrido rompia justo las fases sensibles.

### Regla reusable

En cobre dinamico:

- `set COP1LC` cola trabajo para el siguiente frame;
- `COPJMP1` reinicia el copper ahora mismo.

No deben mezclarse en una misma API de ciclo normal.

### Guardrail aplicado

- `engine_copper_double_queue_front()`
- `engine_copper_double_install_front()` pasa a tener semantica de cola
- `engine_start_copper()` queda como operacion separada

## Como usar este documento

Cuando aparezca un bug low-level nuevo, anadir:

1. sintoma,
2. causa raiz,
3. regla reusable,
4. guardrail automatizado si existe.

Objetivo:

- que la IA no tenga que redescubrir el mismo fallo;
- y que el proyecto acumule conocimiento real del hardware, no solo fixes
  aislados.
