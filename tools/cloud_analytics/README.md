# Local summary worker

Start this repository's disposable Supabase stack and ingest simulator data. Run the bounded manual worker with:

```sh
node tools/cloud_analytics/run.mjs --max-batches 64
```

The runner accepts only the Docker container labelled `Dog-RGB-1`. Each call uses the service-role-only producer, a maximum of four dirty days, one stale recording per day iteration, and an outer 10-second statement timeout. Daily and recording inputs cap at 100,000 and 250,000 points and loss markers. It checks remaining dirty work separately because a zero consumed-day count may mean that one day still has recordings to process. At the batch limit it exits 2 and leaves work queued; rerun to continue.

New telemetry and loss markers dirty affected civil days and advance recording freshness. Summary upserts and queue consumption occur in one transaction. Current-day and open-recording windows end at the worker's captured `as_of`. `source_received_at` tracks source/queue freshness, `window_start`/`window_end` identify the covered interval, and `computed_at` records calculation provenance. Retention gaps produce `insufficient_retained_data`; source caps and missing time evidence also return explicit statuses with absent metrics.

No hosted schedule is installed. Run the worker manually after ingestion or a late upload, then refresh the portal. The two-session race check is:

```sh
node tools/cloud_analytics/concurrency.mjs
```

It locks a synthetic dirty day in the ingestion transaction while the real worker runs; the worker must skip it, and the next batch must summarize both committed points and drain the queue. The fixture is deleted in `finally`. `phase1:local` runs this check after database tests.

Algorithm v1 rules and units are exported as `CLOUD_ANALYTICS_RULES` from `packages/analytics/index.js`. The database fixture recomputes pre-M4 daily and recording rows with missing provenance and checks deterministic replay; no future v2 migration is implemented or tested. See [`packages/analytics/README.md`](../../packages/analytics/README.md).
