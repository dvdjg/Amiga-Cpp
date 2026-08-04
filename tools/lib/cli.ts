/**
 * Utilidades de linea de comandos compartidas por las herramientas TypeScript.
 */
/** Devuelve el valor de un argumento `--clave valor` o el fallback. */
export function argValue(argv: string[], name: string, fallback?: string): string | undefined {
	const index = argv.indexOf(name);
	if (index >= 0 && index + 1 < argv.length) {
		return argv[index + 1];
	}
	return fallback;
}

/** Comprueba si un flag booleano `--flag` esta presente. */
export function hasFlag(argv: string[], name: string): boolean {
	return argv.includes(name);
}

/** Pausa asincrona de `ms` milisegundos. */
export function sleep(ms: number): Promise<void> {
	return new Promise((resolve) => setTimeout(resolve, ms));
}

/** Falla con un mensaje claro y termina el proceso con codigo 1. */
export function fail(message: string): never {
	console.error(message);
	process.exit(1);
}
