# Instalar MCP Debug Tools desde VSIX (herramientas Amiga)

Si la extensión no muestra las herramientas Amiga (`read-register-list`, `read-memory`, `execute-command`) o falla con iconv-lite, instala esta versión desde el VSIX.

## Pasos (importante: en este orden)

1. **Cierra Cursor por completo** (todas las ventanas, salir de la aplicación).

2. **Desinstala** la extensión actual:
   - Abre Cursor.
   - Panel Extensiones (Ctrl+Shift+X).
   - Busca "MCP Debug Tools".
   - Desinstala.

3. **Vuelve a cerrar Cursor** (para que no quede ningún proceso con la extensión antigua).

4. **Abre Cursor** de nuevo.

5. **Instala desde VSIX**:
   - Extensiones → menú "..." (arriba) → **Install from VSIX...**
   - Navega a:
     ```
     C:\Users\dvdjg\Documents\programa\AI\mcp-debug-tools\mcp-debug-tools-0.2.3.vsix
     ```
   - Selecciónalo e instala.

6. **Recarga la ventana**: Ctrl+Shift+P → "Developer: Reload Window".

7. **Comprueba**:
   ```bash
   node scripts/test-mcp-amiga-tools.js
   ```
   Deberías ver "Herramientas en servidor: 29 | Amiga: read-register-list, read-memory, execute-command, read-registers" y "OK" para cada herramienta.

## Si ves "fetch failed" o "No active VSCode instances found"

Significa que **el servidor HTTP de la extensión no está escuchando** en el puerto que usa el CLI (o la extensión no ha arrancado).

1. **Comprueba que el servidor esté activo**
   - Abre **Output** (Ver → Salida), en el desplegable elige **"Extension Host"**.
   - Recarga la ventana (Ctrl+Shift+P → "Developer: Reload Window") y espera unos segundos.
   - Busca líneas que empiecen por `DAP Proxy:`:
     - `DAP Proxy extension activating...` → la extensión se está activando.
     - `DAP Proxy: HTTP server listening at http://localhost:XXXX/mcp` → servidor activo en el puerto XXXX.
     - `DAP Proxy: Port 8890 was in use, using 8891` → el servidor está en **8891**; en ese caso en `mcp.json` usa `"--port=8891"` en `args`.
   - Si no aparece ninguna línea "DAP Proxy", la extensión no se está activando (deshabilitada, error al cargar, etc.).

2. **Barra de estado**: abajo a la derecha debe aparecer algo como `DAP-MCP:8890`. Si ves el puerto, el servidor está activo; usa ese mismo puerto en `mcp.json` si hace falta.

3. **Sin puerto fijo en mcp.json**: si quitas `"--port=8890"` de `args`, el CLI intentará descubrir el puerto (workspace `.mcp-debug-tools/config.json` o registro global). Eso solo funciona si tienes **una carpeta abierta** como workspace (p. ej. Cursor-Amiga-C), no solo un archivo.

4. **Ventanas múltiples**: si tienes varias ventanas de Cursor, cada una puede usar un puerto distinto (8890, 8891, 8892…). El CLI se conecta a uno; asegúrate de que `--port=` en `mcp.json` sea el de la ventana donde tienes este proyecto.

## Si sigue fallando

- Asegúrate de tener **solo una** ventana de Cursor abierta (o que la que uses para Cursor-Amiga-C sea la primera).
- No tengas la carpeta **mcp-debug-tools** como raíz del workspace al probar (solo Cursor-Amiga-C).
