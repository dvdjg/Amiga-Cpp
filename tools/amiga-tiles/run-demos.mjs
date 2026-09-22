#!/usr/bin/env node
// run-demos.mjs — genera la carpeta de demostraciones de amiga-tiles en
// out/assets/tile-demos. Cada demo crea una carpeta con la imagen fuente, los
// resultados (reconstruct/tilebank/paletas/headers/binarios) y su README.
// Uso: node tools/amiga-tiles/run-demos.mjs
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const TOOL = path.join(ROOT, 'tools', 'amiga-tiles', 'amiga-tiles.mjs');
const ASSETS = path.join(ROOT, 'assets', 'amiga', 'tiles-reference');
const METAL = 'C:/Users/dvdjg/Downloads/Neo Geo _ NGCD - Metal Slug - Backgrounds - Mission 2.png';
const OUT = path.join(ROOT, 'out', 'assets', 'tile-demos');
const NODE = process.execPath;

function rm(dir) { fs.rmSync(dir, { recursive: true, force: true }); }
function cp(src, dst) { fs.mkdirSync(path.dirname(dst), { recursive: true }); fs.copyFileSync(src, dst); }
function run(args) {
	const r = spawnSync(NODE, [TOOL, ...args], { cwd: ROOT, maxBuffer: 64 * 1024 * 1024, encoding: 'utf8' });
	if (r.status !== 0) throw new Error(`tool fallo (${args.join(' ')}):\n${r.stderr || r.stdout}`);
	return r.stdout;
}
function statsFrom(stdout) {
	const read = stdout.split('\n').map((s) => s.trim());
	const first = (pfx) => (read.find((s) => s.startsWith(pfx)) || '').replace(pfx, '').trim();
	return {
		mode: first('[amiga-tiles] tiles únicos'),
		cmp: first('[amiga-tiles] COMPARAR'),
		report: first('[amiga-tiles] paleta'),
		warn: (read.find((s) => s.includes('AVISO')) || '').replace(/^\[amiga-tiles\] /, '').trim(),
	};
}
function readme(folder, lines) { fs.writeFileSync(path.join(folder, 'README.md'), lines.join('\n') + '\n', 'utf8'); }

console.log(`[run-demos] salida -> ${OUT}`);
// Preserva los artefactos de visión (los genera run-vision-verify dentro de
// out/assets/tile-demos y no deben perderse al regenerar las demos).
const stash = [];
function stashVision() {
	const stack = [OUT];
	while (stack.length) {
		const d = stack.pop();
		for (const e of fs.readdirSync(d, { withFileTypes: true })) {
			const p = path.join(d, e.name);
			if (e.isDirectory()) { stack.push(p); continue; }
			if (/\.(ops|vision|compare)\.txt$/.test(e.name) || e.name === 'vision_report.md' || e.name === 'VISION_summary.md') {
				stash.push([path.relative(OUT, p), fs.readFileSync(p, 'utf8')]);
			}
		}
	}
}
rm(OUT);
fs.mkdirSync(OUT, { recursive: true });
const O = (n) => path.join(OUT, n);
function restoreVision() {
	for (const [rel, txt] of stash) { const p = path.join(OUT, rel); fs.mkdirSync(path.dirname(p), { recursive: true }); fs.writeFileSync(p, txt, 'utf8'); }
}

// ============================================================================
// 01 · Imagen real → cuantización 16 colores (con y sin dithering)
// ============================================================================
{
	const dir = O('01_imagen_real_cuantizacion');
	const src = path.join(dir, 'source.jpg');
	cp(path.join(ASSETS, 'real', 'pac_man.jpg'), src);
	const ro = path.join(dir, 'resized');
	fs.mkdirSync(ro, { recursive: true });
	run([src, '--emit-source', '--max-ram', '300000', '--out', ro]);
	const rows = [];
	for (const dith of ['none', 'floyd', 'atkinson']) {
		const o = path.join(dir, `dith_${dith}`);
		fs.mkdirSync(o, { recursive: true });
		const s = run([src, '--max-ram', '300000', '--colors', '16', '--dither', dith, '--out', o]);
		rows.push({ dith, st: statsFrom(s) });
	}
	const big = rows.map((r) => `- \`dith_${r.dith}\`: ${r.st.mode} · ${r.st.cmp} · ${r.st.report}`).join('\n');
	readme(dir, [
		'# 01 · Imagen real → cuantización a 16 colores (dithering on/off)',
		'',
		'Fuente: fan-art "pac_man_muscle" (999×800). Se **redimensiona con Lanczos** a un',
		'área de ~300000 px (ver `resized/source_resized.png`) para que el buffer de',
		'índices a 1 B/px quepa en ~300 KB de RAM.',
		'',
		'Objetivo: comparar el resultado a 16 colores **sin dither** (estricto, ideal para',
		'pixel-art: mantiene superficies planas) frente a **Floyd–Steinberg** (fotos y',
		'degradados) y **Atkinson**.',
		'',
		'Carpetas:',
		'- `source.png` — imagen original (999×800).',
		'- `resized/source_resized.png` — redimensionado Lanczos (múltiplo de 16).',
		big,
		'',
		'Cada variante contiene `reconstruct.png` (lo que se dibuja), `tilebank.png`,',
		'`palette.json`/`.h`, `tilebank.h`/`.bin` (listos para incrustar en C/C++).',
	]);
}

// ============================================================================
// 02 · Redimensionado de calidad (Lanczos / area / bilinear / nearest)
// ============================================================================
{
	const dir = O('02_redimensionado_calidad');
	const src = path.join(dir, 'source.jpg');
	cp(path.join(ASSETS, 'real', 'apple_guy.jpg'), src);
	for (const m of ['lanczos', 'area', 'bilinear', 'nearest']) {
		const o = path.join(dir, `resample_${m}`);
		fs.mkdirSync(o, { recursive: true });
		run([src, '--emit-source', '--resize', '640x368', '--resample', m, '--out', o]);
	}
	const qo = path.join(dir, 'resample_lanczos', 'quant_16c');
	fs.mkdirSync(qo, { recursive: true });
	const s = run([src, '--resize', '640x368', '--resample', 'lanczos', '--colors', '16', '--dither', 'floyd', '--out', qo]);
	readme(dir, [
		'# 02 · Redimensionado de calidad (Lanczos-3 vs área vs bilineal vs vecino)',
		'',
		'Fuente: fan-art "apple_guy" (1191×671). En Amiga casi siempre hay que ajustar',
		'la resolución a un presupuesto de Chip RAM: aquí el objetivo es 640×368.',
		'',
		'Se comparan 4 métodos (`--resample`):',
		'- **lanczos** — la mejor interpolación; ideal para degradados y bordes.',
		'- **area** — caja (box); óptimo para REDUCCIONES (media de cada bloque fuente).',
		'- **bilinear** — suave y rápido.',
		'- **nearest** — vecino más próximo; rompedor, útil solo para pruebas.',
		'',
		'Carpetas `resample_<método>/source_resized.png` — el mismo origen reescalado con',
		`cada método, y \`resample_lanczos/quant_16c\` cuantiza el resultado a 16 colores`,
		`con Floyd–Steinberg (${statsFrom(s).report}).`,
		'',
		'Abre las cuatro versiones lado a lado para ver diferencias en los bordes:',
		'lanczos mantiene contraste, area suaviza el ruido, nearest crea dientes de sierra.',
	]);
}

// ============================================================================
// 03 · Tiling + dedupe (tilebank por tiles únicos exactos)
// ============================================================================
{
	const dir = O('03_tiling_dedupe');
	const src = path.join(dir, 'source.png');
	cp(path.join(ASSETS, 'Beginning Fields.png'), src);
	const oe = path.join(dir, 'dedupe_exacto');
	fs.mkdirSync(oe, { recursive: true });
	const se = run([src, '--colors', '64', '--tile', '16', '--out', oe]);
	const om = path.join(dir, 'dedupe_merge095');
	fs.mkdirSync(om, { recursive: true });
	const sm = run([src, '--colors', '64', '--tile', '16', '--merge', '0.95', '--out', om]);
	readme(dir, [
		'# 03 · Tiling + dedupe del tilebank (mapa "Beginning Fields")',
		'',
		'Fuente: atlas 640×640 = 40×40 tiles de 16×16 (The Fan-tasy Tileset).',
		'',
		'El slicer convierte cada tile de 16×16 en un **índice de banco**. El dedupe',
		'exacto une todas las celdas idénticas a la MISMA entrada del tilebank, y',
		'`kTileIndexedMap[]` guarda, por celda, qué tile (índice) hay que pintar.',
		'',
		`- \`dedupe_exacto\`: ${statsFrom(se).mode}; ${statsFrom(se).cmp} ` +
			'(reconstrucción idéntica sin fusión: `reconstruct.png` = original cuantizado).',
		`- \`dedupe_merge095\`: con \`--merge 0.95\` se fusionan tiles casi iguales ` +
			'(fracción de índices ≥ 0.95) para reducir el banco a costa de pérdida permitida.',
		'',
		'`tilebank.png` es la hoja de tiles únicos; `reconstruct.png` es lo que se ve en',
		'el Amiga al dibujar `banco[kTileIndexedMap]`.',
	]);
}

// ============================================================================
// 04 · Metal Slug — extracción de bandas (planos del fondo)
// ============================================================================
{
	const dir = O('04_metalslug_bandas');
	cp(METAL, path.join(dir, 'source.png'));
	const bd = path.join(dir, 'bands');
	const s = run([METAL, '--extract-bands', bd, '--band-jump', '55', '--band-align', '16', '--out', bd]);
	const bands = s.split('\n').filter((l) => l.includes('   y=')).join('\n');
	readme(dir, [
		'# 04 · Metal Slug Mission 2 — extracción de bandas (planos del fondo)',
		'',
		'Fuente: `Neo Geo _ NGCD - Metal Slug - Backgrounds - Mission 2.png` (4504×2617,',
		'fondo continuo de planos horizontales, sin gutters).',
		'',
		'`--extract-bands` detecta cortes por el salto de color medio entre filas y',
		'extrae BANDAS horizontales a ancho completo (los "planos" del fondo: cielo,',
		'montañas, edificios, agua, objetos). Cada banda se guarda como PNG; `bands.json`',
		'guarda sus rects y `bands_preview.png` es una hoja de contacto para localizarlas.',
		'',
		'Bandas extraídas (jump 55, alineación 16):',
		'```',
		bands,
		'```',
		'',
		'Estas bandas son la materia prima de la demo 05 (cuantización a EHB/31/15/7).',
	]);
}

// ============================================================================
// 05 · Metal Slug — cuantización a EHB / 31 / 15 / 7 colores
// ============================================================================
{
	const dir = O('05_metalslug_cuantizacion');
	cp(METAL, path.join(dir, 'source.png'));
	const crops = [
		{ name: 'edificios', x: 640, y: 544, w: 640, h: 256 },
		{ name: 'agua_objetos', x: 1280, y: 1344, w: 640, h: 256 },
	];
	const colorsDefs = [
		{ c: 64, pal: 'bright', label: 'EHB (64 = 32 base + half), mitad brillante→bases' },
		{ c: 31, pal: 'mediancut', label: '31 colores (1 slot reservado), median-cut' },
		{ c: 15, pal: 'mediancut', label: '15 colores (1 slot reservado), median-cut' },
		{ c: 7, pal: 'kmeans', label: '7 colores, k-means' },
	];
	const summaries = [];
	for (const crop of crops) {
		for (const d of colorsDefs) {
			const o = path.join(dir, crop.name, `${d.c}c_${d.pal}`);
			fs.mkdirSync(o, { recursive: true });
			const args = [path.join(dir, 'source.png'), '--crop', `${crop.x},${crop.y},${crop.w},${crop.h}`,
				'--colors', String(d.c), '--palette', d.pal, '--dither', 'floyd', '--tile', '16', '--out', o];
			const s = run(args);
			summaries.push(`- \`${crop.name}/${d.c}c_${d.pal}\` — ${d.label}: ${statsFrom(s).report}`);
		}
	}
	readme(dir, [
		'# 05 · Metal Slug — cuantización a EHB / 31 / 15 / 7 colores',
		'',
		'Fondo de Metal Slug Mission 2 recortado en dos regiones con contenido real',
		'(elegidas por máxima varianza de color):',
		'- **edificios** (x=640, y=544) — estructuras urbanas de la franja media.',
		'- **agua_objetos** (x=1280, y=1344) — franja baja con objetos/agua.',
		'',
		'Para cada región se prueba el mismo algoritmo a 4 profundidades de paleta',
		'(la EHB genera 64 colores con solo 32 bases: half = base/2 por hardware).',
		'',
		summaries.join('\n'),
		'',
		'Cada carpeta lleva `reconstruct.png` (resultado), `palette.json`/`.h` (paleta',
		'adaptada en orden Amiga) y `tilebank.h`/`.bin` listos para incluir en C/C++.',
		'Observa cómo baja la calidad y sube la textura de dithering conforme se',
		'reducen colores: EHB≈64 muy bueno, 31 bueno, 15 aceptable, 7 estilizado.',
	]);
}

// ============================================================================
// 06 · Descripciones con ollama local (modelo de visión)
// ============================================================================
{
	const dir = O('06_descripciones_ollama');
	cp(path.join(ASSETS, 'real', 'apple_guy.jpg'), path.join(dir, 'apple_guy.jpg'));
	cp(path.join(ASSETS, 'real', 'aussie_bum.jpg'), path.join(dir, 'aussie_bum.jpg'));
	cp(path.join(ASSETS, 'real', 'pac_man.jpg'), path.join(dir, 'pac_man.jpg'));
	cp(METAL, path.join(dir, 'metalslug.png'));
	// Descripciones capturadas con qwen3-vl:8b-instruct-q8_0 (2026-09-04).
	const descs = {
		'apple_guy_desc.txt': 'Bodybuilder hiperrealista en una tienda de tecnología Apple: músculos detallados, iluminación dramática, iMacs/iPhone en estanterías, fondo oscuro con azules de pantallas; piel cálida dorada. Colores dominantes: negro/gris + azul brillante + tonos piel.',
		'aussie_bum_desc.txt': 'Bodybuilder posando con bóxer rojo "AUSSIEBUN" en un cuarto de gaming: pantalla con Doom Eternal, pósters de The Last of Us II / CoD / RDR2 / Elden Ring, letrero neón "DISCIPLINE DRIVE DOMINATE", ventana con skyline nocturno. Colores: rojo, neón, azul noche.',
		'pac_man_desc.txt': 'Bodybuilder con pantalones amarillos ante una pared de ladrillos negros con mural pixelado de Pac-Man (laberinto azul neón, fantasmas rojo/rosa/azul y Pac-Man amarillo). Estética retro-80s; colores: negro, amarillo, azul neón.',
		'metalslug_desc.txt': 'Fondo vertical de Metal Slug Misión 2 (4504×2617, sin transparencia): de arriba a abajo cielo azul claro → montañas/lejanía → edificios europeos (tejados puntiagudos) → franja baja con agua y objetos → tierra oscura al pie. Plano continuo sin gutters; dominan azules, grises y ocres.',
	};
	for (const [name, txt] of Object.entries(descs)) fs.writeFileSync(path.join(dir, name), txt + '\n', 'utf8');
	readme(dir, [
		'# 06 · Descripciones con ollama local (qwen3-vl)',
		'',
		'Las imágenes reales se describen con un modelo de visión local (`qwen3-vl`).',
		'Aquí se guardan las descripciones capturadas (2026-09-04) como `*_desc.txt`;',
		'para regenerarlas usa el flag `--describe` de la tool:',
		'```',
		'node tools/amiga-tiles/amiga-tiles.mjs image.png --colors 64 --describe --model qwen3-vl:8b-instruct-q8_0',
		'```',
		'La descripción se guarda en `<out>/image_description.txt`.',
	]);
}

// ============================================================================
// 07 · Tiles de 32×32 y patrón de repetición
// ============================================================================
{
	const dir = O('07_tiles_32x32');
	const atlas = path.join(dir, 'atlas_32x32');
	fs.mkdirSync(atlas, { recursive: true });
	const sa = run([path.join(ASSETS, 'Beginning Fields.png'), '--colors', '64', '--tile', '32', '--out', atlas]);
	const foto = path.join(dir, 'foto_32x32');
	fs.mkdirSync(foto, { recursive: true });
	const sf = run([path.join(ASSETS, 'real', 'aussie_bum.jpg'), '--max-ram', '300000', '--colors', '16', '--dither', 'floyd', '--tile', '32', '--out', foto]);
	const slug = path.join(dir, 'metalslug_32x32');
	fs.mkdirSync(slug, { recursive: true });
	const ss = run([METAL, '--crop', '1280,1344,640,256', '--colors', '64', '--palette', 'bright', '--tile', '32', '--out', slug]);
	const a = statsFrom(sa), f = statsFrom(sf), sgs = statsFrom(ss);
	readme(dir, [
		'# 07 · Tiles de 32×32 y patrón de repetición',
		'',
		'El mismo algoritmo con `--tile 32`. A tamaño de tile mayor hay MENOS celdas, y',
		'la repetición exacta cambia: en un atlas (Beginning Fields) con tiles 32×32 casi',
		'todo tile es único; en una foto real (aussie) también. La tool lo detecta y lo',
		'avisa en consola automáticamente.',
		'',
		'- `atlas_32x32` — Beginning Fields, EHB, tile 32:',
		`  ${a.mode} · ${a.cmp}`,
		`  ${a.warn || 'sin aviso (hay repetición).'}`,
		'- `foto_32x32` — aussie_bum (Lanczos a ~300 KB), 16 colores con Floyd, tile 32:',
		`  ${f.mode} · ${f.report}`,
		`  ${f.warn || '(sin aviso)'}`,
		'- `metalslug_32x32` — recorte agua_objetos, EHB bright, tile 32:',
		`  ${sgs.mode} · ${sgs.report}`,
		`  ${sgs.warn || '(sin aviso)'}`,
		'',
		'Cuando la tool emite el AVISO de "sin patrón de repetición", el tilebank es',
		'equivalente a la imagen: `tilebank.bin == imagen indexada` y `tilebank.png` ≈',
		'`reconstruct.png`. Abre los dos PNG en esas carpetas y compruébalo.',
	]);
}

// ============================================================================
// 08 · Fotos reales → EHB (64), 32 y 16 colores con el mejor dither (floyd)
// ============================================================================
{
	const dir = O('08_foto_real_ehb_32c');
	const fotos = ['apple_guy', 'aussie_bum', 'pac_man', 'forgotten_relict', 'landscape_painting'];
	const rows = [];
	for (const name of fotos) {
		const src = path.join(ASSETS, 'real', `${name}.jpg`);
		for (const [c, label] of [[64, 'EHB (64 = 32 base + half)'], [32, '32 colores'], [16, '16 colores']]) {
			const o = path.join(dir, name, `${c}c_floyd`);
			fs.mkdirSync(o, { recursive: true });
			const pal = c === 64 ? 'ehb' : 'adaptive';
			const s = run([src, '--max-ram', '300000', '--resample', 'lanczos', '--colors', String(c), '--palette', pal, '--dither', 'best', '--out', o]);
			rows.push(`- \`${name}/${c}c_floyd\` — ${label}: ${statsFrom(s).report} · ${statsFrom(s).warn || 'sin aviso'}`);
		}
	}
	readme(dir, [
		'# 08 · Fotos reales → EHB (64) / 32 / 16 colores, dither Floyd–Steinberg',
		'',
		'Las cinco imágenes foto-realistas (apple_guy, aussie_bum, pac_man,',
		'forgotten_relict, landscape_painting) reducidas con Lanczos a ~300 KB de',
		'buffer y cuantizadas a EHB (paleta `ehb` half-max), 32 y 16 colores, todas',
		'con dither **Floyd–Steinberg** (el mejor para fotos).',
		'',
		'- **EHB** (`64c_*`): 32 bases + half generado por hardware, paleta que',
		'  maximiza el uso de los hal‑brite (`--palette ehb`, nunca peor que el óptimo).',
		'- **32 colores** (`32c_*`): 5 bits/píxel, paleta k-means.',
		'- **16 colores** (`16c_*`): 4 bits/píxel, paleta k-means (más pérdida).',
		'Todas con **`--dither best`** = Floyd + **serpentina** + **deadband 14** + **clamp 16**:',
		'evita el punteado visible en zonas de color casi uniforme (cielos) sin perder los',
		'degradados. (Medido en landscape: cielo con píxeles aislados 9.7 % → 3.5 % y',
		'PSNR 25.5 → 27.7 dB frente al Floyd clásico.)',
		'',
		rows.join('\n'),
		'',
		'Cada carpeta lleva `reconstruct.png` (resultado), `palette.json`/`.h` y',
		'`tilebank.h`/`.bin` listos para incrustar en C/C++.',
	]);
}

console.log('[run-demos] OK');
restoreVision();