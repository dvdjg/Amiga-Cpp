# HOST-308 - os_cd32_pad

Test host del **decodificador del pad CD32** (`eng/os/input.hpp`, M2).

## Qué valida

`cd32_mask_from_shift(bits)` mapea los **8 bits serie** del registro de desplazamiento (74LS165,
**activo a 0**; `bit 0` = primer bit = **Blue**) al bitmask estable `Cd32Btn`:

- sin botones (7 bits a 1) → mask 0;
- cada botón por separado (Blue/Red/Yellow/Green/Forward/Reverse/Play);
- todos pulsados y combinaciones;
- el bit de **firma** (bit 7) no aporta botones.

## Fuera de alcance (hardware)

La lectura real (reloj por **CIA-A PRA bit 7** como salida + dato en **`POTINP` bit 14** + `POTGO`,
con `os::enable_cd32_pad`) se valida en emulador. Orden del stream calibrado contra
`WinUAE-DBG/inputdevice.cpp:4050-4053`; ver `MINI_OS_INPUT.md` §6.

## Build / run

```
CXX=<g++> bash tools/run-host-tests.sh tests/host/308_os_cd32_pad
```
