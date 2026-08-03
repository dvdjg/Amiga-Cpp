# 68000: pila, ABI y llamadas

## Documentos padre

- [README principal](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/README.md)
- [Indice de documentación](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/engine-docs-index.md)
- [Kernel y loader del Amiga 500](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-kernel-loader-notes.md)
- [Cargador de desarrollo Amiga](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-dev-harness-loader.md)

## Objetivo

Recoger los detalles del ABI m68k que más nos afectan cuando:

- llamamos a C desde ensamblador;
- saltamos a payloads de forma directa;
- o capturamos postmortems de excepcion.

## Registros relevantes

- `D0-D7`: datos
- `A0-A6`: direcciones
- `A7`: stack pointer
- `PC`: program counter
- `SR`: status register

En GCC/m68k para Amiga:

- `D0` devuelve el valor de retorno entero
- `D1`, `A0` y `A1` también son scratch
- el resto deben preservarse si una función los modifica

## Convencion de llamada C (GCC m68k)

Reglas practicas:

- la llamada se hace con `JSR`
- los parametros se empujan de derecha a izquierda
- los tipos de 8 y 16 bits se promocionan antes de apilarse
- el frame pointer normal de GCC esta en `A6`

Eso implica:

- si escribimos una entrada ensamblador hacia C, tenemos que presentar una pila coherente;
- si lanzamos una función C por carga directa y `A7` no es valida, el crash puede ocurrir antes de cualquier telemetría propia.

## Caller-saved y callee-saved

Scratch:

- `D0`
- `D1`
- `A0`
- `A1`

Conservacion obligatoria si una función los toca:

- `D2-D7`
- `A2-A6`

Implicacion para stubs:

- un stub de arranque que entre en C debería instalar primero la pila y solo despues llamar a la función C principal;
- eso ya está aplicado en la ruta `metal` del repo.

## `A6` como frame pointer

GCC usa `A6` como base del stack frame para acceder a:

- parametros
- variables locales
- saved registers

Por eso, al leer disassembly/postmortem, offsets típicos como `8(a6)` o `-4(a6)` suelen indicar:

- parametros del caller
- o variables locales del callee

## Limpieza de la pila

Caso normal:

- el caller limpia la pila tras la llamada

Caso `-mrtd`:

- el callee puede devolver con `RTD #n`

Para este repo:

- la suposición segura es el caso normal
- y cualquier ensamblador reutilizable debería dejarlo documentado si depende de `-mrtd`

## `-mshort`

`-mshort` cambia el tamano de `int` a 16 bits.

Riesgo:

- offsets de pila distintos
- ABI distinto para argumentos

En este proyecto debemos evitar que un payload ensamblador presuponga offsets si no conoce exactamente los flags de compilación.

## Excepciones 68000

Las excepciones importantes para nosotros:

- `BUS ERROR`
- `ADDRESS ERROR`
- `ILLEGAL INSTRUCTION`

Cuando capturamos una excepcion:

- la CPU construye un exception frame en la pila activa
- ese frame no es el mismo que el stack frame normal de una función C
- si la pila ya está corrupta, el postmortem se degrada muy rápido

Consecuencia:

- el mejor sitio para capturar estas excepciones es un stub propio muy pequeño, con pila conocida y contrato simple en RAM
- por eso la ruta `metal/direct` sigue siendo la base correcta para autopsia dura

## Lo que necesita una entrada directa “sana”

Si queremos saltar manualmente a un payload:

1. `PC` a una entrada conocida
2. `A7` a una pila valida y alineada
3. estado de registros conocido
4. contrato claro sobre que registros son basura y cuales contienen argumentos

Sin eso:

- un binario GCC puede fallar antes de `main`
- y el crash no demuestra que el binario este mal, sino que el contrato de entrada es incompleto

## Reglas practicas para este repo

- **Payload DOS**: no arrancarlo como `PC=entry` salvo diagnóstico.
- **Payload metal**: darle su propio `entry` y su propia pila.
- **Bridge DOS -> metal**: el harness DOS puede cargar y señalar, pero el payload metal debe seguir teniendo su stub de entrada propio.

## Fuentes consultadas

- [xGCC Manual, sección 4.2.1 68k calling convention](https://gendev.spritesmind.net/files/xgcc/xgcc.pdf)
- [Motorola M68000 Family Programmer's Reference Manual](https://m680x0.github.io/ref/M68000PM_AD_Rev_1_Programmers_Reference_Manual_1992.pdf)
