/**
 * Utilidades de rutas compartidas por las herramientas TypeScript.
 *
 * Tras compilar a `dist/`, `import.meta.url` apunta a los archivos emitidos y
 * subir un numero fijo de directorios ya no llega a la raiz del repositorio.
 * Esta funcion sube hasta encontrar `package.json`, de modo que el mismo codigo
 * funciona ejecutando el fuente (dev) o el binario compilado (dist).
 */
import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';
/**
 * Localiza la raiz del repositorio subiendo desde el modulo actual.
 * @param importMetaUrl El valor de `import.meta.url` del llamador.
 * @returns Ruta absoluta al directorio raiz (el que contiene package.json).
 */
export function repoRoot(importMetaUrl) {
    let dir = path.dirname(fileURLToPath(importMetaUrl));
    while (dir !== path.dirname(dir)) {
        if (fs.existsSync(path.join(dir, 'package.json'))) {
            return dir;
        }
        dir = path.dirname(dir);
    }
    return dir;
}
