# Auditoría de `mcp-debug-tools`

## Resultado

Las modificaciones locales son útiles para un entorno con varias ventanas de VS Code/Cursor y varios proyectos Amiga. No deben mezclarse con `mcp-winuae-emu`: `mcp-debug-tools` habla con la extensión MCP/DAP del IDE, mientras que `mcp-winuae-emu` habla directamente con WinUAE-DBG por GDB RSP.

## Cambios propios detectados

La rama local `fix` contiene dos commits sobre `origin/main` y además tres archivos modificados sin commit. El conjunto completo afecta a 16 archivos y añade mejoras de herramientas DAP/MCP, servidor, estado, recursos, parámetros y empaquetado VS Code.

Los cambios más relevantes para Amiga son:

- `src/cli.ts` fuerza `127.0.0.1`, acepta `--host` y `--workspace`, permite `MCP_DEBUG_WORKSPACE` y amplía la espera de conexión de unos 30 a unos 60 segundos.
- `src/config-finder.ts` busca primero la configuración `.mcp-debug-tools/config.json` del workspace elegido, después la ruta ascendente desde `cwd` y finalmente el registro global.
- `MCP_DEBUG_TOOLS_RULES.md` documenta cómo fijar el workspace para no conectar accidentalmente la IA a otra instancia del IDE.
- Los commits también incorporan herramientas DAP y cambios de integración que conviene conservar, pero deben probarse con la extensión antes de actualizar el VSIX.

## Valor para este proyecto

El binding explícito al workspace es especialmente valioso porque pueden estar abiertas a la vez `Amiga-Cpp`, `Universal-Asset-Format` y otros proyectos. El uso de IPv4 evita problemas de resolución de `localhost` en Windows. Los reintentos adicionales cubren el arranque lento de una extensión que inicializa el adaptador DAP.

Para depuración de la CPU Amiga, el canal principal seguirá siendo `mcp-winuae-emu` más el canal lateral `:2346`. `mcp-debug-tools` será útil cuando queramos controlar desde el IDE la sesión DAP de Bartman, inspeccionar configuraciones `launch.json` o coordinar el estado de VS Code.

## ¿Crear un fork?

Sí, si se van a seguir incorporando mejoras. El repositorio actual apunta a `https://github.com/hwanyong/mcp-debug-tools.git`; la rama `fix` contiene trabajo propio suficiente para justificar un fork bajo la cuenta de desarrollo. No lo he creado automáticamente porque el nombre del repositorio y la cuenta de destino son decisiones de publicación, y crear un fork remoto requiere confirmación explícita.

Mientras tanto, la práctica segura es:

1. Mantener los cambios locales sin sobrescribirlos.
2. Ejecutar `npm run compile` y `npm run lint` antes de cada VSIX.
3. Separar cambios genéricos DAP de adaptaciones Amiga.
4. Añadir pruebas para selección de workspace, `127.0.0.1` y reintento de conexión.
5. Crear el fork y cambiar `origin` cuando se decida el nombre público.

## Estado de verificación

`npm run compile` pasa. `npm run lint` no tiene errores, pero muestra 1107 avisos de estilo heredados, principalmente por puntos y comas ausentes. No se ha instalado el VSIX modificado ni se ha alterado la extensión Bartman.
