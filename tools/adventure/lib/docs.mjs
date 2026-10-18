// Documentación **generada** de los directorios de artefactos del workspace de aventuras.
// Escribe un README.md determinista (sin fechas) en cada carpeta de assets, explicando qué es,
// quién lo produce/consume y cómo se relaciona con el resto del pipeline. El texto canónico de
// referencia vive en tools/adventure/WORKSPACE.md; esto es la guía local junto a los datos.
//
// Uso típico: writeDirReadme(dir, { title, what, producedBy, consumedBy, items, related, notes }).

import fs from 'node:fs';
import path from 'node:path';

/**
 * Escribe (o reescribe) `<dir>/README.md` con una ficha determinista del contenido.
 * \param dir directorio destino (se crea).
 * \param spec.title título de la carpeta.
 * \param spec.what párrafo: qué contiene y para qué sirve.
 * \param spec.producedBy herramienta/comando que lo genera.
 * \param spec.consumedBy quién lo consume (etapas siguientes).
 * \param spec.items filas [{name, desc}] de la tabla «Contenido».
 * \param spec.related filas [{name, desc}] de la tabla «Relaciones».
 * \param spec.notes lista de notas (reglas de uso).
 */
export function writeDirReadme(dir, spec) {
	fs.mkdirSync(dir, { recursive: true });
	const lines = [`# ${spec.title}`, '', spec.what, ''];
	lines.push('> Documento **generado** por el pipeline (se reescribe al regenerar la carpeta); la referencia canónica está en `tools/adventure/WORKSPACE.md`.', '');
	lines.push(`**Generado por**: ${spec.producedBy}`, '');
	lines.push(`**Consumido por**: ${spec.consumedBy}`, '');
	if (spec.items?.length) {
		lines.push('## Contenido', '', '| Elemento | Qué es |', '|---|---|');
		for (const it of spec.items) lines.push(`| \`${it.name}\` | ${it.desc} |`);
		lines.push('');
	}
	if (spec.related?.length) {
		lines.push('## Relaciones', '', '| Con | Relación |', '|---|---|');
		for (const r of spec.related) lines.push(`| ${r.name} | ${r.desc} |`);
		lines.push('');
	}
	if (spec.notes?.length) {
		lines.push('## Notas', '');
		for (const n of spec.notes) lines.push(`- ${n}`);
		lines.push('');
	}
	fs.writeFileSync(path.join(dir, 'README.md'), lines.join('\n'));
}

/** Texto común de relaciones del pipeline (para reutilizar en varias fichas). */
export const PIPELINE_RELATIONS = [
	{ name: '`raw/`', desc: 'extracción literal del contenedor; fuente de `decoded/`.' },
	{ name: '`decoded/`', desc: 'contenido semántico y formatos estándar (SIM v2, PNG, FLAC, AST).' },
	{ name: '`edited/`', desc: 'fuente editable del port (lo único no regenerable).' },
	{ name: '`cook/`', desc: 'salida cocinada para el engine (tablas C++, futuros UAF).' },
	{ name: '`reports/`', desc: 'informes de cobertura, opcodes y (futuro) presupuesto.' },
];
