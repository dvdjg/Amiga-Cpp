'use strict';
// Stub minimo del modulo 'vscode' para ejecutar el adaptador de depuracion
// Amiga FUERA del host de extensiones (harness DAP standalone).
// Copiar a <fork-vscode-amiga-debug>/node_modules/vscode/index.js
const path = require('path');

class EventEmitter {
	constructor() { this._listeners = new Map(); }
	event(l) { return this.subscribe(l); }
	subscribe(l) {
		const k = (this._next = (this._next || 0) + 1);
		if (!this._listeners.has(k)) this._listeners.set(k, l);
		return new Disposable(() => this._listeners.delete(k));
	}
	fire(d) { for (const l of Array.from(this._listeners.values())) { try { l(d); } catch (e) {} } }
	dispose() { this._listeners.clear(); }
}
class Disposable { constructor(fn) { this._fn = fn; } dispose() { if (this._fn) { this._fn(); this._fn = null; } } }
class Uri { constructor(fsPath) { this.scheme = 'file'; this.fsPath = fsPath; this.path = fsPath; } static file(f) { return new Uri(f); } static parse(s) { return new Uri(s); } toString() { return this.fsPath; } }
class Position { constructor(l, c) { this.line = l; this.character = c; } }
class Range { constructor(sl, sc, el, ec) { this.start = new Position(sl, sc); this.end = new Position(el, ec); } }
class Diagnostic { constructor(range, message, severity) { this.range = range; this.message = message; this.severity = severity; } }
class TreeItem { constructor(label, state) { this.label = label; this.collapsibleState = state; } }
class CompletionItem { constructor(label, kind) { this.label = label; this.kind = kind; } }
class Hover { constructor(c) { this.contents = c; } }
class MarkdownString { constructor(v) { this.value = v; } }
class ThemeColor { constructor(id) { this.id = id; } }
class Location { constructor(uri, range) { this.uri = uri; this.range = range; } }
class TextEditorDecorationType { dispose() {} }

const commands = {
	_registered: {},
	registerCommand(id, fn) { this._registered[id] = fn; return new Disposable(); },
	async executeCommand(id, ...args) {
		if (id === 'amiga.bin-path') {
			const p = process.env.AMIGA_TEST_BIN;
			if (p) return p;
			return path.join(process.env.USERPROFILE, '.vscode', 'extensions', 'bartmanabyss.amiga-debug-1.8.1', 'bin', process.platform);
		}
		if (this._registered[id]) return this._registered[id](...args);
		return undefined;
	},
	getCommands() { return Promise.resolve(Object.keys(this._registered)); }
};
class StubProgress { report() {} }
class StubCancellationToken { get isCancellationRequested() { return false; } onCancellationRequested() { return new Disposable(); } }
const window = {
	createOutputChannel() { return { append() {}, appendLine() {}, clear() {}, show() {}, hide() {}, dispose() {} }; },
	createStatusBarItem() { return { show() {}, hide() {}, dispose() {}, text: '', command: '', tooltip: '' }; },
	createTextEditorDecorationType() { return new TextEditorDecorationType(); },
	withProgress(_o, task) { return task(new StubProgress(), new StubCancellationToken()); },
	async showInformationMessage(m) { return m; }, async showErrorMessage(m) { return m; }, async showWarningMessage(m) { return m; },
	setStatusBarMessage() { return new Disposable(); },
	registerWebviewPanelSerializer() { return new Disposable(); },
	registerWebviewViewProvider() { return new Disposable(); },
	createTreeView() { return { dispose() {} }; }, registerTreeDataProvider() { return new Disposable(); },
	createTerminal() { return { show() {}, dispose() {}, sendText() {} }; },
	onDidChangeActiveTextEditor() { return new Disposable(); }, onDidCloseTerminal() { return new Disposable(); },
	onDidChangeVisibleTextEditors() { return new Disposable(); }, activeTextEditor: undefined, visibleTextEditors: [], terminals: [],
	showTextDocument() { return Promise.resolve(); }
};
const workspace = {
	workspaceFolders: [{ uri: Uri.file(process.cwd()) }],
	getConfiguration() { return { get() { return undefined; }, update() { return Promise.resolve(); } }; },
	registerTextDocumentContentProvider() { return new Disposable(); },
	onDidChangeConfiguration() { return new Disposable(); }, onDidOpenTextDocument() { return new Disposable(); },
	onDidCloseTextDocument() { return new Disposable(); }, onDidChangeTextDocument() { return new Disposable(); },
	textDocuments: [], openTextDocument() { return Promise.resolve(); }, fileSystem: { readFile() { return Promise.resolve(new Uint8Array(0)); } },
	registerCodeLensProvider() { return new Disposable(); }, registerInlineValuesProvider() { return new Disposable(); }
};
const languages = {
	createDiagnosticCollection() { return { set() {}, delete() {}, clear() {}, dispose() {} }; }, match() { return 0; },
	registerCodeLensProvider() { return new Disposable(); }, registerDocumentSemanticTokensProvider() { return new Disposable(); },
	registerCompletionItemProvider() { return new Disposable(); }, registerHoverProvider() { return new Disposable(); },
	registerDocumentSymbolProvider() { return new Disposable(); }, registerDefinitionProvider() { return new Disposable(); },
	registerInlineValuesProvider() { return new Disposable(); }, registerCodeActionsProvider() { return new Disposable(); }
};
const extensions = { getExtension() { return undefined; }, all: [], onDidChange() { return new Disposable(); } };
const debug = {
	activeDebugSession: undefined, activeDebugConsole: { append() {}, appendLine() {} },
	registerDebugConfigurationProvider() { return new Disposable(); }, registerDebugAdapterDescriptorFactory() { return new Disposable(); },
	registerDebugAdapterTrackerFactory() { return new Disposable(); }, onDidStartDebugSession() { return new Disposable(); },
	onDidTerminateDebugSession() { return new Disposable(); }, onDidChangeActiveDebugSession() { return new Disposable(); },
	startDebugging() { return Promise.resolve(true); }
};
const env = { machineId: 'stub', sessionId: 'stub', appRoot: __dirname, uiKind: 1, shell: 'cmd', clipboard: { writeText() { return Promise.resolve(); } }, asExternalUri() { return Promise.resolve(Uri.file('')); } };
const ProgressLocation = { Notification: 15, Window: 10 };
const TreeItemCollapsibleState = { None: 0, Collapsed: 1, Expanded: 2 };
const SymbolKind = { Function: 12 }; const CompletionItemKind = { Function: 3, Keyword: 5 };
const DiagnosticSeverity = { Error: 0, Warning: 1, Information: 2, Hint: 3 };
const OverviewRulerLane = { Left: 1, Center: 2, Right: 4, Full: 7 };
const SemanticTokensLegend = class { constructor(tt, tm) { this.tokenTypes = tt; this.tokenModifiers = tm; } };
const SemanticTokens = class { constructor(b) { this.resultId = ''; this.data = b.data; } };
const SemanticTokensBuilder = class { constructor() { this.data = []; } push(l, c, len, type, mod) { this.data.push(l, c, len, type, mod || 0); } };

module.exports = {
	EventEmitter, Disposable, Uri, Range, Position, Diagnostic, TreeItem, CompletionItem, Hover, MarkdownString,
	ThemeColor, Location, TextEditorDecorationType, ProgressLocation, TreeItemCollapsibleState, SymbolKind,
	CompletionItemKind, DiagnosticSeverity, OverviewRulerLane, SemanticTokens, SemanticTokensLegend, SemanticTokensBuilder,
	commands, window, workspace, languages, extensions, debug, env,
	TextDocumentContentProvider: class {}, CancellationToken: StubCancellationToken
};
