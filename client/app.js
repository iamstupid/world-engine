/**
 * WorldEngine - Main application entry point.
 * Multi-buffer pipeline with partial rerun support.
 *
 * Pipeline steps: Mesh -> Terrain -> Plates -> Display
 * Changing a step's params reruns from that step onward,
 * reusing results from earlier steps.
 */

import { Engine } from './renderer/engine.js';
import { GlobeView } from './renderer/views/globe_view.js';
import { MapView } from './renderer/views/map_view.js';
import { PipelinePanel } from './ui/pipeline_panel.js';
import { Legend } from './ui/legend.js';
import { ViewSwitcher } from './ui/view_switcher.js';
import { generateNoise, generatePlates, getFieldData, getBufferData } from './api/client.js';
import { buildMesh, parseFILDWasm } from './api/mesh_bridge.js';
import { mapLayerToColors, TERRAIN_COLORMAP } from './renderer/colormap_registry.js';

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------
let engine = null;
let globeView = null;
let mapView = null;
let panel = null;
let legend = null;
let viewSwitcher = null;

// Current pipeline state
let currentMeshData = null;
let currentWorldId = null;
let currentK = 6;

// Buffer cache: name -> { data: TypedArray, dtype: string, colormap: string, displayName: string }
const bufferCache = {};
let activeLayer = 'elevation';

// ---------------------------------------------------------------------------
// Status helper
// ---------------------------------------------------------------------------
function setStatus(text) {
    const el = document.getElementById('status-text');
    if (el) el.textContent = text;
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------
window.addEventListener('load', () => {
    // 1. Create the 3D engine
    const container = document.getElementById('renderer-container');
    engine = new Engine(container);

    // 2. Create views
    globeView = new GlobeView(engine);
    const mapCanvas = document.getElementById('map-canvas');
    mapView = new MapView(mapCanvas);

    // 3. Create pipeline panel
    const pipelineContainer = document.getElementById('pipeline-controls');
    panel = new PipelinePanel(pipelineContainer);

    // 4. Create legend
    const legendContainer = document.getElementById('legend-container');
    legend = new Legend(legendContainer);

    // 5. Create view switcher
    const globeBtn = document.getElementById('btn-globe');
    const mapBtn = document.getElementById('btn-map');
    viewSwitcher = new ViewSwitcher(globeBtn, mapBtn);

    // 6. Wire up view switching
    viewSwitcher.onSwitch((viewName) => {
        if (viewName === 'globe') {
            mapView.hide();
            globeView.show();
        } else {
            globeView.hide();
            mapView.show();
            renderMap();
        }
    });

    // 7. Wire up pipeline callbacks
    panel.onGenerateTerrain(async () => {
        await doGenerateTerrain();
    });

    panel.onGeneratePlates(async () => {
        await doGeneratePlates();
    });

    panel.onLayerChange((name) => {
        activeLayer = name;
        renderCurrentView();
        updateLegend();
    });

    panel.onExaggerationChange(() => {
        renderCurrentView();
    });

    // Plates button disabled until terrain exists
    panel.setStepEnabled('plates', false);

    // 8. Generate default terrain on startup
    setStatus('Generating default world...');
    doGenerateTerrain();
});

// ---------------------------------------------------------------------------
// Pipeline step: Generate Terrain
// Reruns mesh build (if k changed) + terrain generation.
// Invalidates downstream: plates.
// ---------------------------------------------------------------------------
async function doGenerateTerrain() {
    panel.setAllEnabled(false);

    try {
        const params = panel.getTerrainParams();
        const k = params.k;

        // Rebuild mesh if k changed
        if (!currentMeshData || k !== currentK) {
            setStatus('Building mesh (WASM)...');
            panel.setSectionStatus('mesh', 'running');
            currentMeshData = await buildMesh(k);
            globeView.setMesh(currentMeshData);
            currentK = k;
            panel.setSectionStatus('mesh', 'done');
        }

        // Generate terrain
        setStatus('Generating terrain...');
        panel.setSectionStatus('terrain', 'running');

        const genResult = await generateNoise({
            mesh_type: 'ico',
            N: params.N,
            noise: {
                seed:           params.seed,
                octaves:        params.octaves,
                frequency:      params.frequency,
                warp_amplitude: params.warp,
                ocean_fraction: params.ocean_fraction,
            },
        });

        currentWorldId = genResult.world_id;

        // Download + parse elevation FILD
        setStatus('Downloading elevation...');
        const fieldBuffer = await getFieldData(currentWorldId, 'elevation');
        const fild = await parseFILDWasm(fieldBuffer, k);

        // Cache the elevation buffer
        bufferCache.elevation = {
            data: fild.data,
            dtype: fild.dtype || 'float32',
            colormap: 'terrain',
            displayName: 'Elevation',
        };

        panel.setSectionStatus('terrain', 'done');

        // Invalidate downstream: remove plates from cache
        invalidatePlates();

        // Enable plates button
        panel.setStepEnabled('plates', true);

        // Set active layer to elevation and render
        activeLayer = 'elevation';
        panel.setActiveLayer('elevation');
        renderCurrentView();
        updateLegend();

        const cellCount = fild.data.length;
        setStatus(`Terrain done. ${cellCount} cells.`);

    } catch (err) {
        console.error('Terrain generation failed:', err);
        setStatus(`Error: ${err.message}`);
        panel.setSectionStatus('terrain', 'pending');
    } finally {
        panel.setAllEnabled(true);
        panel.setStepEnabled('plates', !!bufferCache.elevation);
    }
}

// ---------------------------------------------------------------------------
// Pipeline step: Generate Plates
// Reuses terrain from cache — only reruns plate assignment.
// ---------------------------------------------------------------------------
async function doGeneratePlates() {
    if (!currentWorldId || !bufferCache.elevation) {
        setStatus('Generate terrain first.');
        return;
    }

    panel.setAllEnabled(false);

    try {
        const params = panel.getPlateParams();

        setStatus('Assigning plates...');
        panel.setSectionStatus('plates', 'running');

        const result = await generatePlates({
            world_id: currentWorldId,
            plates: params,
        });

        // Download + parse plate_id FILD
        setStatus('Downloading plate data...');
        const plateBuffer = await getBufferData(currentWorldId, 'plate_id');
        const fild = await parseFILDWasm(plateBuffer, currentK);

        // Cache the plate buffer
        bufferCache.plate_id = {
            data: fild.data,
            dtype: fild.dtype || 'uint16',
            colormap: 'categorical',
            displayName: 'Plate ID',
            numCategories: params.num_plates,
        };

        panel.setSectionStatus('plates', 'done');

        // Add plate layer to selector if not already there
        panel.addLayer('plate_id', 'Plate ID');

        // Switch to plate view
        activeLayer = 'plate_id';
        panel.setActiveLayer('plate_id');
        renderCurrentView();
        updateLegend();

        setStatus(`Plates done. ${params.num_plates} plates assigned.`);

    } catch (err) {
        console.error('Plate generation failed:', err);
        setStatus(`Error: ${err.message}`);
        panel.setSectionStatus('plates', 'pending');
    } finally {
        panel.setAllEnabled(true);
    }
}

// ---------------------------------------------------------------------------
// Invalidation: downstream step data cleared when upstream params change.
// ---------------------------------------------------------------------------
function invalidatePlates() {
    delete bufferCache.plate_id;
    panel.setSectionStatus('plates', 'pending');
}

// ---------------------------------------------------------------------------
// Rendering: applies displacement (from elevation) + coloring (from active layer)
// ---------------------------------------------------------------------------
function renderCurrentView() {
    if (!currentMeshData) return;

    const layerData = bufferCache[activeLayer];
    if (!layerData) return;

    // Displacement always from elevation (or null if no elevation)
    const elevationData = bufferCache.elevation ? bufferCache.elevation.data : null;
    const exaggeration = panel.getExaggeration();

    // Color from active layer
    const colors = mapLayerToColors(layerData.data, layerData.colormap, {
        numCategories: layerData.numCategories,
    });

    // Render globe
    globeView.update(elevationData, colors, exaggeration);

    // Re-render map if visible
    if (viewSwitcher.getCurrentView() === 'map') {
        renderMap();
    }
}

function renderMap() {
    if (!currentMeshData || !bufferCache.elevation) return;
    // Map view always shows elevation with terrain colormap for now
    mapView.render(currentMeshData, bufferCache.elevation.data, TERRAIN_COLORMAP);
}

// ---------------------------------------------------------------------------
// Legend
// ---------------------------------------------------------------------------
function updateLegend() {
    const layerData = bufferCache[activeLayer];
    if (!layerData) {
        legend.clear();
        return;
    }

    if (layerData.colormap === 'categorical') {
        const numCats = layerData.numCategories || (Math.max(...layerData.data) + 1);
        legend.showCategorical(numCats, (i) => `Plate ${i}`);
    } else {
        legend.showGradient(TERRAIN_COLORMAP, '-1.0', '1.0');
    }
}
