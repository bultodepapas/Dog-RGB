import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

import {
  createDogDataAccess,
  DogDataAccessError,
} from "./dogs-core.ts";

const USER_ID = "10000000-0000-4000-8000-000000000001";
const DOG_ID = "20000000-0000-4000-8000-000000000001";
const OTHER_DOG_ID = "20000000-0000-4000-8000-000000000002";

function harness({
  userId = USER_ID,
  membership = { dog_id: DOG_ID, role: "owner" },
  dog = {
    id: DOG_ID,
    name: "Mora",
    timezone: "America/Bogota",
    created_by: USER_ID,
    deleted_at: null,
    weight_kg: 18.25,
  },
  memberships = [membership].filter(Boolean),
  dogs = [dog].filter(Boolean),
} = {}) {
  const calls = {
    createClient: 0,
    getFreshUserId: 0,
    findMembership: 0,
    findDog: 0,
    listMemberships: 0,
    listDogs: 0,
  };
  const client = {};

  const dal = createDogDataAccess({
    async createClient() {
      calls.createClient += 1;
      return client;
    },
    async getFreshUserId(receivedClient) {
      calls.getFreshUserId += 1;
      assert.equal(receivedClient, client);
      return userId;
    },
    async findMembership(receivedClient, receivedUserId, receivedDogId) {
      calls.findMembership += 1;
      assert.equal(receivedClient, client);
      assert.equal(receivedUserId, USER_ID);
      assert.equal(receivedDogId, DOG_ID);
      return membership;
    },
    async findDog(receivedClient, receivedDogId) {
      calls.findDog += 1;
      assert.equal(receivedClient, client);
      assert.equal(receivedDogId, DOG_ID);
      return dog;
    },
    async listMemberships(receivedClient, receivedUserId) {
      calls.listMemberships += 1;
      assert.equal(receivedClient, client);
      assert.equal(receivedUserId, USER_ID);
      return memberships;
    },
    async listDogs(receivedClient, dogIds) {
      calls.listDogs += 1;
      assert.equal(receivedClient, client);
      assert.deepEqual(dogIds, memberships.map((row) => row.dog_id));
      return dogs;
    },
  });

  return { calls, dal };
}

async function expectCode(promise, code) {
  await assert.rejects(promise, (error) => {
    assert.ok(error instanceof DogDataAccessError);
    assert.equal(error.code, code);
    return true;
  });
}

test("malformed dog identifiers fail before session or database access", async () => {
  const { calls, dal } = harness();

  await expectCode(dal.requireDogAccess("not-a-uuid", "read"), "invalid_dog_id");
  assert.deepEqual(calls, {
    createClient: 0,
    getFreshUserId: 0,
    findMembership: 0,
    findDog: 0,
    listMemberships: 0,
    listDogs: 0,
  });
});

test("stale or revoked sessions fail closed before membership lookup", async () => {
  const { calls, dal } = harness({ userId: null });

  await expectCode(
    dal.requireDogAccess(DOG_ID, "read"),
    "authentication_required",
  );
  assert.equal(calls.createClient, 1);
  assert.equal(calls.getFreshUserId, 1);
  assert.equal(calls.findMembership, 0);
});

test("non-members and insufficient roles receive the same denial", async () => {
  const nonMember = harness({ membership: null });
  const viewer = harness({
    membership: { dog_id: DOG_ID, role: "viewer" },
  });

  await expectCode(
    nonMember.dal.requireDogAccess(DOG_ID, "read"),
    "access_denied",
  );
  await expectCode(
    viewer.dal.requireDogAccess(DOG_ID, "write"),
    "access_denied",
  );
  assert.equal(nonMember.calls.findDog, 0);
  assert.equal(viewer.calls.findDog, 0);
});

test("owner, editor, and viewer capabilities are exact", async () => {
  for (const [role, allowed, denied] of [
    ["owner", ["read", "write", "admin"], []],
    ["editor", ["read", "write"], ["admin"]],
    ["viewer", ["read"], ["write", "admin"]],
  ]) {
    for (const capability of allowed) {
      const { dal } = harness({ membership: { dog_id: DOG_ID, role } });
      assert.deepEqual(await dal.requireDogAccess(DOG_ID, capability), {
        dogId: DOG_ID,
        role,
      });
    }
    for (const capability of denied) {
      const { dal } = harness({ membership: { dog_id: DOG_ID, role } });
      await expectCode(
        dal.requireDogAccess(DOG_ID, capability),
        "access_denied",
      );
    }
  }
});

test("dog summary DTO excludes ownership, health, deletion, and audit fields", async () => {
  const { dal } = harness();
  const dto = await dal.getDogSummary(DOG_ID);

  assert.deepEqual(dto, {
    id: DOG_ID,
    name: "Mora",
    timezone: "America/Bogota",
    role: "owner",
  });
  assert.deepEqual(Object.keys(dto).sort(), ["id", "name", "role", "timezone"]);
  assert.equal(Object.isFrozen(dto), true);
});

test("list DTOs remain minimal and membership scoped", async () => {
  const memberships = [
    { dog_id: DOG_ID, role: "owner" },
    { dog_id: OTHER_DOG_ID, role: "viewer" },
  ];
  const dogs = [
    {
      id: DOG_ID,
      name: "Mora",
      timezone: "America/Bogota",
      created_by: USER_ID,
    },
    {
      id: OTHER_DOG_ID,
      name: "Tango",
      timezone: "America/Bogota",
      created_by: "90000000-0000-4000-8000-000000000009",
    },
  ];
  const { dal } = harness({ memberships, dogs });
  const result = await dal.listDogSummaries();

  assert.deepEqual(result, [
    { id: DOG_ID, name: "Mora", timezone: "America/Bogota", role: "owner" },
    {
      id: OTHER_DOG_ID,
      name: "Tango",
      timezone: "America/Bogota",
      role: "viewer",
    },
  ]);
  assert.equal(Object.isFrozen(result), true);
  result.forEach((dto) =>
    assert.deepEqual(Object.keys(dto).sort(), ["id", "name", "role", "timezone"]),
  );
});

test("every public DAL call performs a fresh identity check", async () => {
  const { calls, dal } = harness();

  await dal.requireDogAccess(DOG_ID, "read");
  await dal.getDogSummary(DOG_ID);
  await dal.listDogSummaries();

  assert.equal(calls.createClient, 3);
  assert.equal(calls.getFreshUserId, 3);
});

test("corrupt membership roles fail closed", async () => {
  const { dal } = harness({
    membership: { dog_id: DOG_ID, role: "superuser" },
  });

  await expectCode(
    dal.requireDogAccess(DOG_ID, "read"),
    "data_unavailable",
  );
});

test("unexpected runtime capabilities fail closed", async () => {
  const { dal } = harness();

  await expectCode(
    dal.requireDogAccess(DOG_ID, "delete-everything"),
    "access_denied",
  );
});

test("production adapter is server-only, explicit-column, fresh, and uncached", async () => {
  const adapterSource = await readFile(
    new URL("./dogs.ts", import.meta.url),
    "utf8",
  );
  const coreSource = await readFile(
    new URL("./dogs-core.ts", import.meta.url),
    "utf8",
  );

  assert.match(adapterSource, /^import "server-only";/u);
  assert.match(adapterSource, /client\.auth\.getUser\(\)/u);
  assert.match(adapterSource, /\.select\("dog_id, role"\)/u);
  assert.match(adapterSource, /\.select\("id, name, timezone"\)/u);
  assert.match(adapterSource, /\.eq\("user_id", userId\)/u);
  assert.doesNotMatch(adapterSource, /\.select\(\s*["'`]\*["'`]\s*\)/u);
  assert.doesNotMatch(
    `${adapterSource}\n${coreSource}`,
    /unstable_cache|"use cache|cache: private/u,
  );
});
