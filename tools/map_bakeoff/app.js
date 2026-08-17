import {
  FIXTURE_VERSION,
  PROVIDERS,
  routeFacts,
  selectScenarios,
  speedForSegment,
} from "/fixtures.mjs";

const allowedLabelModes = new Set(["normal", "deemphasized"]);
const allowedCvdModes = new Set(["none", "deuteranopia", "protanopia", "grayscale"]);

function queryOptions() {
  const query = new URLSearchParams(location.search);
  const providerId = query.get("provider") ?? "stadia-dark";
  const labelMode = query.get("labels") ?? "normal";
  const cvdMode = query.get("cvd") ?? "none";
  const scenarioSet = query.get("scenarios") ?? "all";
  const provider = PROVIDERS[providerId];
  if (!provider) throw new Error(`Unknown provider '${providerId}'`);
  if (!allowedLabelModes.has(labelMode)) throw new Error(`Unknown label mode '${labelMode}'`);
  if (!allowedCvdModes.has(cvdMode)) throw new Error(`Unknown CVD mode '${cvdMode}'`);
  return { providerId, provider, labelMode, cvdMode, scenarioSet };
}

function styleFor(provider) {
  if (provider.style) return provider.style;
  const hash = new URLSearchParams(location.hash.slice(1));
  const key = hash.get("key");
  if (!key || !/^[A-Za-z0-9_-]{8,256}$/.test(key)) {
    throw new Error("MapTiler requires a temporary testing key in the URL fragment: #key=<key>");
  }
  return `https://api.maptiler.com/maps/${provider.maptilerStyle}/style.json?key=${encodeURIComponent(key)}`;
}

function formatDuration(seconds) {
  if (seconds >= 3_600 && seconds % 3_600 === 0) return `${seconds / 3_600} h`;
  return `${Math.round(seconds / 60)} min`;
}

function renderScenarioShells(scenarios) {
  const grid = document.querySelector("#map-grid");
  const tableBody = document.querySelector("#route-data-body");
  grid.setAttribute("aria-label", `${scenarios.length} deterministic synthetic route scenarios`);
  scenarios.forEach((scenario, index) => {
    const facts = routeFacts(scenario);
    const article = document.createElement("article");
    article.className = "scenario";
    article.innerHTML = `
      <div class="scenario-copy">
        <span>${String(index + 1).padStart(2, "0")}</span>
        <h2>${scenario.title}</h2>
        <p>${scenario.context}</p>
        <small>${facts.pointCount} points · ${formatDuration(facts.durationSeconds)} · ${facts.distanceMeters} m · ${facts.gapCount} gap${facts.gapCount === 1 ? "" : "s"}</small>
      </div>
      <div class="map" id="map-${scenario.id}" role="region" aria-label="${scenario.title} interactive map; synthetic fixture, ${facts.pointCount} samples"></div>`;
    grid.append(article);

    const row = document.createElement("tr");
    row.innerHTML = `
      <th scope="row">${scenario.title}</th>
      <td>${facts.pointCount}</td>
      <td>${formatDuration(facts.durationSeconds)}</td>
      <td>${facts.distanceMeters} m</td>
      <td>${facts.gapCount}</td>`;
    tableBody.append(row);
  });
}

function segments(scenario) {
  return {
    type: "FeatureCollection",
    features: scenario.coordinates.slice(0, -1).flatMap((coordinate, index) => {
      if (scenario.gapIndexes.includes(index)) return [];
      const { band, speedKph } = speedForSegment(scenario, index);
      return [{
        type: "Feature",
        properties: {
          band,
          speed_kph: speedKph,
          quality: "trusted synthetic",
          elapsed_s: index * scenario.sampleIntervalSeconds,
          segment_index: index,
        },
        geometry: {
          type: "LineString",
          coordinates: [coordinate, scenario.coordinates[index + 1]],
        },
      }];
    }),
  };
}

function gaps(scenario) {
  return {
    type: "FeatureCollection",
    features: scenario.gapIndexes.map((index) => ({
      type: "Feature",
      properties: { quality: "low / discontinuity", segment_index: index },
      geometry: {
        type: "LineString",
        coordinates: [scenario.coordinates[index], scenario.coordinates[index + 1]],
      },
    })),
  };
}

function endpoints(scenario) {
  return {
    type: "FeatureCollection",
    features: [
      { type: "Feature", properties: { kind: "start" }, geometry: { type: "Point", coordinates: scenario.coordinates[0] } },
      { type: "Feature", properties: { kind: "end" }, geometry: { type: "Point", coordinates: scenario.coordinates.at(-1) } },
    ],
  };
}

function applyLabelMode(map, labelMode) {
  if (labelMode !== "deemphasized") return 0;
  let modified = 0;
  for (const layer of map.getStyle().layers ?? []) {
    if (layer.type !== "symbol" || layer.layout?.["text-field"] === undefined) continue;
    try {
      map.setPaintProperty(layer.id, "text-opacity", 0.24);
      if (layer.layout?.["icon-image"] !== undefined) map.setPaintProperty(layer.id, "icon-opacity", 0.22);
      modified += 1;
    } catch {
      // A provider may lock or specialize a symbol layer. The evidence manifest records the count actually changed.
    }
  }
  return modified;
}

function addRoute(map, scenario) {
  map.addSource("gaps", { type: "geojson", data: gaps(scenario) });
  map.addLayer({
    id: "gaps",
    type: "line",
    source: "gaps",
    paint: {
      "line-color": "#ff0055",
      "line-width": 4,
      "line-opacity": 0.9,
      "line-dasharray": [1.2, 2],
    },
  });
  map.addSource("route", { type: "geojson", data: segments(scenario) });
  map.addLayer({
    id: "route-casing",
    type: "line",
    source: "route",
    paint: { "line-color": "#000000", "line-width": 10, "line-opacity": 0.84 },
    layout: { "line-cap": "round", "line-join": "round" },
  });
  map.addLayer({
    id: "route-visible",
    type: "line",
    source: "route",
    paint: {
      "line-color": ["match", ["get", "band"], "stationary", "#4ee8ff", "steady", "#00ff41", "fast", "#ffd700", "#ff0055"],
      "line-width": ["match", ["get", "band"], "stationary", 7, "fast", 6, 5],
      "line-opacity": 0.96,
    },
    layout: { "line-cap": "round", "line-join": "round" },
  });
  map.addLayer({
    id: "route-hit",
    type: "line",
    source: "route",
    paint: { "line-color": "#ffffff", "line-width": 22, "line-opacity": 0 },
  });
  map.addSource("endpoints", { type: "geojson", data: endpoints(scenario) });
  map.addLayer({
    id: "endpoints",
    type: "circle",
    source: "endpoints",
    paint: {
      "circle-radius": 6,
      "circle-color": ["match", ["get", "kind"], "start", "#00ff41", "#ff0055"],
      "circle-stroke-color": "#000000",
      "circle-stroke-width": 2,
    },
  });

  const bounds = scenario.coordinates.reduce(
    (current, coordinate) => current.extend(coordinate),
    new maplibregl.LngLatBounds(scenario.coordinates[0], scenario.coordinates[0]),
  );
  map.fitBounds(bounds, { padding: scenario.padding, duration: 0 });

  const popup = new maplibregl.Popup({ closeButton: false, closeOnClick: false, offset: 12 });
  const showSegment = (event) => {
    const feature = event.features?.[0];
    if (!feature) return;
    const speed = Number(feature.properties.speed_kph).toFixed(1);
    popup
      .setLngLat(event.lngLat)
      .setText(`${speed} km/h · ${feature.properties.band} · ${feature.properties.quality}`)
      .addTo(map);
  };
  map.on("mousemove", "route-hit", (event) => {
    map.getCanvas().style.cursor = "pointer";
    showSegment(event);
  });
  map.on("click", "route-hit", showSegment);
  map.on("mouseleave", "route-hit", () => {
    map.getCanvas().style.cursor = "";
    popup.remove();
  });
}

function createMap(style, scenario, labelMode) {
  return new Promise((resolve, reject) => {
    const map = new maplibregl.Map({
      container: `map-${scenario.id}`,
      style,
      attributionControl: true,
      interactive: true,
      fadeDuration: 0,
      preserveDrawingBuffer: true,
    });
    map.once("error", (event) => reject(event.error ?? new Error("Map failed to load")));
    map.once("load", () => {
      const labelLayersModified = applyLabelMode(map, labelMode);
      addRoute(map, scenario);
      map.once("idle", () => resolve({ id: scenario.id, labelLayersModified }));
    });
  });
}

async function start() {
  const options = queryOptions();
  const scenarios = selectScenarios(options.scenarioSet);
  document.querySelector("#provider-name").textContent = options.provider.name;
  document.querySelector("#evidence-state").textContent = `${options.labelMode} labels · ${options.cvdMode} diagnostic · ${scenarios.length} synthetic routes`;
  document.body.dataset.provider = options.providerId;
  document.body.dataset.labels = options.labelMode;
  document.body.dataset.cvd = options.cvdMode;
  document.documentElement.dataset.scenarioCount = String(scenarios.length);
  renderScenarioShells(scenarios);

  const style = styleFor(options.provider);
  const mapResults = await Promise.all(scenarios.map((scenario) => createMap(style, scenario, options.labelMode)));
  window.__dogRgbEvidence = Object.freeze({
    fixtureVersion: FIXTURE_VERSION,
    providerId: options.providerId,
    providerVariant: options.provider.variant,
    scenarioSet: options.scenarioSet,
    scenarioFacts: scenarios.map(routeFacts),
    labelMode: options.labelMode,
    cvdMode: options.cvdMode,
    labelLayersModified: mapResults.reduce((sum, result) => sum + result.labelLayersModified, 0),
    colorVisionSimulation: options.cvdMode === "none" ? "none" : "CSS/SVG diagnostic approximation; requires human review",
    qualityGapUsesDashPattern: true,
    routeWidthAlsoVariesBySpeedBand: true,
    rawRouteCoordinatesRenderedInDom: false,
  });
  document.documentElement.dataset.ready = "true";
}

start().catch((error) => {
  const target = document.querySelector("#fatal-error");
  target.hidden = false;
  target.textContent = error instanceof Error ? error.message : String(error);
  document.documentElement.dataset.ready = "error";
});
