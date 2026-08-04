#!/usr/bin/env node
/**
 * Comprobador generico de captura de pantalla de una demo del engine.
 *
 * Sustituye a analyze_demo_screenshot.py (Pillow) por una version Node/pngjs.
 * Muestrea la captura contando pixeles blancos, oscuros, no-azul-Workbench y
 * (opcionalmente) verdes y amarillos, y valida que el run-report.json adjunto
 * corresponde a la demo esperada y que su runStatus llego a Ready (state 3).
 *
 * Uso: node dist/tools/analyze/analyze_demo_screenshot.js --image <png>
 *      --demo <nombre> [--min-white N] [--min-dark N] [--min-nonblue N]
 *      [--need-green] [--need-yellow]
 */
import * as fs from 'fs';
import * as path from 'path';
import { argValue, hasFlag, fail } from '../lib/cli.js';
import { readPng } from '../lib/image.js';

interface ColorCounts {
	width: number;
	height: number;
	white: number;
	dark: number;
	nonBlue: number;
	green: number;
	yellow: number;
}

/** Cuenta muestras blancas, oscuras, no-WorkbenchBlue, verdes y amarillas. */
function sampleColors(imagePath: string): ColorCounts {
	const image = readPng(imagePath);
	const { width, height, data } = image;
	const stepX = Math.max(1, Math.floor(width / 220));
	const stepY = Math.max(1, Math.floor(height / 160));
	const counts: ColorCounts = { width, height, white: 0, dark: 0, nonBlue: 0, green: 0, yellow: 0 };
	for (let y = 0; y < height; y += stepY) {
		for (let x = 0; x < width; x += stepX) {
			const i = (y * width + x) * 4;
			const r = data[i];
			const g = data[i + 1];
			const b = data[i + 2];
			if (r > 210 && g > 210 && b > 210) counts.white++;
			if (r < 24 && g < 24 && b < 24) counts.dark++;
			if (g > 160 && r < 80 && b < 180) counts.green++;
			if (r > 180 && g > 180 && b < 120) counts.yellow++;
			const isWorkbenchBlue = Math.abs(r - 0) <= 28 && Math.abs(g - 85) <= 28 && Math.abs(b - 170) <= 28;
			if (!isWorkbenchBlue) counts.nonBlue++;
		}
	}
	return counts;
}

/** Valida el run-report.json junto a la captura (demo y estado Ready). */
function checkRunReport(imagePath: string, expectedDemo: string): void {
	const runReportPath = path.join(path.dirname(imagePath), 'run-report.json');
	if (!fs.existsSync(runReportPath)) {
		fail(`No existe run-report.json junto a la captura.`);
	}
	const report = JSON.parse(fs.readFileSync(runReportPath, 'utf-8'));
	if (report.demo !== expectedDemo) {
		fail(`run-report.json no corresponde a ${expectedDemo} (demo=${report.demo}).`);
	}
	const sideChannel = report.finalSideChannel && report.finalSideChannel.ok
		? report.finalSideChannel
		: (report.sideChannel || {}).value;
	const state = sideChannel ? Number(sideChannel.state) : -1;
	if (state !== 3) {
		fail(`runStatus.state no esta en Ready para ${expectedDemo} (state=${state}).`);
	}
}

function main(): void {
	const args = process.argv.slice(2);
	const imageValue = argValue(args, '--image');
	const demo = argValue(args, '--demo');
	if (!imageValue || !demo) {
		fail('Uso: analyze_demo_screenshot.js --image <png> --demo <nombre> [opciones]');
	}
	const imagePath = path.resolve(imageValue);
	if (!fs.existsSync(imagePath)) {
		fail(`No existe la captura: ${imagePath}`);
	}

	const minWhite = parseInt(argValue(args, '--min-white', '10')!, 10);
	const minDark = parseInt(argValue(args, '--min-dark', '10')!, 10);
	const minNonBlue = parseInt(argValue(args, '--min-nonblue', '100')!, 10);
	const needGreen = hasFlag(args, '--need-green');
	const needYellow = hasFlag(args, '--need-yellow');

	const counts = sampleColors(imagePath);
	if (counts.width < 320 || counts.height < 200) {
		fail(`Captura demasiado pequena: ${counts.width}x${counts.height}`);
	}

	checkRunReport(imagePath, demo);

	const failures: string[] = [];
	if (counts.white < minWhite) failures.push(`white=${counts.white} < ${minWhite}`);
	if (counts.dark < minDark) failures.push(`dark=${counts.dark} < ${minDark}`);
	if (counts.nonBlue < minNonBlue) failures.push(`nonBlue=${counts.nonBlue} < ${minNonBlue}`);
	if (needGreen && counts.green < 1) failures.push(`green=${counts.green} < 1`);
	if (needYellow && counts.yellow < 1) failures.push(`yellow=${counts.yellow} < 1`);

	if (failures.length > 0) {
		fail(`FAIL ${demo}: ${failures.join('; ')}`);
	}

	console.log(`OK ${demo} ${counts.width}x${counts.height} ` +
		`white=${counts.white} dark=${counts.dark} nonBlue=${counts.nonBlue}`);
}

main();
