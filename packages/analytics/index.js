export const CLOUD_ANALYTICS_VERSION = 1;

export const CLOUD_ANALYTICS_RULES = Object.freeze({
  units: Object.freeze({
    duration: "seconds",
    distance: "meters",
    speed: "centimeters_per_second",
    coordinates: "degrees_e7",
    timestamps: "unix_seconds",
  }),
  flags: Object.freeze({
    fixValid: 0x01,
    movementEvidence: 0x02,
    timeTrusted: 0x04,
    stationaryHeartbeat: 0x08,
    lowQuality: 0x10,
    gap: 0x20,
    legacyV2: 0x40,
  }),
  acceptedTimeQualities: Object.freeze([
    "server_anchored",
    "sntp_synced",
    "gnss_trusted",
  ]),
  minMovingSpeedCmps: 20,
  maxSpeedCmps: 1111,
  minDistanceM: 3,
  maxIntervalS: 65,
  maxDailyPoints: 100_000,
  maxRecordingPoints: 250_000,
});

const { flags } = CLOUD_ANALYTICS_RULES;
const METRIC_FIELDS = Object.freeze([
  "observed_s",
  "moving_s",
  "inactive_s",
  "unknown_s",
  "distance_m",
  "average_observed_cmps",
  "average_moving_cmps",
  "filtered_max_speed_cmps",
  "valid_points",
  "warning_points",
  "gap_count",
  "dropped_points",
  "coverage_ratio",
]);

export function coverageRatio(observedSeconds, windowSeconds) {
  if (
    !Number.isFinite(observedSeconds) ||
    !Number.isFinite(windowSeconds) ||
    windowSeconds <= 0
  ) {
    return null;
  }
  return Math.round(Math.max(0, Math.min(1, observedSeconds / windowSeconds)) * 1_000_000) /
    1_000_000;
}

/**
 * Deterministic Track v3 interval summary. Time fields use Unix seconds;
 * positions use E7 integer coordinates and speeds use cm/s.
 *
 * The result describes telemetry evidence only. It does not identify wear,
 * walks, exercise, rest, sleep, or animal behavior.
 */
export function computeSummaryV1({
  points,
  windowStart,
  windowEnd,
  losses = [],
  maxPoints = CLOUD_ANALYTICS_RULES.maxRecordingPoints,
}) {
  const windowStartS = epochSeconds(windowStart);
  const windowEndS = epochSeconds(windowEnd);
  const windowSeconds =
    Number.isSafeInteger(windowStartS) &&
    Number.isSafeInteger(windowEndS) &&
    windowEndS > windowStartS
      ? windowEndS - windowStartS
      : null;

  if (windowSeconds === null) {
    return unavailableSummary("insufficient_time_evidence", windowStartS, windowEndS);
  }

  const uniquePoints = deduplicatePoints(Array.isArray(points) ? points : []);
  const uniqueLosses = deduplicateLosses(Array.isArray(losses) ? losses : []);
  const sourceSchema = uniquePoints
    .map((point) =>
      point.telemetry_schema == null ||
      (typeof point.telemetry_schema === "string" && point.telemetry_schema.trim() === "")
        ? null
        : Number(point.telemetry_schema),
    )
    .filter((schema) => Number.isInteger(schema) && schema > 0);
  if (uniquePoints.length > maxPoints || uniqueLosses.length > maxPoints) {
    return unavailableSummary("source_limit_exceeded", windowStartS, windowEndS, {
      valid_points: null,
      warning_points: null,
      gap_count: null,
      dropped_points: null,
      source_schema_min: minimum(sourceSchema),
      source_schema_max: maximum(sourceSchema),
    });
  }

  const usable = uniquePoints
    .map(toUsablePoint)
    .filter((point) => point !== null);
  const timedByRecording = new Map();
  for (const point of usable) {
    const key = point.recordingKey;
    if (!timedByRecording.has(key)) timedByRecording.set(key, []);
    timedByRecording.get(key).push(point);
  }

  for (const recordingPoints of timedByRecording.values()) {
    recordingPoints.sort(compareTime);
  }

  let movingSeconds = 0;
  let stationarySeconds = 0;
  const coverageIntervals = [];
  let distanceM = 0;
  let acceptedDistanceSegments = 0;
  let maximumSpeedCmps = null;
  let gapCount = 0;
  const countedGapPoints = new Set();
  const warningPoints = new Set();
  let validPointCount = 0;

  for (const point of uniquePoints) {
    if (!isUsableObservation(point)) warningPoints.add(pointKey(point));
    if ((Number(point.flags) & flags.lowQuality) !== 0) {
      warningPoints.add(pointKey(point));
    }
  }

  for (const [recordingKey, recordingPoints] of timedByRecording) {
    for (const point of recordingPoints) {
      if (
        point.kind !== null &&
        point.time >= windowStartS &&
        point.time < windowEndS
      ) {
        validPointCount += 1;
      }
      if (point.isGap && point.time >= windowStartS && point.time < windowEndS) {
        countedGapPoints.add(`${recordingKey}:${point.point_sequence}`);
      }
    }

    for (let index = 1; index < recordingPoints.length; index += 1) {
      const previous = recordingPoints[index - 1];
      const current = recordingPoints[index];
      const deltaS = current.time - previous.time;
      const overlapS = overlapSeconds(
        previous.time,
        current.time,
        windowStartS,
        windowEndS,
      );

      if (deltaS <= 0) {
        warningPoints.add(pointKey(current.source));
        continue;
      }

      if (
        previous.collar_id === current.collar_id &&
        previous.boot_sequence === current.boot_sequence &&
        current.point_sequence > previous.point_sequence + 1
      ) {
        gapCount += 1;
      }

      if (deltaS > CLOUD_ANALYTICS_RULES.maxIntervalS) {
        if (overlapS > 0) gapCount += 1;
        continue;
      }

      if (previous.isGap || current.isGap) {
        if (overlapS > 0) gapCount += 1;
        continue;
      }

      if (previous.kind !== null && current.kind !== null && overlapS > 0) {
        if (previous.kind === "moving" && current.kind === "moving") {
          coverageIntervals.push({
            start: Math.max(previous.time, windowStartS),
            end: Math.min(current.time, windowEndS),
            kind: "moving",
          });
        } else if (
          previous.kind === "stationary" &&
          current.kind === "stationary"
        ) {
          coverageIntervals.push({
            start: Math.max(previous.time, windowStartS),
            end: Math.min(current.time, windowEndS),
            kind: "stationary",
          });
        }
      }

      if (isDistanceEndpoint(previous) && isDistanceEndpoint(current)) {
        const segmentM = haversineMeters(previous, current);
        const segmentSpeedCmps = (segmentM * 100) / deltaS;
        const midpointS = previous.time + deltaS / 2;
        if (
          midpointS >= windowStartS &&
          midpointS < windowEndS &&
          segmentM >= CLOUD_ANALYTICS_RULES.minDistanceM &&
          segmentSpeedCmps <= CLOUD_ANALYTICS_RULES.maxSpeedCmps
        ) {
          distanceM += segmentM;
          acceptedDistanceSegments += 1;
          maximumSpeedCmps = Math.max(
            maximumSpeedCmps ?? 0,
            Math.round(segmentSpeedCmps),
          );
        } else if (
          midpointS >= windowStartS &&
          midpointS < windowEndS &&
          segmentSpeedCmps > CLOUD_ANALYTICS_RULES.maxSpeedCmps
        ) {
          warningPoints.add(pointKey(previous.source));
          warningPoints.add(pointKey(current.source));
        }
      }
    }
  }

  for (const point of usable) {
    if (
      point.kind === "moving" &&
      point.reportedSpeedCmps !== null &&
      point.reportedSpeedCmps <= CLOUD_ANALYTICS_RULES.maxSpeedCmps &&
      point.time >= windowStartS &&
      point.time < windowEndS
    ) {
      maximumSpeedCmps = Math.max(
        maximumSpeedCmps ?? 0,
        point.reportedSpeedCmps,
      );
    } else if (
      point.reportedSpeedCmps !== null &&
      point.reportedSpeedCmps > CLOUD_ANALYTICS_RULES.maxSpeedCmps &&
      point.time >= windowStartS &&
      point.time < windowEndS
    ) {
      warningPoints.add(pointKey(point.source));
    }
  }

  for (const point of usable) {
    if (
      point.isGap &&
      point.time >= windowStartS &&
      point.time < windowEndS
    ) {
      countedGapPoints.add(`${point.recordingKey}:${point.point_sequence}`);
    }
  }
  gapCount += countedGapPoints.size;

  ({ movingSeconds, stationarySeconds } = unionClassifiedIntervals(
    coverageIntervals,
  ));

  const observedSeconds = movingSeconds + stationarySeconds;
  const inWindow = usable.filter(
    (point) => point.time >= windowStartS && point.time < windowEndS,
  );
  const hasTimeEvidence = inWindow.some((point) => point.kind !== null);
  const resultStatus = hasTimeEvidence
    ? "available"
    : "insufficient_time_evidence";
  const droppedPoints = countLosses(uniqueLosses);
  const schemas = sourceSchema;

  if (resultStatus !== "available") {
    return unavailableSummary(resultStatus, windowStartS, windowEndS, {
      source_schema_min: minimum(schemas),
      source_schema_max: maximum(schemas),
    });
  }

  return {
    summary_status: "available",
    window_start: windowStartS,
    window_end: windowEndS,
    observed_s: observedSeconds,
    moving_s: movingSeconds,
    inactive_s: stationarySeconds,
    unknown_s: windowSeconds - observedSeconds,
    distance_m: acceptedDistanceSegments > 0 ? Math.round(distanceM) : null,
    average_observed_cmps:
      acceptedDistanceSegments > 0 && observedSeconds > 0
        ? Math.round((distanceM * 100) / observedSeconds)
        : null,
    average_moving_cmps:
      acceptedDistanceSegments > 0 && movingSeconds > 0
        ? Math.round((distanceM * 100) / movingSeconds)
        : null,
    filtered_max_speed_cmps: maximumSpeedCmps,
    valid_points: validPointCount,
    warning_points: warningPoints.size,
    gap_count: gapCount,
    dropped_points: droppedPoints,
    coverage_ratio: coverageRatio(observedSeconds, windowSeconds),
    source_schema_min: minimum(schemas),
    source_schema_max: maximum(schemas),
  };
}

function toUsablePoint(point) {
  if (!isTrustedTime(point)) return null;
  const time = epochSeconds(point.recorded_at);
  if (!Number.isSafeInteger(time)) return null;

  const pointFlags = Number(point.flags);
  const isGap = (pointFlags & flags.gap) !== 0;
  const lowQuality = (pointFlags & flags.lowQuality) !== 0;
  let kind = null;
  if (!isGap && !lowQuality) {
    const isMoving =
      (pointFlags & flags.movementEvidence) !== 0 &&
      Number(point.reported_speed_cmps) >=
        CLOUD_ANALYTICS_RULES.minMovingSpeedCmps &&
      Number(point.reported_speed_cmps) <= CLOUD_ANALYTICS_RULES.maxSpeedCmps;
    const isStationary =
      (pointFlags & flags.stationaryHeartbeat) !== 0 &&
      ((point.reported_speed_cmps == null) ||
        Number(point.reported_speed_cmps) <
          CLOUD_ANALYTICS_RULES.minMovingSpeedCmps);
    if (isMoving !== isStationary) kind = isMoving ? "moving" : "stationary";
  }

  return {
    source: point,
    collar_id: point.collar_id,
    boot_sequence: Number(point.boot_sequence),
    point_sequence: Number(point.point_sequence),
    recordingKey: `${point.collar_id ?? ""}:${point.boot_sequence ?? ""}`,
    time,
    kind,
    isGap,
    lowQuality,
    reportedSpeedCmps: validSpeed(point.reported_speed_cmps),
    latE7: validCoordinate(point.lat_e7, 900_000_000),
    lonE7: validCoordinate(point.lon_e7, 1_800_000_000),
    fixValid: (pointFlags & flags.fixValid) !== 0,
  };
}

function isTrustedTime(point) {
  return (
    point &&
    CLOUD_ANALYTICS_RULES.acceptedTimeQualities.includes(point.time_quality) &&
    Number(point.telemetry_schema) === 3 &&
    (Number(point.flags) & flags.timeTrusted) !== 0 &&
    (Number(point.flags) & flags.legacyV2) === 0
  );
}

function isUsableObservation(point) {
  if (!isTrustedTime(point)) return false;
  const pointFlags = Number(point.flags);
  if (pointFlags & (flags.lowQuality | flags.gap | flags.legacyV2)) return false;
  const isMoving =
    (pointFlags & flags.movementEvidence) !== 0 &&
    Number(point.reported_speed_cmps) >= CLOUD_ANALYTICS_RULES.minMovingSpeedCmps &&
    Number(point.reported_speed_cmps) <= CLOUD_ANALYTICS_RULES.maxSpeedCmps;
  const isStationary =
    (pointFlags & flags.stationaryHeartbeat) !== 0 &&
    (point.reported_speed_cmps == null ||
      Number(point.reported_speed_cmps) < CLOUD_ANALYTICS_RULES.minMovingSpeedCmps);
  return isMoving !== isStationary;
}

function isDistanceEndpoint(point) {
  return (
    point.kind === "moving" &&
    !point.isGap &&
    !point.lowQuality &&
    point.fixValid &&
    point.latE7 !== null &&
    point.lonE7 !== null
  );
}

function deduplicatePoints(points) {
  const rows = new Map();
  for (const point of points) {
    const key = pointKey(point);
    if (!rows.has(key)) rows.set(key, point);
  }
  return [...rows.values()];
}

function compareSequence(left, right) {
  return (
    String(left.collar_id ?? "").localeCompare(String(right.collar_id ?? "")) ||
    Number(left.boot_sequence ?? 0) - Number(right.boot_sequence ?? 0) ||
    Number(left.point_sequence ?? 0) - Number(right.point_sequence ?? 0)
  );
}

function compareTime(left, right) {
  return (
    left.time - right.time ||
    left.point_sequence - right.point_sequence ||
    String(left.collar_id ?? "").localeCompare(String(right.collar_id ?? ""))
  );
}

function pointKey(point) {
  return `${point.collar_id ?? ""}:${point.boot_sequence ?? ""}:${point.point_sequence ?? ""}`;
}

function overlapSeconds(start, end, windowStart, windowEnd) {
  return Math.max(0, Math.min(end, windowEnd) - Math.max(start, windowStart));
}

function unionClassifiedIntervals(intervals) {
  const events = new Map();
  for (const interval of intervals) {
    if (interval.end <= interval.start) continue;
    for (const [time, delta] of [
      [interval.start, 1],
      [interval.end, -1],
    ]) {
      if (!events.has(time)) events.set(time, { moving: 0, stationary: 0 });
      events.get(time)[interval.kind] += delta;
    }
  }

  let movingCount = 0;
  let stationaryCount = 0;
  let previousTime = null;
  let movingSeconds = 0;
  let stationarySeconds = 0;
  for (const [time, change] of [...events.entries()].sort((a, b) => a[0] - b[0])) {
    if (previousTime !== null && time > previousTime) {
      if (movingCount > 0 && stationaryCount === 0) {
        movingSeconds += time - previousTime;
      } else if (stationaryCount > 0 && movingCount === 0) {
        stationarySeconds += time - previousTime;
      }
    }
    movingCount += change.moving;
    stationaryCount += change.stationary;
    previousTime = time;
  }
  return { movingSeconds, stationarySeconds };
}

function haversineMeters(left, right) {
  const radians = Math.PI / 180;
  const lat1 = (left.latE7 / 10_000_000) * radians;
  const lat2 = (right.latE7 / 10_000_000) * radians;
  const deltaLat = lat2 - lat1;
  const deltaLon = ((right.lonE7 - left.lonE7) / 10_000_000) * radians;
  const a =
    Math.sin(deltaLat / 2) ** 2 +
    Math.cos(lat1) * Math.cos(lat2) * Math.sin(deltaLon / 2) ** 2;
  const boundedA = Math.max(0, Math.min(1, a));
  return 6_371_008.8 * 2 * Math.atan2(Math.sqrt(boundedA), Math.sqrt(1 - boundedA));
}

function validCoordinate(value, limit) {
  if (value == null || (typeof value === "string" && value.trim() === "")) {
    return null;
  }
  const number = Number(value);
  return Number.isInteger(number) && Math.abs(number) <= limit ? number : null;
}

function validSpeed(value) {
  if (value == null) return null;
  const number = Number(value);
  return Number.isInteger(number) && number >= 0 && number <= 65_534
    ? number
    : null;
}

function countLosses(losses) {
  return losses.reduce(
    (total, count) => total + (Number.isSafeInteger(count) && count > 0 ? count : 0),
    0,
  );
}

function deduplicateLosses(losses) {
  const rows = new Map();
  for (const loss of losses) {
    if (loss?.id != null && !rows.has(loss.id)) rows.set(loss.id, loss);
  }
  return [...rows.values()].map((loss) =>
    Number(loss.dropped_points ?? loss.lost_points ?? 0),
  );
}

function epochSeconds(value) {
  if (typeof value === "number") return Number.isSafeInteger(value) ? value : null;
  if (typeof value === "string") {
    const milliseconds = Date.parse(value);
    return Number.isFinite(milliseconds) ? Math.floor(milliseconds / 1000) : null;
  }
  return null;
}

function minimum(values) {
  return values.length ? values.reduce((smallest, value) => Math.min(smallest, value), Infinity) : null;
}

function maximum(values) {
  return values.length ? values.reduce((largest, value) => Math.max(largest, value), -Infinity) : null;
}

function unavailableSummary(
  status,
  windowStart,
  windowEnd,
  overrides = {},
) {
  const summary = {
    summary_status: status,
    window_start: windowStart,
    window_end: windowEnd,
  };
  for (const field of METRIC_FIELDS) summary[field] = null;
  summary.source_schema_min = null;
  summary.source_schema_max = null;
  return { ...summary, ...overrides };
}
