# Demo 000: toolchain C++23

Objetivo: comprobar que el toolchain Bartman/GCC compila C++23 real para
`m68k-amiga-elf` y que el engine ya arranca con una separacion limpia entre juego,
backend y memoria.

## Que verifica

- `-std=gnu++23`.
- `consteval`, templates y conceptos del engine.
- Backend minimo `MinimalBackend`.
- Arenas `Chip` y `Slow`.
- Overlay de debug de WinUAE-DBG.
- Bucle de frames a 50 Hz mediante espera de VBlank.

## Build

```powershell
.\tools\build\build-demo.ps1 demos\000_toolchain_cpp23 -Clean
.\tools\run\run-demo.ps1 demos\000_toolchain_cpp23
.\tools\analyze\analyze-demo.ps1 demos\000_toolchain_cpp23
```

Salida esperada:

```text
out\demos\000_toolchain_cpp23\000_toolchain_cpp23.exe
```

## Criterio de aceptacion

- El binario `.exe` y el `.elf` se generan correctamente.
- El mapa contiene `_start` y `_main`.
- El runner genera `out\run\000_toolchain_cpp23\screenshot.png`.
- En la captura debe verse el overlay `AMG demo 000 - C++23 abstractions online`.
- La linea `Memory arenas: OK` confirma que las arenas iniciales funcionan.
- El analizador automatico detecta los colores de overlay esperados.
