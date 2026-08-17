export const FIXTURE_VERSION = "2026-08-13.2";

export const PROVIDERS = Object.freeze({
  "stadia-dark": Object.freeze({
    family: "stadia",
    variant: "dark",
    name: "Stadia · Alidade Smooth Dark",
    style: "https://tiles.stadiamaps.com/styles/alidade_smooth_dark.json",
    credentialMode: "keyless-loopback-only",
  }),
  "stadia-light": Object.freeze({
    family: "stadia",
    variant: "light",
    name: "Stadia · Alidade Smooth",
    style: "https://tiles.stadiamaps.com/styles/alidade_smooth.json",
    credentialMode: "keyless-loopback-only",
  }),
  "stadia-outdoor": Object.freeze({
    family: "stadia",
    variant: "outdoor",
    name: "Stadia · Outdoors",
    style: "https://tiles.stadiamaps.com/styles/outdoors.json",
    credentialMode: "keyless-loopback-only",
  }),
  "maptiler-dark": Object.freeze({
    family: "maptiler",
    variant: "dark",
    name: "MapTiler · Dataviz Dark",
    maptilerStyle: "dataviz-dark",
    credentialMode: "fragment-testing-key-required",
  }),
  "maptiler-light": Object.freeze({
    family: "maptiler",
    variant: "light",
    name: "MapTiler · Dataviz Light",
    maptilerStyle: "dataviz-light",
    credentialMode: "fragment-testing-key-required",
  }),
  "maptiler-outdoor": Object.freeze({
    family: "maptiler",
    variant: "outdoor",
    name: "MapTiler · Outdoor v4",
    maptilerStyle: "outdoor-v4",
    credentialMode: "fragment-testing-key-required",
  }),
});

const coreScenarios = [
  {
    id: "urban",
    title: "Synthetic urban",
    context: "Road hierarchy, labels, and dense points of interest",
    padding: 48,
    sampleIntervalSeconds: 30,
    gapIndexes: [3],
    coordinates: [
      [-74.0718, 4.6486], [-74.0708, 4.6493], [-74.0695, 4.6499],
      [-74.0682, 4.6508], [-74.0671, 4.6518], [-74.0658, 4.6527],
      [-74.0643, 4.6532], [-74.0631, 4.6541],
    ],
  },
  {
    id: "park",
    title: "Synthetic park",
    context: "Green space, water, and route contrast",
    padding: 54,
    sampleIntervalSeconds: 30,
    gapIndexes: [3],
    coordinates: [
      [-74.0948, 4.6570], [-74.0936, 4.6581], [-74.0920, 4.6585],
      [-74.0905, 4.6580], [-74.0899, 4.6568], [-74.0911, 4.6558],
      [-74.0928, 4.6557], [-74.0942, 4.6562],
    ],
  },
  {
    id: "trail",
    title: "Synthetic steep trail",
    context: "Terrain context, steep geometry, and a quality gap",
    padding: 46,
    sampleIntervalSeconds: 30,
    gapIndexes: [3],
    coordinates: [
      [-74.0665, 4.6026], [-74.0650, 4.6032], [-74.0638, 4.6040],
      [-74.0623, 4.6044], [-74.0608, 4.6052], [-74.0592, 4.6050],
      [-74.0575, 4.6057], [-74.0557, 4.6059],
    ],
  },
  {
    id: "rural",
    title: "Synthetic rural road",
    context: "Sparse labels, minor roads, and orientation",
    padding: 44,
    sampleIntervalSeconds: 30,
    gapIndexes: [3],
    coordinates: [
      [-74.0040, 4.7060], [-74.0007, 4.7084], [-73.9970, 4.7098],
      [-73.9935, 4.7130], [-73.9890, 4.7140], [-73.9853, 4.7171],
      [-73.9808, 4.7190], [-73.9760, 4.7224],
    ],
  },
];

function makeSparseRoute() {
  const coordinates = Array.from({ length: 11 }, (_, index) => {
    const progress = index / 10;
    const longitude = -74.0840 + (0.0021 * progress) + (0.00024 * Math.sin(progress * Math.PI * 2));
    const latitude = 4.6320 + (0.00865 * progress);
    return [Number(longitude.toFixed(6)), Number(latitude.toFixed(6))];
  });
  return {
    id: "sparse-1km",
    title: "Sparse route · ≈1 km",
    context: "Eleven samples stress long chords and missing-point legibility",
    padding: 52,
    sampleIntervalSeconds: 60,
    gapIndexes: [5],
    expectedProfile: "sparse-distance",
    coordinates,
  };
}

function makeDenseRoute() {
  const pointCount = 241;
  const coordinates = Array.from({ length: pointCount }, (_, index) => {
    const progress = index / (pointCount - 1);
    const angle = progress * Math.PI * 2;
    const longitude = -74.0820 + (0.0046 * Math.sin(angle * 2)) + (0.0012 * Math.sin(angle * 7));
    const latitude = 4.6710 + (0.0038 * Math.cos(angle * 3)) + (0.0009 * Math.sin(angle * 5));
    return [Number(longitude.toFixed(6)), Number(latitude.toFixed(6))];
  });
  return {
    id: "dense-2h",
    title: "Dense route · 2 h",
    context: "241 samples at 30 s cadence stress overlap and rendering density",
    padding: 42,
    sampleIntervalSeconds: 30,
    durationSeconds: 7_200,
    gapIndexes: [80, 161],
    expectedProfile: "dense-duration",
    coordinates,
  };
}

export const SCENARIOS = Object.freeze(
  [...coreScenarios, makeSparseRoute(), makeDenseRoute()].map((scenario) => Object.freeze({
    ...scenario,
    gapIndexes: Object.freeze([...scenario.gapIndexes]),
    coordinates: Object.freeze(scenario.coordinates.map((coordinate) => Object.freeze([...coordinate]))),
  })),
);

export function haversineMeters(from, to) {
  const radians = (degrees) => degrees * Math.PI / 180;
  const radiusMeters = 6_371_008.8;
  const latitudeDelta = radians(to[1] - from[1]);
  const longitudeDelta = radians(to[0] - from[0]);
  const fromLatitude = radians(from[1]);
  const toLatitude = radians(to[1]);
  const a = Math.sin(latitudeDelta / 2) ** 2
    + Math.cos(fromLatitude) * Math.cos(toLatitude) * Math.sin(longitudeDelta / 2) ** 2;
  return 2 * radiusMeters * Math.asin(Math.sqrt(a));
}

export function routeDistanceMeters(coordinates) {
  return coordinates.slice(0, -1).reduce(
    (distance, coordinate, index) => distance + haversineMeters(coordinate, coordinates[index + 1]),
    0,
  );
}

export function routeFacts(scenario) {
  const durationSeconds = scenario.durationSeconds
    ?? (scenario.coordinates.length - 1) * scenario.sampleIntervalSeconds;
  return Object.freeze({
    id: scenario.id,
    pointCount: scenario.coordinates.length,
    segmentCount: scenario.coordinates.length - 1,
    gapCount: scenario.gapIndexes.length,
    durationSeconds,
    distanceMeters: Math.round(routeDistanceMeters(scenario.coordinates)),
  });
}

export function speedForSegment(scenario, index) {
  if (index % 17 === 0) return { band: "stationary", speedKph: 0.2 };
  if ((index + scenario.id.length) % 5 === 0) return { band: "fast", speedKph: 11.6 };
  return { band: "steady", speedKph: 4.8 };
}

export function selectScenarios(setName = "all") {
  if (setName === "all") return SCENARIOS;
  if (setName === "stress") return SCENARIOS.filter(({ expectedProfile }) => Boolean(expectedProfile));
  throw new Error(`Unknown scenario set '${setName}'`);
}
