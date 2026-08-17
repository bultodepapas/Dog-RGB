\set QUIET 1
\pset pager off
\timing on
\set QUIET 0

select version() as postgres_version;
create extension if not exists postgis;
select postgis_full_version() as postgis_version;

drop schema if exists phase0_capacity cascade;
create schema phase0_capacity;

create table phase0_capacity.telemetry_points (
    collar_id uuid not null,
    boot_sequence bigint not null check (boot_sequence >= 0),
    point_sequence bigint not null check (point_sequence >= 0),
    recorded_at timestamptz,
    received_at timestamptz not null,
    lat_e7 integer,
    lon_e7 integer,
    position geography(Point, 4326),
    reported_speed_cmps integer check (
        reported_speed_cmps is null or reported_speed_cmps between 0 and 1112
    ),
    satellites smallint check (satellites is null or satellites between 0 and 64),
    flags integer not null,
    time_quality text not null check (
        time_quality in (
            'unknown',
            'approximate_persisted',
            'server_anchored',
            'sntp_synced',
            'gnss_trusted'
        )
    ),
    telemetry_schema smallint not null,
    firmware_version text not null,
    chunk_sequence bigint not null,
    primary key (collar_id, boot_sequence, point_sequence),
    check ((lat_e7 is null) = (lon_e7 is null)),
    check (lat_e7 is null or lat_e7 between -900000000 and 900000000),
    check (lon_e7 is null or lon_e7 between -1800000000 and 1800000000)
);

create index telemetry_points_collar_time_idx
    on phase0_capacity.telemetry_points
    (collar_id, recorded_at, point_sequence);

create index telemetry_points_position_gist_idx
    on phase0_capacity.telemetry_points using gist (position)
    where position is not null;

\echo 'Loading 1,000,000 representative points...'
insert into phase0_capacity.telemetry_points (
    collar_id,
    boot_sequence,
    point_sequence,
    recorded_at,
    received_at,
    lat_e7,
    lon_e7,
    position,
    reported_speed_cmps,
    satellites,
    flags,
    time_quality,
    telemetry_schema,
    firmware_version,
    chunk_sequence
)
select
    '018f6a94-59d8-7a21-9b56-6f4d1ee06000'::uuid,
    ((sample_no - 1) / 100000) + 1,
    ((sample_no - 1) % 100000) + 1,
    observed_at,
    observed_at + make_interval(secs => 30 + (sample_no % 90)::integer),
    lat_e7,
    lon_e7,
    st_setsrid(st_makepoint(lon_e7 / 10000000.0, lat_e7 / 10000000.0), 4326)::geography,
    case when sample_no % 29 < 20 then 0 else 110 + (sample_no % 510)::integer end,
    (6 + sample_no % 9)::smallint,
    (
        1 -- FIX_VALID
        | 4 -- TIME_TRUSTED: every generated point has non-zero recorded UTC
        | case when sample_no % 29 < 20
            then 8 -- STATIONARY_HEARTBEAT
            else 2 -- MOVEMENT_EVIDENCE
          end
        | case when sample_no % 97 = 0 then 16 else 0 end -- LOW_QUALITY
    ),
    'gnss_trusted',
    3,
    'phase0-fixture',
    (((sample_no - 1) % 100000) / 96) + 1
from generate_series(1, 1000000) as series(sample_no)
cross join lateral (
    select '2026-01-01 00:00:00+00'::timestamptz
        + make_interval(secs => (sample_no * 5)::double precision) as observed_at
) as clock
cross join lateral (
    select
        47110000 + round(16000 * sin(sample_no / 900.0))::integer as lat_e7,
        -740721000 + round(22000 * cos(sample_no / 1100.0))::integer as lon_e7
) as coordinates;

vacuum (analyze) phase0_capacity.telemetry_points;

\echo 'Relation and index sizes'
select
    pg_size_pretty(pg_relation_size('phase0_capacity.telemetry_points')) as heap,
    pg_size_pretty(pg_indexes_size('phase0_capacity.telemetry_points')) as indexes,
    pg_size_pretty(pg_total_relation_size('phase0_capacity.telemetry_points')) as total,
    pg_relation_size('phase0_capacity.telemetry_points') as heap_bytes,
    pg_indexes_size('phase0_capacity.telemetry_points') as index_bytes,
    pg_total_relation_size('phase0_capacity.telemetry_points') as total_bytes,
    round(pg_total_relation_size('phase0_capacity.telemetry_points')::numeric / count(*), 2)
        as total_bytes_per_point,
    count(*) as points
from phase0_capacity.telemetry_points;

select
    indexrelname,
    pg_size_pretty(pg_relation_size(indexrelid)) as size
from pg_stat_user_indexes
where schemaname = 'phase0_capacity'
order by pg_relation_size(indexrelid) desc;

\echo 'Representative serialized sizes (sample of 10,000)'
with sample as (
    select *
    from phase0_capacity.telemetry_points
    order by collar_id, boot_sequence, point_sequence
    limit 10000
)
select
    round(avg(octet_length(json_build_array(
        point_sequence,
        floor(extract(epoch from recorded_at) * 1000)::bigint,
        lat_e7,
        lon_e7,
        reported_speed_cmps,
        satellites,
        flags
    )::text)), 2) as avg_compact_tuple_json_bytes,
    round(avg(octet_length(row_to_json(sample)::text)), 2) as avg_named_row_json_bytes
from sample;

\echo 'One local-day aggregate (~17,280 observations at five-second cadence)'
explain (analyze, buffers, settings)
select
    count(*),
    min(recorded_at),
    max(recorded_at),
    sum(reported_speed_cmps),
    max(reported_speed_cmps)
from phase0_capacity.telemetry_points
where collar_id = '018f6a94-59d8-7a21-9b56-6f4d1ee06000'::uuid
  and recorded_at >= '2026-01-10 05:00:00+00'::timestamptz
  and recorded_at <  '2026-01-11 05:00:00+00'::timestamptz;

\echo 'Thirty-day aggregate (~518,400 observations)'
explain (analyze, buffers, settings)
select count(*), sum(reported_speed_cmps), max(reported_speed_cmps)
from phase0_capacity.telemetry_points
where collar_id = '018f6a94-59d8-7a21-9b56-6f4d1ee06000'::uuid
  and recorded_at >= '2026-01-05 00:00:00+00'::timestamptz
  and recorded_at <  '2026-02-04 00:00:00+00'::timestamptz;

\echo 'Keyset route page'
explain (analyze, buffers, settings)
select point_sequence, recorded_at, lat_e7, lon_e7, reported_speed_cmps, satellites, flags
from phase0_capacity.telemetry_points
where collar_id = '018f6a94-59d8-7a21-9b56-6f4d1ee06000'::uuid
  and boot_sequence = 5
  and point_sequence > 40000
order by point_sequence
limit 2000;

\echo 'Spatial bounding-box query'
explain (analyze, buffers, settings)
select count(*)
from phase0_capacity.telemetry_points
where position && st_makeenvelope(-74.0730, 4.7102, -74.0712, 4.7118, 4326)::geography;

\echo 'Duplicate-safe 384-point ingest shape'
explain (analyze, buffers, wal, settings)
insert into phase0_capacity.telemetry_points
select *
from phase0_capacity.telemetry_points
where collar_id = '018f6a94-59d8-7a21-9b56-6f4d1ee06000'::uuid
order by boot_sequence, point_sequence
limit 384
on conflict (collar_id, boot_sequence, point_sequence) do nothing;
