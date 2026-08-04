#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
const imageMimeByExtension = new Map([
    ['.png', 'image/png'],
    ['.jpg', 'image/jpeg'],
    ['.jpeg', 'image/jpeg'],
    ['.bmp', 'image/bmp'],
]);
function parseArgs(argv) {
    const args = {};
    for (let i = 0; i < argv.length; ++i) {
        const token = argv[i];
        if (!token.startsWith('--')) {
            continue;
        }
        const key = token.slice(2);
        const next = argv[i + 1];
        if (!next || next.startsWith('--')) {
            args[key] = true;
        }
        else {
            args[key] = next;
            ++i;
        }
    }
    return args;
}
function ensureDir(dir) {
    fs.mkdirSync(dir, { recursive: true });
}
function readJsonIfExists(file) {
    if (!file || !fs.existsSync(file)) {
        return null;
    }
    return JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
}
function readJsonRequired(file) {
    if (!file || !fs.existsSync(file)) {
        throw new Error(`No existe el JSON requerido: ${file}`);
    }
    return JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
}
function listImageFrames(sourceDir) {
    const extensions = new Set(['.png', '.jpg', '.jpeg', '.bmp']);
    const files = fs.readdirSync(sourceDir, { withFileTypes: true })
        .filter((entry) => entry.isFile())
        .map((entry) => path.join(sourceDir, entry.name))
        .filter((file) => extensions.has(path.extname(file).toLowerCase()))
        .sort((a, b) => path.basename(a).localeCompare(path.basename(b)));
    const frameNamed = files.filter((file) => /^frame_\d+/i.test(path.basename(file)));
    return frameNamed.length > 0 ? frameNamed : files;
}
function parseFrameList(value) {
    if (!value) {
        return [];
    }
    return value.split(',')
        .map((item) => Number.parseInt(item.trim(), 10))
        .filter((value) => Number.isInteger(value) && value >= 0);
}
function decodeRunStatus(runStatus) {
    if (!runStatus?.ok) {
        return null;
    }
    const detail = Number(runStatus.detail >>> 0);
    return {
        ok: true,
        frame: Number(runStatus.frame ?? 0),
        detail: `0x${detail.toString(16).padStart(8, '0')}`,
        cameraX: (detail >>> 16) & 0xff,
        cameraY: (detail >>> 8) & 0xff,
        tileJobs: (detail >>> 4) & 0x0f,
        prefetchFlags: detail & 0x0f,
    };
}
function uniqueSorted(values, maxExclusive) {
    return [...new Set(values)]
        .filter((value) => Number.isInteger(value) && value >= 0 && value < maxExclusive)
        .sort((a, b) => a - b);
}
function windowAround(index, before, after, maxExclusive) {
    const values = [];
    for (let i = index - before; i <= index + after; ++i) {
        values.push(i);
    }
    return uniqueSorted(values, maxExclusive);
}
function findFrameScopeMismatch(frameScopeReport) {
    const mismatches = frameScopeReport?.ProfileReport?.Mismatches;
    if (Array.isArray(mismatches) && mismatches.length > 0) {
        const first = mismatches[0];
        if (Number.isInteger(first.From)) {
            return first.From;
        }
        if (Number.isInteger(first.from)) {
            return first.from;
        }
    }
    return -1;
}
function selectAmigaScrollTransition(runReport, frameScopeReport, frameCount) {
    const mismatchIndex = findFrameScopeMismatch(frameScopeReport);
    if (mismatchIndex >= 0) {
        return {
            reason: 'framescope-mismatch',
            frames: windowAround(mismatchIndex + 1, 2, 2, frameCount),
        };
    }
    const sequenceFrames = runReport?.sequence?.frames;
    if (!Array.isArray(sequenceFrames) || sequenceFrames.length === 0) {
        return {
            reason: 'fallback-no-run-report-sequence',
            frames: uniqueSorted([0, 1, 2, 3], frameCount),
        };
    }
    const telemetry = sequenceFrames.map((frame) => decodeRunStatus(frame.runStatus));
    for (let i = 1; i < telemetry.length; ++i) {
        const prev = telemetry[i - 1];
        const cur = telemetry[i];
        if (!prev || !cur) {
            continue;
        }
        const prevCoarse = Math.floor(prev.cameraX / 16);
        const curCoarse = Math.floor(cur.cameraX / 16);
        if (prevCoarse !== curCoarse) {
            return {
                reason: `coarse-x-change-${prevCoarse}-to-${curCoarse}`,
                frames: windowAround(i, 2, 2, frameCount),
            };
        }
    }
    for (let i = 0; i < telemetry.length; ++i) {
        const cur = telemetry[i];
        if (!cur) {
            continue;
        }
        const fine = cur.cameraX & 15;
        if (fine === 14 || fine === 15 || fine === 0 || fine === 1) {
            return {
                reason: `fine-x-near-boundary-${fine}`,
                frames: windowAround(i, 2, 2, frameCount),
            };
        }
    }
    return {
        reason: 'fallback-first-four',
        frames: uniqueSorted([0, 1, 2, 3], frameCount),
    };
}
function selectFrames({ explicitFrames, profile, runReport, frameScopeReport, frameCount }) {
    if (explicitFrames.length > 0) {
        return {
            reason: 'manual',
            frames: uniqueSorted(explicitFrames, frameCount),
        };
    }
    if (profile === 'amiga-scroll-transition') {
        return selectAmigaScrollTransition(runReport, frameScopeReport, frameCount);
    }
    return {
        reason: 'fallback-first-four',
        frames: uniqueSorted([0, 1, 2, 3], frameCount),
    };
}
function loadPrompt(rootDir, profile) {
    const promptPath = path.join(rootDir, 'tools', 'vision-review', 'prompts', `${profile}.md`);
    if (!fs.existsSync(promptPath)) {
        throw new Error(`No existe el prompt del perfil: ${promptPath}`);
    }
    return {
        path: promptPath,
        text: fs.readFileSync(promptPath, 'utf8'),
    };
}
function copySelectedFrames(frameFiles, selectedIndexes, framesDir) {
    ensureDir(framesDir);
    return selectedIndexes.map((sourceIndex, packageIndex) => {
        const sourcePath = frameFiles[sourceIndex];
        const extension = path.extname(sourcePath).toLowerCase();
        const targetName = `review_${String(packageIndex).padStart(3, '0')}_source_${String(sourceIndex).padStart(3, '0')}${extension}`;
        const targetPath = path.join(framesDir, targetName);
        fs.copyFileSync(sourcePath, targetPath);
        return {
            packageIndex,
            sourceIndex,
            sourcePath,
            path: targetPath,
            file: targetName,
        };
    });
}
function writeRequestMarkdown(request, markdownPath) {
    const lines = [];
    lines.push(`# Vision Review request`);
    lines.push('');
    lines.push(`Profile: ${request.profile}`);
    lines.push(`SelectionReason: ${request.selection.reason}`);
    lines.push(`Source: ${request.source}`);
    lines.push('');
    lines.push(`## Frames`);
    for (const frame of request.frames) {
        lines.push(`- package ${frame.packageIndex}: source frame ${frame.sourceIndex}, file \`${frame.file}\``);
        if (frame.telemetry) {
            lines.push(`  camera=${frame.telemetry.cameraX},${frame.telemetry.cameraY}, programFrame=${frame.telemetry.frame}, detail=${frame.telemetry.detail}`);
        }
    }
    lines.push('');
    lines.push(`## User prompt`);
    lines.push('');
    lines.push(request.userQuestion);
    lines.push('');
    lines.push(`## Profile prompt`);
    lines.push('');
    lines.push(request.prompt.text);
    fs.writeFileSync(markdownPath, `${lines.join('\n')}\n`, 'utf8');
}
function buildUserQuestion(profile, frames, selection) {
    if (profile === 'amiga-scroll-transition') {
        const firstTelemetry = frames.find((frame) => frame.telemetry)?.telemetry;
        const lastTelemetry = [...frames].reverse().find((frame) => frame.telemetry)?.telemetry;
        const expectedMotion = expectedAmigaContentMotion(firstTelemetry, lastTelemetry);
        return [
            'Compare these frames from an Amiga EHB tile scrolling demo.',
            `The frames were selected because: ${selection.reason}.`,
            'Hypothesis: the visual content should scroll continuously around the transition, without tile pop, sudden jump, tearing, planar corruption, or unexpected palette change.',
            `Expected visible content motion from telemetry: ${expectedMotion}.`,
            'Important direction convention: if cameraX increases, the world/content should move left on screen; if cameraY increases, the world/content should move up on screen.',
            'The images may be sampled several game frames apart; do not classify normal positional displacement as a jump unless the tile structure is discontinuous or corrupted.',
            'Focus especially on continuity around the central transition frames.',
        ].join('\n');
    }
    if (profile === 'sprite-animation') {
        return [
            'Compare these frames from a possible sprite animation in a retro game.',
            'State whether the main object changes pose coherently or appears frozen/corrupt.',
        ].join('\n');
    }
    return [
        'Compare these frames and describe important visible differences.',
        'Do not invent information that is not visible; mark uncertain points as unclear.',
    ].join('\n');
}
function expectedAmigaContentMotion(firstTelemetry, lastTelemetry) {
    if (!firstTelemetry || !lastTelemetry) {
        return 'unknown';
    }
    const dx = lastTelemetry.cameraX - firstTelemetry.cameraX;
    const dy = lastTelemetry.cameraY - firstTelemetry.cameraY;
    const horizontal = dx > 0 ? 'left' : dx < 0 ? 'right' : '';
    const vertical = dy > 0 ? 'up' : dy < 0 ? 'down' : '';
    if (horizontal && vertical) {
        return `${vertical}-${horizontal}`;
    }
    return horizontal || vertical || 'static';
}
function telemetryForFrames(runReport, selectedFrames) {
    const sequenceFrames = runReport?.sequence?.frames;
    if (!Array.isArray(sequenceFrames)) {
        return new Map();
    }
    const map = new Map();
    for (const sourceIndex of selectedFrames) {
        const decoded = decodeRunStatus(sequenceFrames[sourceIndex]?.runStatus);
        if (decoded) {
            map.set(sourceIndex, decoded);
        }
    }
    return map;
}
function defaultOutDir(rootDir, profile) {
    const stamp = new Date().toISOString().replace(/[-:]/g, '').replace(/\..+/, '').replace('T', '-');
    return path.join(rootDir, 'out', 'vision-review', `${profile}-${stamp}`);
}
function normalizeOpenAiBaseUrl(baseUrl) {
    let normalized = String(baseUrl ?? '').trim().replace(/\/+$/, '');
    if (!normalized) {
        throw new Error('El proveedor no define baseUrl');
    }
    if (!normalized.endsWith('/v1')) {
        normalized = `${normalized}/v1`;
    }
    return normalized;
}
function providerApiKey(provider) {
    if (provider.apiKeyEnv) {
        return process.env[provider.apiKeyEnv] ?? '';
    }
    return provider.apiKey ?? '';
}
function imageToDataUrl(file) {
    const extension = path.extname(file).toLowerCase();
    const mime = imageMimeByExtension.get(extension);
    if (!mime) {
        throw new Error(`Extension de imagen no soportada para envio al proveedor: ${file}`);
    }
    const bytes = fs.readFileSync(file);
    return `data:${mime};base64,${bytes.toString('base64')}`;
}
function extractJsonFromText(text) {
    const trimmed = String(text ?? '').trim();
    if (!trimmed) {
        throw new Error('El modelo no devolvio texto');
    }
    const fenced = trimmed.match(/```(?:json)?\s*([\s\S]*?)```/i);
    const candidate = fenced ? fenced[1].trim() : trimmed;
    try {
        return JSON.parse(candidate);
    }
    catch {
        const first = candidate.indexOf('{');
        const last = candidate.lastIndexOf('}');
        if (first >= 0 && last > first) {
            return JSON.parse(candidate.slice(first, last + 1));
        }
        throw new Error(`No se pudo extraer JSON valido de la respuesta del modelo: ${trimmed.slice(0, 300)}`);
    }
}
function buildProviderContent(request, sendMode) {
    const lines = [
        request.prompt.text,
        '',
        'Task-specific context:',
        request.userQuestion,
        '',
        'Frame metadata:',
    ];
    for (const frame of request.frames) {
        const telemetry = frame.telemetry
            ? ` camera=${frame.telemetry.cameraX},${frame.telemetry.cameraY} programFrame=${frame.telemetry.frame} detail=${frame.telemetry.detail}`
            : '';
        lines.push(`- package frame ${frame.packageIndex}, source frame ${frame.sourceIndex}.${telemetry}`);
    }
    lines.push('');
    lines.push('Return only strict JSON matching the requested schema.');
    const content = [{ type: 'text', text: lines.join('\n') }];
    if (sendMode === 'contact-sheet') {
        if (!request.contactSheet) {
            throw new Error('sendMode contact-sheet requiere contactSheet en request.json');
        }
        content.push({
            type: 'image_url',
            image_url: { url: imageToDataUrl(request.contactSheet) },
        });
        return content;
    }
    for (const frame of request.frames) {
        content.push({
            type: 'image_url',
            image_url: { url: imageToDataUrl(frame.path) },
        });
    }
    return content;
}
async function postJsonWithTimeout(url, body, headers, timeoutMs) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), timeoutMs);
    try {
        const response = await fetch(url, {
            method: 'POST',
            headers,
            body: JSON.stringify(body),
            signal: controller.signal,
        });
        const text = await response.text();
        if (!response.ok) {
            throw new Error(`HTTP ${response.status} ${response.statusText}: ${text.slice(0, 1000)}`);
        }
        return text ? JSON.parse(text) : {};
    }
    finally {
        clearTimeout(timeout);
    }
}
async function runOpenAiCompatibleReview(requestPath, providerPath, sendModeOverride) {
    const request = readJsonRequired(requestPath);
    const provider = readJsonRequired(providerPath);
    if (provider.kind !== 'openai-compatible') {
        throw new Error(`Proveedor no soportado todavia: ${provider.kind}`);
    }
    const sendMode = sendModeOverride || provider.sendMode || 'multi-image';
    const baseUrl = normalizeOpenAiBaseUrl(provider.baseUrl);
    const apiKey = providerApiKey(provider);
    const headers = {
        'Content-Type': 'application/json',
    };
    if (apiKey) {
        headers.Authorization = `Bearer ${apiKey}`;
    }
    const body = {
        model: provider.model,
        temperature: provider.temperature ?? 0.1,
        max_tokens: provider.maxTokens ?? 1200,
        messages: [
            {
                role: 'user',
                content: buildProviderContent(request, sendMode),
            },
        ],
    };
    if (provider.responseFormatJson) {
        body.response_format = { type: 'json_object' };
    }
    const raw = await postJsonWithTimeout(`${baseUrl}/chat/completions`, body, headers, provider.timeoutMs ?? 60000);
    const text = raw?.choices?.[0]?.message?.content ?? '';
    const parsed = extractJsonFromText(text);
    const report = {
        schemaVersion: 1,
        status: 'ok',
        provider: {
            name: provider.name,
            kind: provider.kind,
            baseUrl,
            model: provider.model,
            sendMode,
        },
        request: requestPath,
        generatedAt: new Date().toISOString(),
        result: parsed,
    };
    fs.writeFileSync(path.join(request.outDir, `raw-response-${sendMode}.json`), `${JSON.stringify(raw, null, 2)}\n`, 'utf8');
    fs.writeFileSync(path.join(request.outDir, `vision-review-report-${sendMode}.json`), `${JSON.stringify(report, null, 2)}\n`, 'utf8');
    fs.writeFileSync(path.join(request.outDir, `vision-review-report-${sendMode}.md`), [
        '# Vision Review report',
        '',
        `Provider: ${provider.name}`,
        `Model: ${provider.model}`,
        `SendMode: ${sendMode}`,
        `Status: ${parsed.status ?? 'unknown'}`,
        `Answer: ${parsed.answer ?? ''}`,
        `Confidence: ${parsed.confidence ?? ''}`,
        '',
        '```json',
        JSON.stringify(parsed, null, 2),
        '```',
        '',
    ].join('\n'), 'utf8');
    console.log(JSON.stringify({
        status: 'ok',
        sendMode,
        report: path.join(request.outDir, `vision-review-report-${sendMode}.json`),
        result: parsed,
    }, null, 2));
}
function main() {
    const args = parseArgs(process.argv.slice(2));
    if (args.reviewRequest) {
        return runOpenAiCompatibleReview(path.resolve(args.reviewRequest), path.resolve(args.provider), args.sendMode && args.sendMode !== true ? args.sendMode : '');
    }
    const rootDir = path.resolve(args.root ?? process.cwd());
    const source = args.source ? path.resolve(args.source) : '';
    if (!source) {
        throw new Error('Falta --source');
    }
    if (!fs.existsSync(source) || !fs.statSync(source).isDirectory()) {
        throw new Error(`--source debe ser una carpeta de frames: ${source}`);
    }
    const profile = args.profile ?? 'generic-frame-diff';
    const outDir = args.outDir ? path.resolve(args.outDir) : defaultOutDir(rootDir, profile);
    const frameFiles = listImageFrames(source);
    if (frameFiles.length === 0) {
        throw new Error(`No se encontraron imagenes en ${source}`);
    }
    const runReport = readJsonIfExists(args.runReport ? path.resolve(args.runReport) : '');
    const frameScopeReport = readJsonIfExists(args.frameScopeReport ? path.resolve(args.frameScopeReport) : '');
    const explicitFrames = parseFrameList(args.frames);
    const selection = selectFrames({
        explicitFrames,
        profile,
        runReport,
        frameScopeReport,
        frameCount: frameFiles.length,
    });
    if (selection.frames.length === 0) {
        throw new Error('La seleccion de frames quedo vacia');
    }
    ensureDir(outDir);
    const framesDir = path.join(outDir, 'frames');
    const copiedFrames = copySelectedFrames(frameFiles, selection.frames, framesDir);
    const prompt = loadPrompt(rootDir, profile);
    const telemetryByFrame = telemetryForFrames(runReport, selection.frames);
    const framesWithTelemetry = copiedFrames.map((frame) => ({
        ...frame,
        telemetry: telemetryByFrame.get(frame.sourceIndex) ?? null,
    }));
    const userQuestion = buildUserQuestion(profile, framesWithTelemetry, selection);
    const request = {
        schemaVersion: 1,
        status: 'offline-package',
        profile,
        source,
        outDir,
        generatedAt: new Date().toISOString(),
        selection,
        prompt: {
            path: prompt.path,
            text: prompt.text,
        },
        userQuestion,
        frames: framesWithTelemetry,
        provider: args.provider ? path.resolve(args.provider) : null,
        expectedResponse: 'JSON estricto segun el esquema del prompt del perfil.',
    };
    const requestJson = path.join(outDir, 'request.json');
    const requestMarkdown = path.join(outDir, 'request.md');
    fs.writeFileSync(requestJson, `${JSON.stringify(request, null, 2)}\n`, 'utf8');
    writeRequestMarkdown(request, requestMarkdown);
    const summary = {
        status: 'ok',
        outDir,
        requestJson,
        requestMarkdown,
        selectedFrames: selection.frames,
        selectionReason: selection.reason,
    };
    fs.writeFileSync(path.join(outDir, 'vision-review-summary.json'), `${JSON.stringify(summary, null, 2)}\n`, 'utf8');
    console.log(JSON.stringify(summary, null, 2));
}
try {
    await main();
}
catch (error) {
    console.error(error?.stack ?? String(error));
    process.exit(1);
}
