# Ideas para hacer al asistente IA más autónomo en pruebas visuales e interacción

El asistente no puede “ver” ni interactuar directamente con las aplicaciones (WinUAE, demos Amiga, etc.) como el usuario. Este documento recopila ideas para acercar ese uso y permitir más autonomía en pruebas y depuración.

---

## 1. Limitación actual

- El asistente no tiene acceso a tu escritorio, ventanas ni periféricos.
- No ve en tiempo real lo que ocurre en la pantalla.
- No puede mover el ratón, hacer clic ni pulsar teclas en tus aplicaciones.
- Solo puede analizar código, logs, capturas que le proporciones y sugerir cambios.

---

## 2. Ideas por enfoque

### 2.1 Scripts de automatización que producen artefactos

**Descripción:** Scripts (PowerShell, Python, AutoIt, etc.) que:

1. Arrancan WinUAE / la demo.
2. Ejecutan secuencias de acciones (mover ratón, clics, teclas).
3. Capturan pantallas en momentos clave.
4. Guardan logs, vídeos o reportes.

**Ejemplo:** Un script que inicia WinUAE con absolute_mouse, mueve el cursor por la pantalla, toma capturas cada 100 px y guarda un informe con timestamps. Tú ejecutas el script y compartes el resultado; el asistente lo analiza.

**Pros:** Reutilizable, reproducible.  
**Contras:** Tú sigues teniendo que ejecutarlo; el asistente no actúa en directo.

---

### 2.2 Servidor MCP de automatización de escritorio

**Descripción:** Un servidor MCP (Model Context Protocol) que exponga herramientas tipo:

- `launch_application(path, args)`
- `send_mouse_move(x, y)`
- `send_mouse_click(button)`
- `send_key(key)`
- `capture_window_screenshot(process_name)`
- `capture_region(x, y, w, h)`

**Implementación:** Envolver APIs de Windows (UI Automation, PyAutoGUI, `user32.dll`) o herramientas como WinAppDriver en un servidor MCP que Cursor pueda invocar.

**Pros:** El asistente podría lanzar apps, enviar input y ver capturas de forma estructurada.  
**Contras:** Hay que desarrollar y mantener el servidor.

---

### 2.3 Máquina virtual accesible por el asistente

**Descripción:** Una VM (QEMU, VirtualBox, etc.) en la que:

- Corra Windows (o Linux) con WinUAE instalado.
- El asistente tenga acceso vía SSH o API (por ejemplo, para ejecutar comandos).
- Se use un servidor VNC/rdp para capturar pantallas bajo demanda.

**Flujo:** El asistente lanza comandos (ej. “iniciar WinUAE con config X”), espera, pide una captura, analiza la imagen y decide el siguiente paso.

**Pros:** Entorno controlado y reproducible.  
**Contras:** Más complejo de montar; latencia; el asistente sigue necesitando herramientas (SSH, captura de pantalla) para automatizar.

---

### 2.4 Herramientas de testing con UI (WinAppDriver, etc.)

**Descripción:** Usar Windows Application Driver (WinAppDriver) o equivalentes para:

- Localizar ventanas y controles.
- Simular clics y teclas.
- Obtener atributos de la interfaz.
- Integrar con un script que el asistente pueda modificar o que genere reportes.

**Pros:** Pensado para pruebas automatizadas de aplicaciones Windows.  
**Contras:** Suele requerir que la app tenga soporte para accesibilidad / UI Automation.

---

### 2.5 Bucle de feedback con capturas manuales

**Descripción:** Proceso manual pero estructurado:

1. Reproduces el fallo.
2. Tomas capturas en momentos concretos (antes/después de redimensionar, al entrar el ratón, etc.).
3. Pegas o adjuntas las capturas en el chat.
4. El asistente las interpreta y propone cambios.

Para guiar al asistente, conviene indicar qué representa cada captura (ej. “cursor atrapado en la línea superior”, “deriva hacia la derecha en el extremo”).

**Pros:** No requiere infraestructura nueva.  
**Contras:** No es automático; depende de que tú captures y compartas.

---

### 2.6 Tests “headless” con métricas

**Descripción:** Para problemas de coordenadas y mapeo:

- Añadir en WinUAE un modo de depuración que registre:
  - Posición del cursor Windows.
  - Posición enviada al Amiga.
  - Valores de `amigawinclip_rect`, etc.
- Un script ejecuta acciones conocidas y compara con valores esperados.

El asistente puede proponer y revisar esos tests y la lógica de comparación.

**Pros:** Objetivo, reproducible, útil para regresiones.  
**Contras:** No cubre problemas puramente visuales; requiere instrumentación en el código.

---

## 3. Recomendación práctica a corto plazo

1. **Capturas estructuradas:** Cuando reproduzcas un fallo, toma 2–3 capturas (antes, durante, después) y descríbelas brevemente.
2. **Script de prueba:** Mantener y extender `test-mouse-absolute.ps1` (u otro) para ejecutar secuencias fijas y guardar salida/logs.
3. **MCP de escritorio:** Si tienes tiempo, explorar un MCP que permita:
   - Lanzar WinUAE.
   - Enviar input.
   - Capturar pantalla de una ventana concreta.

Con eso el asistente puede trabajar con descripciones + capturas + logs y scripts, y un futuro MCP le daría más autonomía sin cambiar tu forma de usar Cursor.

---

## 4. Referencias

- [WinAppDriver](https://github.com/microsoft/WinAppDriver)
- [PyAutoGUI](https://pyautogui.readthedocs.io/) – control de ratón y teclado en Python
- [MCP (Model Context Protocol)](https://modelcontextprotocol.io/) – protocolo para extender asistentes con herramientas
- [Cursor MCP](https://docs.cursor.com/context/model-context-protocol) – integración de MCP en Cursor
