import Ajv2020, { type ValidateFunction } from "npm:ajv@8.17.1/dist/2020.js";
import addFormats from "npm:ajv-formats@3.0.1";
import common from "./contracts/common.schema.json" with { type: "json" };
import capabilities from "./contracts/capabilities.schema.json" with { type: "json" };
import configResource from "./contracts/config-resource.schema.json" with { type: "json" };
import telemetry from "./contracts/telemetry.schema.json" with { type: "json" };
import issueClaim from "./contracts/user-v1-issue-claim-request.schema.json" with { type: "json" };
import deviceClaim from "./contracts/device-v1-claim-request.schema.json" with { type: "json" };
import deviceSync from "./contracts/device-v1-sync-request.schema.json" with { type: "json" };
import deviceRevoke from "./contracts/device-v1-revoke-request.schema.json" with { type: "json" };
import { HttpProblem } from "./gateway.ts";

export type ContractRequest = "issue-claim" | "device-claim" | "device-sync" | "device-revoke";

const ajv = new Ajv2020({ allErrors: false, strict: true });
addFormats(ajv);
for (const schema of [common, capabilities, configResource, telemetry]) ajv.addSchema(schema);

const validators: Record<ContractRequest, ValidateFunction> = {
  "issue-claim": ajv.compile(issueClaim),
  "device-claim": ajv.compile(deviceClaim),
  "device-sync": ajv.compile(deviceSync),
  "device-revoke": ajv.compile(deviceRevoke),
};

export function validateContractRequest(kind: ContractRequest, body: unknown): void {
  const validator = validators[kind];
  if (validator(body)) return;

  const path = validator.errors?.[0]?.instancePath ?? "";
  if (path.startsWith("/capabilities")) {
    throw new HttpProblem(422, "invalid_capabilities", "Invalid capability manifest", "The capability manifest failed validation.");
  }
  if (path.startsWith("/upload")) {
    throw new HttpProblem(422, "invalid_telemetry", "Invalid telemetry", "The telemetry payload failed validation.");
  }
  throw new HttpProblem(400, "invalid_envelope", "Invalid request envelope", "The request does not match the device-v1 contract.");
}
