import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import test from 'node:test';

const source = readFileSync(new URL('src/pages/config.html', import.meta.url), 'utf8');
const start = source.indexOf('function identityDraft(){');
const end = source.indexOf('function previewIdentity(){', start);
assert.ok(start >= 0 && end > start);
function draft(name, phone, qr_kind = 'whatsapp') {
  const values = {identity_name: name, identity_phone: phone, identity_kind: qr_kind};
  const context = vm.createContext({TextEncoder, $: id => ({value: values[id]})});
  vm.runInContext(source.slice(start, end), context);
  return JSON.parse(JSON.stringify(vm.runInContext('identityDraft()', context)));
}
test('identity editor normalizes NFC and international phone without guessing a country', () => {
  assert.deepEqual(draft('Rene\u0301', '+1 (000) 000-00000'),
    {name: 'René', phone: '+100000000000', qr_kind: 'whatsapp'});
  assert.equal(draft('Nin\u0303o', '+100000000000', 'call').name, 'Niño');
  assert.throws(() => draft('FREYA', '100000000000'));
});
test('identity name and phone bounds agree with the firmware alphabet', () => {
  assert.equal(draft('Ñ'.repeat(24), '+100000000000000').name.length, 24);
  for (const name of ['W'.repeat(25), ' Freya', 'Freya ', 'Ana  Maria', 'Dog🐶', 'A/B', 'A\nB'])
    assert.throws(() => draft(name, '+100000000000'));
  for (const phone of ['+0123456789', '+123456', '+1234567890123456', '+1234567 ext 3', '+123\t4567'])
    assert.throws(() => draft('FREYA', phone));
});
test('clear is explicit and partial identities are rejected', () => {
  assert.deepEqual(draft('', '', 'disabled'), {name: '', phone: '', qr_kind: 'disabled'});
  assert.throws(() => draft('', '', 'whatsapp'));
  assert.throws(() => draft('FREYA', '', 'disabled'));
  assert.throws(() => draft('FREYA', '+100000000000', 'other'));
});
