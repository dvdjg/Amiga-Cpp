/**
 * Utilidades de linea de comandos compartidas por las herramientas TypeScript.
 */
/** Devuelve el valor de un argumento `--clave valor` o el fallback. */
export function argValue(argv, name, fallback) {
    const index = argv.indexOf(name);
    if (index >= 0 && index + 1 < argv.length) {
        return argv[index + 1];
    }
    return fallback;
}
/** Comprueba si un flag booleano `--flag` esta presente. */
export function hasFlag(argv, name) {
    return argv.includes(name);
}
/** Pausa asincrona de `ms` milisegundos. */
export function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
}
/** Falla con un mensaje claro y termina el proceso con codigo 1. */
export function fail(message) {
    console.error(message);
    process.exit(1);
}
