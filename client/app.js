/**
 * WorldEngine - Main application entry point.
 * Initializes the 3D renderer, UI panels, and wires up generation + rendering.
 */

import { Engine } from './renderer/engine.js';
import { GlobeView } from './renderer/views/globe_view.js';
import { MapView } from './renderer/views/map_view.js';
import { TERRAIN_COLORMAP } from './renderer/colormap.js';
import { ParameterPanel } from './ui/parameter_panel.js';
import { ViewSwitcher } from './ui/view_switcher.js';
import { generateNoise, getFieldData } from './api/client.js';
import { buildMesh, parseFILDWasm } from './api/mesh_bridge.js';

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------
let engine = null;
let globeView = null;
let mapView = null;
let paramPanel = null;
let viewSwitcher = null;

// Current world state
let currentMeshData = null;
let currentFieldData = null;
let currentWorldId = null;

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
    // 1. Create the 3D engine in the renderer container
    const container = document.getElementById('renderer-container');
    engine = new Engine(container);

    // 2. Create views
    globeView = new GlobeView(engine);
    const mapCanvas = document.getElementById('map-canvas');
    mapView = new MapView(mapCanvas);

    // 3. Create parameter panel
    const paramContainer = document.getElementById('parameter-controls');
    paramPanel = new ParameterPanel(paramContainer);

    // 4. Create view switcher
    const globeBtn = document.getElementById('btn-globe');
    const mapBtn = document.getElementById('btn-map');
    viewSwitcher = new ViewSwitcher(globeBtn, mapBtn);

    // 5. Wire up view switching
    viewSwitcher.onSwitch((viewName) => {
        if (viewName === 'globe') {
            mapView.hide();
            globeView.show();
        } else {
            globeView.hide();
            mapView.show();
            // Re-render the map whenever we switch to it
            if (currentMeshData && currentFieldData) {
                mapView.render(currentMeshData, currentFieldData, TERRAIN_COLORMAP);
            }
        }
    });

    // 6. Wire up generate button
    paramPanel.onGenerate(async (params) => {
        await generateWorld(params);
    });

    // 7. Generate default world on startup
    setStatus('Generating default world...');
    const defaultParams = paramPanel.getParams();
    generateWorld(defaultParams);
});

// ---------------------------------------------------------------------------
// World generation pipeline
// ---------------------------------------------------------------------------
async function generateWorld(params) {
    paramPanel.setEnabled(false);

    try {
        // Step 1: Build mesh geometry via WASM Worker
        setStatus('Building mesh (WASM)...');
        currentMeshData = await buildMesh(params.k);
        globeView.setMesh(currentMeshData);

        setStatus('Generating terrain...');

        // Step 2: Generate noise-based terrain on server
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

        setStatus('Downloading elevation data...');

        // Step 3: Fetch FILD field data and parse via WASM Worker
        const fieldBuffer = await getFieldData(currentWorldId, 'elevation');
        const fild = await parseFILDWasm(fieldBuffer, params.k);
        currentFieldData = fild.data;

        setStatus('Rendering...');

        // Step 4: Update the globe view
        const exaggeration = params.exaggeration;
        globeView.update(currentFieldData, TERRAIN_COLORMAP, exaggeration);

        // Step 5: If map view is active, re-render it too
        if (viewSwitcher.getCurrentView() === 'map') {
            mapView.render(currentMeshData, currentFieldData, TERRAIN_COLORMAP);
        }

        const cellCount = currentFieldData.length;
        setStatus(`Done. ${cellCount} cells rendered.`);

    } catch (err) {
        console.error('Generation failed:', err);
        setStatus(`Error: ${err.message}`);
    } finally {
        paramPanel.setEnabled(true);
    }
}
