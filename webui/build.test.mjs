import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { gunzipSync } from 'node:zlib';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

import {
  PAGE_DEFINITIONS,
  buildArtifacts,
  canonicalizeTextInput,
  canonicalGzip,
  renderCppArray,
} from './build.mjs';

const WEBUI = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(WEBUI, '..');

function digest(bytes) {
  return createHash('sha256').update(bytes).digest('hex');
}

test('canonical gzip is deterministic and carries no variable metadata', () => {
  const source = Buffer.from('DOG-RGB\n'.repeat(128));
  const first = canonicalGzip(source);
  const second = canonicalGzip(source);
  assert.deepEqual(first, second);
  assert.deepEqual(
    [...first.subarray(0, 10)],
    [0x1f, 0x8b, 0x08, 0, 0, 0, 0, 0, 0x02, 0xff],
  );
  assert.deepEqual(gunzipSync(first), source);
});

test('text fingerprints are stable across LF and CRLF checkouts', () => {
  assert.equal(canonicalizeTextInput('alpha\r\nbeta\r\n'), 'alpha\nbeta\n');
  assert.equal(canonicalizeTextInput('alpha\nbeta\n'), 'alpha\nbeta\n');
  assert.throws(
    () => canonicalizeTextInput(Buffer.from([0xef, 0xbb, 0xbf, 0x61])),
    /UTF-8 BOM/,
  );
  assert.throws(() => canonicalizeTextInput('alpha\rbeta'), /bare CR/);
});

test('C++ array rendering preserves binary zeroes and stable width', () => {
  assert.equal(
    renderCppArray(Buffer.from([0, 1, 15, 16, 255]), '  ', 3),
    '  0x00, 0x01, 0x0f,\n  0x10, 0xff,',
  );
});

test('tracked manifest and C++ assets match generated decoded bytes', async () => {
  const built = await buildArtifacts({ check: true });
  const manifest = JSON.parse(
    readFileSync(join(WEBUI, 'generated', 'manifest.json'), 'utf8'),
  );
  const cpp = readFileSync(
    join(ROOT, 'Platformio', 'Dog-RGB', 'src', 'web', 'generated_assets.cpp'),
    'utf8',
  );

  assert.equal(manifest.schema_version, 1);
  assert.equal(manifest.pages.length, PAGE_DEFINITIONS.length);
  for (const definition of PAGE_DEFINITIONS) {
    const page = manifest.pages.find((item) => item.key === definition.key);
    assert.ok(page, `manifest entry for ${definition.key}`);
    const pattern = new RegExp(
      `const uint8_t ${definition.symbol}_GZIP\\[\\] PROGMEM = \\{([\\s\\S]*?)\\n\\};`,
    );
    const match = cpp.match(pattern);
    assert.ok(match, `C++ array for ${definition.symbol}`);
    const values = [...match[1].matchAll(/0x([0-9a-f]{2})/g)].map((item) =>
      Number.parseInt(item[1], 16),
    );
    const gzip = Buffer.from(values);
    const generatedPage = built.pages.find(
      (item) => item.definition.key === definition.key,
    );
    assert.ok(generatedPage, `generated page for ${definition.key}`);
    const preview = generatedPage.decoded;
    assert.equal(gzip.length, page.gzip_bytes);
    assert.equal(digest(gzip), page.gzip_sha256);
    assert.equal(page.etag, `"sha256-${page.gzip_sha256}"`);
    assert.deepEqual(gunzipSync(gzip), preview);
    assert.equal(preview.length, page.decoded_bytes);
    assert.ok(gzip.length <= page.gzip_budget_bytes);
  }
});
