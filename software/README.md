# Future Software Area

No companion app, backend, cloud portal, account system, or remote ingestion service is implemented here. The supported interface is the ESP32's local Wi-Fi portal.

If optional software work starts, keep it as a separate, explicitly scoped project and preserve offline collar operation/recovery. Relevant proposals:

- [BLE companion app MVP](../docs/app_mvp_spec.md)
- [BLE wire format](../docs/ble_spec.md)
- [Cloud plan snapshot](../docs/PLANS/2026-08-01_cloud-portal-master-plan.md)

Do not add cloud credentials, location data, generated build output, or local environment files to the repository.
