# Demo 010: modelo Chip/Slow memory

Objetivo: validar el modelo inicial de memoria para `A500_1MB_Slow`.

La demo reserva tres arenas desde el backend:

- `ChipArena`: recursos que podria leer el chipset.
- `SlowArena`: metadatos, entidades y scripts.
- `FrameScratch`: trabajos temporales del frame.

Tambien fuerza una reserva demasiado grande en Chip para comprobar que el fallo se
detecta y no se convierte en corrupcion silenciosa.

## Build, run y analisis

```powershell
.\tools\build\build-demo.ps1 demos\010_chip_slow_memory -DebugBuild
.\tools\run\run-demo.ps1 demos\010_chip_slow_memory
.\tools\analyze\analyze-demo.ps1 demos\010_chip_slow_memory
```

O como parte de la regresion completa:

```powershell
.\tools\test-regression.ps1
```

## Criterio de aceptacion

- La demo muestra `Arena checks: OK`.
- La captura contiene barras diferenciadas para Chip, Slow y Frame.
- La captura muestra bases de memoria coherentes: Chip en rango bajo y Slow en rango
  trapdoor/bogo cuando el perfil emulado la expone.
- La regresion debe compilar, ejecutar, capturar y analizar la demo.
