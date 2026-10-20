#!/usr/bin/env node
// Regression checks for HUNK_HEADER parsing, A1200 `.data` resolution and the A500 strict-mode gate.
import assert from 'node:assert/strict';
import {
  isStrictReadyEnabled,
  readHunkCountFromBytes,
  resolveStrictDataHunkAddress,
} from '../../dist/tools/run/run-status-resolver.js';

function makeHunkHeader(count, residentName = true) {
  const words = [0x3f3];
  if (residentName) words.push(1, 0x54455354); // one longword: ASCII "TEST"
  words.push(0, count, 0, count - 1);
  for (let i = 0; i < count; ++i) words.push(0x100 + i);
  const bytes = Buffer.alloc(words.length * 4);
  words.forEach((word, index) => bytes.writeUInt32BE(word >>> 0, index * 4));
  return bytes;
}

assert.equal(readHunkCountFromBytes(makeHunkHeader(7)), 7, 'reads resident names and seven hunks');
assert.equal(readHunkCountFromBytes(makeHunkHeader(7, false)), 7, 'reads an empty resident-name table');
assert.equal(readHunkCountFromBytes(Buffer.from([0, 0, 0, 0])), 0, 'rejects a non-HUNK file');
assert.equal(readHunkCountFromBytes(Buffer.from([0, 0, 3, 0xf3, 0, 0, 0, 2])), 0, 'rejects a truncated resident name');
assert.equal(readHunkCountFromBytes(Buffer.from([0, 0, 3, 0xf3, 0, 0, 0, 0, 0, 0, 0, 7])), 0, 'rejects a truncated hunk table');

const linkedSymbol = 0x381be;
const mapSections = [
  { name: '.text', start: 0x400, end: 0x11000 },
  { name: '.data', start: 0x381a8, end: 0x381d8 },
  { name: '.bss', start: 0x381d8, end: 0x41500 },
];
const runtimeSections = [
  '0x43b88', '0x23cd8', '0x24d10', '0x26798', '0x2fe58', '0x2fe68', '0x2fea8',
];
assert.deepEqual(
  resolveStrictDataHunkAddress(linkedSymbol, mapSections, runtimeSections, 7),
  { hunkCount: 7, dataIndex: 5, dataBase: 0x2fe68, address: 0x2fe7e },
  'resolves the run-status offset in the penultimate `.data` hunk, not BSS',
);
assert.equal(resolveStrictDataHunkAddress(0x381d8, mapSections, runtimeSections, 7), null, 'does not resolve a BSS symbol');
assert.equal(resolveStrictDataHunkAddress(linkedSymbol, mapSections, runtimeSections.slice(0, 5), 7), null, 'rejects a missing data hunk');
assert.equal(resolveStrictDataHunkAddress(linkedSymbol, mapSections, runtimeSections, 1), null, 'rejects an invalid hunk count');

assert.equal(isStrictReadyEnabled('1'), true, 'A1200 wrapper enables strict READY');
assert.equal(isStrictReadyEnabled(undefined), false, 'default A500 runner keeps legacy resolution');
assert.equal(isStrictReadyEnabled('0'), false, 'other values keep legacy resolution');

console.log('[run-status-resolver] OK: HUNK header, .data mapping and A500 gate');
