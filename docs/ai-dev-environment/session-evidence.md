# Sesión viva y evidencias

## Inicio

1. Compilar la demo o payload.
2. Lanzar WinUAE con `run-demo.sh --keep-running` o desde F5 con Bartman.
3. Elegir un único propietario del socket GDB `:2345`.
4. Conectar MCP con `winuae_connect_existing` si la ventana ya está viva.
5. Comprobar canal lateral `:2346` con `state`, `regs` y `runstatus`.

## Observación sin modificar

En modo `observe`, tomar:

- `state` y `regs`;
- `winuae_machine_snapshot`;
- `custom_registers` y Copper si el fallo es gráfico;
- `screenshot` interno o de ventana según la pregunta;
- `run-report.json` y el mapa `.map` del binario.

La captura interna es mejor para análisis del framebuffer; `host_window` es la referencia de lo que ve el usuario cuando la ventana está visible.

## Cambio controlado en caliente

1. Explicar el objetivo y la dirección/símbolo afectado.
2. Adquirir `lock takeover`.
3. Pausar si la escritura puede competir con el CPU.
4. Leer y guardar bytes anteriores.
5. Aplicar `poke` o `winuae_memory_write`.
6. Verificar la lectura posterior y capturar evidencia.
7. Reanudar o ejecutar el experimento.
8. Hacer `rollback` y comprobar restauración, salvo que el cambio sea intencional.
9. Liberar el lock y documentar dirección, bytes, motivo y resultado.

El canal lateral ya audita `poke` y `rollback`. La inyección de código 68k en zona scratch sigue siendo una capacidad avanzada y no debe improvisarse en una sesión del usuario.

## Fallo o parada inesperada

Antes de desconectar, capturar:

- `winuae_postmortem_capture`;
- registros CPU, PC/SR/A7 y stack;
- desensamblado alrededor del PC;
- custom registers y mapa de memoria;
- screenshot de la pantalla/requester;
- estado de runtime y log del runner.

Después clasificar la causa como build, transporte/carga, runtime, timing, captura o análisis visual. Esta clasificación evita confundir un falso negro de captura con un fallo real del programa.

## Cierre de evidencia

Una sesión se considera útil cuando tiene:

- artefactos `.elf`, `.exe`, `.map` y configuración `.uae` identificables;
- comando de reproducción;
- captura y/o secuencia;
- resultado determinista (analizador, FrameScope o pixel assertions);
- snapshot/postmortem si hubo fallo;
- análisis visual Ollama solo como segunda opinión, con JSON bruto conservado;
- una nota en `docs/methodology/DEVELOPMENT_LOG.md` o en el documento de la técnica.
