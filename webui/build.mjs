import { createHash } from 'node:crypto';
import {
  existsSync,
  mkdirSync,
  readFileSync,
  writeFileSync,
} from 'node:fs';
import { gzipSync, gunzipSync, constants as zlibConstants } from 'node:zlib';
import { dirname, join, relative, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

import { minify } from 'html-minifier-terser';

const WEBUI_DIR = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(WEBUI_DIR, '..');
const PREVIEW_DIR = join(ROOT, '.ap-portal-preview');
const GENERATED_DIR = join(WEBUI_DIR, 'generated');
const GENERATED_HEADER = join(
  ROOT,
  'Platformio',
  'Dog-RGB',
  'include',
  'web',
  'generated_assets.h',
);
const GENERATED_CPP = join(
  ROOT,
  'Platformio',
  'Dog-RGB',
  'src',
  'web',
  'generated_assets.cpp',
);
const MANIFEST = join(GENERATED_DIR, 'manifest.json');
const STYLE_MARKER = '<!-- DOG_RGB_INCLUDE:styles/app.css -->';
const HTML_MINIFIER_VERSION = '7.2.0';
export const TOTAL_GZIP_BUDGET = 55 * 1024;

export const PAGE_DEFINITIONS = Object.freeze([
  {
    key: 'root',
    route: '/',
    filename: 'index.html',
    symbol: 'ROOT_PAGE',
    budgetGzip: 12 * 1024,
  },
  {
    key: 'wifi',
    route: '/wifi',
    filename: 'wifi.html',
    symbol: 'WIFI_PAGE',
    budgetGzip: 13 * 1024,
  },
  {
    key: 'config',
    route: '/config',
    filename: 'config.html',
    symbol: 'CONFIG_PAGE',
    budgetGzip: 23 * 1024,
  },
  {
    key: 'dev',
    route: '/dev',
    filename: 'dev.html',
    symbol: 'DEV_PAGE',
    budgetGzip: 10 * 1024,
  },
]);

const INPUT_PATHS = Object.freeze([
  '.node-version',
  'package.json',
  'package-lock.json',
  'webui/build.mjs',
  'webui/src/styles/app.css',
  ...PAGE_DEFINITIONS.map(({ filename }) => `webui/src/pages/${filename}`),
]);

const MINIFY_OPTIONS = Object.freeze({
  caseSensitive: true,
  collapseBooleanAttributes: false,
  collapseInlineTagWhitespace: false,
  collapseWhitespace: true,
  conservativeCollapse: true,
  continueOnParseError: false,
  decodeEntities: false,
  keepClosingSlash: true,
  minifyCSS: {
    level: 1,
    rebase: false,
  },
  minifyJS: {
    compress: {
      passes: 2,
    },
    mangle: false,
    format: {
      comments: false,
    },
  },
  preventAttributesEscaping: true,
  processConditionalComments: false,
  removeAttributeQuotes: false,
  removeComments: true,
  removeEmptyAttributes: false,
  removeOptionalTags: false,
  removeRedundantAttributes: false,
  removeScriptTypeAttributes: true,
  removeStyleLinkTypeAttributes: true,
  sortAttributes: false,
  sortClassName: false,
  trimCustomFragments: false,
  useShortDoctype: true,
});

function asRepoPath(absolutePath) {
  return relative(ROOT, absolutePath).split(sep).join('/');
}

function sha256(data) {
  return createHash('sha256').update(data).digest('hex');
}

export function canonicalizeTextInput(input, label = 'input') {
  const bytes = Buffer.isBuffer(input) ? input : Buffer.from(input, 'utf8');
  if (bytes.length >= 3 && bytes[0] === 0xef && bytes[1] === 0xbb && bytes[2] === 0xbf) {
    throw new Error(`${label}: UTF-8 BOM is not allowed`);
  }
  const text = bytes.toString('utf8').replaceAll('\r\n', '\n');
  if (text.includes('\r')) {
    throw new Error(`${label}: bare CR line endings are not allowed`);
  }
  return text;
}

function canonicalText(path) {
  return canonicalizeTextInput(readFileSync(path), asRepoPath(path));
}

export function assertExpectedNode() {
  const expected = canonicalText(join(ROOT, '.node-version')).trim().replace(/^v/, '');
  const actual = process.versions.node;
  if (actual !== expected) {
    throw new Error(
      `Web UI assets require Node ${expected}; detected ${actual}. ` +
        'Install the version from .node-version before regenerating files.',
    );
  }
  return expected;
}

export function canonicalGzip(input) {
  const source = Buffer.isBuffer(input) ? input : Buffer.from(input, 'utf8');
  const compressed = gzipSync(source, {
    level: 9,
    strategy: zlibConstants.Z_DEFAULT_STRATEGY,
  });

  // zlib writes the host OS into byte 9 (0x03 on Unix, 0x0a on Windows).
  // It has no decoding semantics, so fix it to "unknown" for byte-identical
  // assets regardless of where the tracked firmware arrays are generated.
  compressed[9] = 0xff;

  if (
    compressed[0] !== 0x1f ||
    compressed[1] !== 0x8b ||
    compressed[2] !== 0x08 ||
    compressed[3] !== 0x00
  ) {
    throw new Error('gzip header must use deflate without optional metadata');
  }
  if (compressed.subarray(4, 8).some((value) => value !== 0)) {
    throw new Error('gzip header contains a non-deterministic timestamp');
  }
  if (compressed[9] !== 0xff) {
    throw new Error('gzip header contains a non-canonical OS identifier');
  }
  if (!gunzipSync(compressed).equals(source)) {
    throw new Error('gzip round-trip verification failed');
  }
  return compressed;
}

function validatePageSource(page, source) {
  const label = `webui/src/pages/${page.filename}`;
  const markerCount = source.split(STYLE_MARKER).length - 1;
  if (markerCount !== 1) {
    throw new Error(`${label}: expected exactly one shared-style marker`);
  }
  if (!/^\s*<!doctype html>/i.test(source)) {
    throw new Error(`${label}: missing HTML doctype`);
  }
  if (!/<html\s+lang="es">/i.test(source)) {
    throw new Error(`${label}: missing lang="es"`);
  }
  if (/<(?:script|img)[^>]+src\s*=\s*["']https?:/i.test(source)) {
    throw new Error(`${label}: remote script/image resources are not allowed`);
  }
  if (/<link[^>]+href\s*=\s*["']https?:/i.test(source)) {
    throw new Error(`${label}: remote stylesheet resources are not allowed`);
  }
}

function inputInventory() {
  const inputs = INPUT_PATHS.map((repoPath) => {
    const absolutePath = join(ROOT, ...repoPath.split('/'));
    const bytes = Buffer.from(canonicalText(absolutePath), 'utf8');
    return {
      path: repoPath,
      sha256: sha256(bytes),
      bytes: bytes.length,
    };
  });
  const fingerprint = inputs.map(({ path, sha256: hash }) => `${path}\0${hash}\n`).join('');
  return { inputs, sourceHash: sha256(fingerprint) };
}

export function renderCppArray(bytes, indent = '    ', columns = 12) {
  const lines = [];
  for (let offset = 0; offset < bytes.length; offset += columns) {
    const chunk = bytes.subarray(offset, offset + columns);
    lines.push(
      indent +
        [...chunk]
          .map((value) => `0x${value.toString(16).padStart(2, '0')}`)
          .join(', ') +
        ',',
    );
  }
  return lines.join('\n');
}

function renderHeader() {
  const declarations = PAGE_DEFINITIONS.map(
    ({ symbol }) => `extern const portal_assets::WebAsset ${symbol};`,
  ).join('\n');
  return `// Generated by webui/build.mjs. Do not edit.\n#pragma once\n\n#include "web/portal_assets.h"\n\nnamespace web_assets {\n${declarations}\n} // namespace web_assets\n`;
}

function renderCpp(pages, sourceHash) {
  const arrays = pages
    .map(({ definition, gzip, decodedSize, etag }) => {
      const dataSymbol = `${definition.symbol}_GZIP`;
      return `alignas(4) const uint8_t ${dataSymbol}[] PROGMEM = {\n${renderCppArray(gzip)}\n};`;
    })
    .join('\n\n');

  const definitions = pages
    .map(({ definition, decodedSize, etag }) => {
      const dataSymbol = `${definition.symbol}_GZIP`;
      return `const portal_assets::WebAsset ${definition.symbol} = {\n    ${dataSymbol},\n    static_cast<uint32_t>(sizeof(${dataSymbol})),\n    ${decodedSize}u,\n    "text/html; charset=utf-8",\n    "${etag.replaceAll('"', '\\"')}",\n};`;
    })
    .join('\n\n');

  return `// Generated by webui/build.mjs. Do not edit.\n// Source fingerprint: ${sourceHash}\n#include "web/generated_assets.h"\n\n#include <pgmspace.h>\n\nnamespace web_assets {\nnamespace {\n${arrays}\n} // namespace\n\n${definitions}\n} // namespace web_assets\n`;
}

async function buildPages(sharedCss) {
  const results = [];
  for (const definition of PAGE_DEFINITIONS) {
    const sourcePath = join(WEBUI_DIR, 'src', 'pages', definition.filename);
    const source = canonicalText(sourcePath);
    validatePageSource(definition, source);
    const assembled = source.replace(STYLE_MARKER, `<style>\n${sharedCss}\n  </style>`);
    const minified = await minify(assembled, MINIFY_OPTIONS);
    if (minified.includes(STYLE_MARKER)) {
      throw new Error(`${definition.filename}: unresolved include marker`);
    }
    const decoded = Buffer.from(`${minified.trim()}\n`, 'utf8');
    const gzip = canonicalGzip(decoded);
    if (gzip.length > definition.budgetGzip) {
      throw new Error(
        `${definition.route}: gzip ${gzip.length} B exceeds budget ${definition.budgetGzip} B`,
      );
    }
    const compressedHash = sha256(gzip);
    results.push({
      definition,
      decoded,
      decodedSize: decoded.length,
      gzip,
      compressedHash,
      etag: `"sha256-${compressedHash}"`,
    });
  }
  const totalGzip = results.reduce((total, page) => total + page.gzip.length, 0);
  if (totalGzip > TOTAL_GZIP_BUDGET) {
    throw new Error(
      `portal: gzip ${totalGzip} B exceeds total budget ${TOTAL_GZIP_BUDGET} B`,
    );
  }
  return results;
}

function renderManifest(pages, nodeVersion, inventory, generated) {
  const manifest = {
    schema_version: 1,
    generator: 'webui/build.mjs',
    node_version: nodeVersion,
    html_minifier_terser_version: HTML_MINIFIER_VERSION,
    source_sha256: inventory.sourceHash,
    inputs: inventory.inputs,
    generated,
    totals: {
      decoded_bytes: pages.reduce((total, page) => total + page.decodedSize, 0),
      gzip_bytes: pages.reduce((total, page) => total + page.gzip.length, 0),
      gzip_budget_bytes: TOTAL_GZIP_BUDGET,
    },
    pages: pages.map(({ definition, decodedSize, gzip, compressedHash, etag }) => ({
      key: definition.key,
      route: definition.route,
      source: `webui/src/pages/${definition.filename}`,
      preview: `.ap-portal-preview/${definition.filename}`,
      symbol: definition.symbol,
      content_type: 'text/html; charset=utf-8',
      content_encoding: 'gzip',
      decoded_bytes: decodedSize,
      gzip_bytes: gzip.length,
      gzip_budget_bytes: definition.budgetGzip,
      gzip_sha256: compressedHash,
      etag,
    })),
  };
  return `${JSON.stringify(manifest, null, 2)}\n`;
}

export async function buildArtifacts({ check = false } = {}) {
  const nodeVersion = assertExpectedNode();
  const sharedCss = canonicalText(join(WEBUI_DIR, 'src', 'styles', 'app.css')).trim();
  const inventory = inputInventory();
  const pages = await buildPages(sharedCss);
  const generatedHeader = renderHeader();
  const generatedCpp = renderCpp(pages, inventory.sourceHash);
  const generated = [
    { path: asRepoPath(GENERATED_HEADER), sha256: sha256(generatedHeader) },
    { path: asRepoPath(GENERATED_CPP), sha256: sha256(generatedCpp) },
  ];
  const outputs = new Map([
    [GENERATED_HEADER, generatedHeader],
    [GENERATED_CPP, generatedCpp],
    [MANIFEST, renderManifest(pages, nodeVersion, inventory, generated)],
  ]);

  if (check) {
    const stale = [];
    for (const [path, expected] of outputs) {
      if (!existsSync(path) || readFileSync(path, 'utf8') !== expected) {
        stale.push(asRepoPath(path));
      }
    }
    if (stale.length > 0) {
      throw new Error(
        `Generated web assets are stale: ${stale.join(', ')}. Run npm run webui:build.`,
      );
    }
    return { pages, changed: [] };
  }

  const changed = [];
  for (const [path, content] of outputs) {
    mkdirSync(dirname(path), { recursive: true });
    if (!existsSync(path) || readFileSync(path, 'utf8') !== content) {
      writeFileSync(path, content, { encoding: 'utf8' });
      changed.push(asRepoPath(path));
    }
  }
  mkdirSync(PREVIEW_DIR, { recursive: true });
  for (const { definition, decoded } of pages) {
    const path = join(PREVIEW_DIR, definition.filename);
    if (!existsSync(path) || !readFileSync(path).equals(decoded)) {
      writeFileSync(path, decoded);
      changed.push(asRepoPath(path));
    }
  }
  return { pages, changed };
}

async function main() {
  const args = new Set(process.argv.slice(2));
  const unknown = [...args].filter((arg) => arg !== '--check');
  if (unknown.length > 0) {
    throw new Error(`Unknown arguments: ${unknown.join(', ')}`);
  }
  const result = await buildArtifacts({ check: args.has('--check') });
  for (const { definition, decodedSize, gzip } of result.pages) {
    console.log(
      `${definition.route.padEnd(7)} ${String(decodedSize).padStart(6)} B raw  ${String(gzip.length).padStart(6)} B gzip`,
    );
  }
  if (args.has('--check')) {
    console.log('webui generated assets are current');
  } else if (result.changed.length === 0) {
    console.log('webui outputs already current');
  } else {
    console.log(`updated ${result.changed.join(', ')}`);
  }
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.message : error);
    process.exitCode = 1;
  });
}
