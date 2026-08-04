# Diagnóstico: ADF se queda en negro

## Resumen de la depuración en Coppenheimer (2025-02-11)

### Estado observado

- **disk.adf** cargado en DF0
- **CPU activa** (barras de Chipmem/Fastmem reads/writes muestran actividad)
- **"Possible bitplane areas"** vacío → no hay bitplanes activos
- **Pantalla negra**

### Conclusiones

1. **El programa está ejecutándose** – hay actividad de CPU/DMA.
2. **El menú depende del overlay WinUAE** (`engine_debug_*` → `debug_cmd` → `UaeLib` en 0xf0ff60).
3. **Ese overlay solo existe en WinUAE** – en Coppenheimer/vAmiga/real no hay nada en 0xf0ff60.
4. **La copper del menú usa 0 bitplanes** (`BPLCON0=0x0200`) – correcto para fondo negro, pero todo el texto y los rectángulos se dibujan solo vía overlay.
5. **En Coppenheimer (y en arranque ADF sin overlay)**: se ve solo color0 (negro), sin texto ni rectángulos.

### Causa raíz

El menú actual usa `engine_debug_*` (overlay WinUAE) para todo el contenido visible. Cuando el overlay no está disponible (Coppenheimer, boot ADF en algunos casos, hardware real), la pantalla queda negra aunque el programa funcione.

### Soluciones propuestas

1. **A corto plazo**: Detectar si el overlay está disponible antes de usarlo; si no, seguir mostrando negro (evita crash; el menú sigue siendo usable por teclado).
2. **A medio plazo**: Añadir un modo de menú que dibuje con bitplanes reales (blitter/CPU) cuando el overlay no exista, para que funcione en Coppenheimer y hardware real.

### Comandos vAmiga Retro Shell usados

- `df0` – configuración del disquete
- `cpu` – configuración CPU 68000
- `denise` – chip de video OCS
- `agnus` – chip DMA
- **Guess!** – detecta posibles áreas de bitplanes (resultado: vacío)
