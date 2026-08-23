# Harness DAP para el adaptador Amiga (depuración sin VS Code)

Conduce una sesión de depuración completa (launch → setBreakpoints →
configurationDone) contra el adaptador `amiga` ejecutado como servidor
standalone, sin abrir VS Code. Sirve para verificar que los breakpoints se
detienen y que el stack trace resuelve fuente C++, y para reproducir bugs de la
capa DAP (como el de los breakpoints con `qOffsets=0`).

## Por qué existe

El adaptador empaquetado (`dist/debugAdapter.js`) no podía ejecutarse standalone
por el multi-entry `dependOn` de webpack. Se arregló en el fork
(`webpack.config.js`: entradas independientes). Ahora:

```
node <fork>/dist/debugAdapter.js --server=<puerto>
```

lanza el adaptador en modo servidor y `dap-client.js` actúa de cliente DAP.

## Prerrequisitos

1. **Fork compilado**: en `vscode-amiga-debug`:
   ```bash
   npm install
   npm run vscode:prepublish   # genera dist/debugAdapter.js
   ```
2. **Stub de `vscode`**: el adaptador requiere el módulo `vscode` (solo existe
   en el host de extensiones). Copiar el stub a `node_modules` del fork:
   ```bash
   cp tools/dap-test/vscode-stub/index.js <fork>/node_modules/vscode/index.js
   ```
   (En Windows, crear antes la carpeta `node_modules\vscode`).
3. **Ejecutable a depurar**:
   ```bash
   bash ./tools/debug/build-current-demo.sh "demos/050_blitter_bobs/src/main.cpp"
   ```
   Genera `out/debug-current/current.elf` y `current.exe`.
4. **Kickstart** en `C:/Amiga/KICK13.rom` (o ajustar en `dap-client.js`).

## Uso

```bash
# Linux/macOS
node tools/dap-test/dap-client.js 4711 \
  out/debug-current/current \
  demos/050_blitter_bobs/src/main.cpp 296

# Windows (rutas absolutas)
node tools/dap-test/dap-client.js 4711 \
  "C:/.../Amiga-Cpp/out/debug-current/current" \
  "C:/.../Amiga-Cpp/demos/050_blitter_bobs/src/main.cpp" 296
```

Salida esperada (caso correcto):

```
=== stopped reason=breakpoint thread=1
   (anonymous namespace)::DemoGame::update@0x00c0f02a @ .../main.cpp : 298
```

## Reproducir el caso qOffsets=0 (GDB conecta antes de cargar)

Es el fallo original: GDB conecta durante el arranque, `qOffsets=0`, y GDB
auto-continuaba los breakpoints sin notificar. Para reproducirlo con el harness:

1. En `WinUAE-DBG` está el fix (`gdb_force_s05_at_entry`): fuerza un `S05` en la
   entrada de proceso cuando los offsets estaban sin resolver.
2. Para forzar `qOffsets=0` de forma determinista, retrasar la carga del proceso
   añadiendo un `type dh0:delay.txt >nil:` en la startup-sequence de la
   extensión (`bin/dh0/s/startup-sequence`) con un fichero grande. El cliente
   conecta antes de que el proceso cargue.

El log del adaptador se escribe en `%TEMP%\amiga-debug-trace.log` (trazas DAP y
MI) y el RSP en `%TEMP%\winuae-gdb.log`.

## Nota

`dap-client.js` es una herramienta de diagnóstico del toolchain Amiga. Si se
quiere depurar otros adaptadores DAP, es un buen punto de partida genérico, pero
no es un cliente DAP completo (falta UI, scopes, evaluación, etc.).
