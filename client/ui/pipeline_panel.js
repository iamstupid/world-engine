/**
 * Pipeline panel UI.
 * Multi-section panel: Mesh, Terrain, Plates, Display.
 * Each pipeline step has its own parameters and generate button.
 */

export class PipelinePanel {
    /**
     * @param {HTMLElement} container - the DOM element to inject controls into
     */
    constructor(container) {
        this.container = container;
        this._controls = {};
        this._callbacks = {};
        this._sectionStatus = {};
        this._build();
    }

    _build() {
        this.container.innerHTML = '';

        // --- Mesh section ---
        this._buildSection('mesh', 'Mesh', [
            { id: 'k', label: 'Detail Level', type: 'range', min: 1, max: 13, step: 1, value: 6,
              format: (v) => `k=${v} (N=${1 << parseInt(v)})` },
        ]);

        // --- Terrain section ---
        this._buildSection('terrain', 'Terrain', [
            { id: 'terrain_seed',     label: 'Seed',            type: 'number', min: 0, max: 999999, step: 1, value: 42 },
            { id: 'octaves',          label: 'Octaves',         type: 'range', min: 1, max: 10, step: 1, value: 6 },
            { id: 'frequency',        label: 'Frequency',       type: 'range', min: 0.5, max: 5.0, step: 0.1, value: 1.5 },
            { id: 'warp',             label: 'Warp',            type: 'range', min: 0, max: 1.0, step: 0.01, value: 0 },
            { id: 'ocean_fraction',   label: 'Ocean Fraction',  type: 'range', min: 0.3, max: 0.8, step: 0.01, value: 0.55 },
        ], 'Generate Terrain', 'onGenerateTerrain');

        // --- Plates section ---
        this._buildSection('plates', 'Plates', [
            { id: 'num_plates',       label: 'Num Plates',      type: 'range', min: 3, max: 40, step: 1, value: 12 },
            { id: 'plate_seed',       label: 'Seed',            type: 'number', min: 0, max: 999999, step: 1, value: 42 },
            { id: 'ocean_bias',       label: 'Ocean Bias',      type: 'range', min: 0.5, max: 4.0, step: 0.1, value: 1.5 },
            { id: 'weight_frequency', label: 'Weight Freq.',    type: 'range', min: 0.1, max: 5.0, step: 0.1, value: 1.0 },
            { id: 'weight_octaves',   label: 'Weight Octaves',  type: 'range', min: 1, max: 8, step: 1, value: 4 },
        ], 'Generate Plates', 'onGeneratePlates');

        // --- Display section ---
        this._buildSection('display', 'Display', [
            { id: 'exaggeration', label: 'Exaggeration', type: 'range', min: 0, max: 0.3, step: 0.005, value: 0.05 },
        ]);

        // Layer radio buttons
        this._buildLayerSelector();
    }

    _buildSection(sectionId, title, params, buttonLabel, callbackName) {
        const section = document.createElement('div');
        section.className = 'pipeline-section';
        section.id = `section-${sectionId}`;

        // Header
        const header = document.createElement('div');
        header.className = 'pipeline-header';

        const indicator = document.createElement('span');
        indicator.className = 'status-indicator pending';
        indicator.id = `indicator-${sectionId}`;
        this._sectionStatus[sectionId] = indicator;
        header.appendChild(indicator);

        const titleEl = document.createElement('span');
        titleEl.className = 'section-title';
        titleEl.textContent = title;
        header.appendChild(titleEl);

        section.appendChild(header);

        // Parameter controls
        const body = document.createElement('div');
        body.className = 'pipeline-body';

        for (const def of params) {
            const group = document.createElement('div');
            group.className = 'param-group';

            const label = document.createElement('label');
            label.setAttribute('for', `param-${def.id}`);

            const labelText = document.createTextNode(def.label + ' ');
            label.appendChild(labelText);

            const valueSpan = document.createElement('span');
            valueSpan.className = 'param-value';
            valueSpan.id = `param-${def.id}-value`;
            const fmt = def.format || ((v) => v);
            valueSpan.textContent = fmt(def.value);
            label.appendChild(valueSpan);
            group.appendChild(label);

            let input;
            if (def.type === 'range') {
                input = document.createElement('input');
                input.type = 'range';
                input.id = `param-${def.id}`;
                input.min = def.min;
                input.max = def.max;
                input.step = def.step;
                input.value = def.value;
                input.addEventListener('input', () => {
                    valueSpan.textContent = fmt(input.value);
                });
            } else if (def.type === 'number') {
                input = document.createElement('input');
                input.type = 'number';
                input.id = `param-${def.id}`;
                input.min = def.min;
                input.max = def.max;
                input.step = def.step;
                input.value = def.value;
                input.addEventListener('input', () => {
                    valueSpan.textContent = input.value;
                });
            }

            group.appendChild(input);
            body.appendChild(group);
            this._controls[def.id] = input;
        }

        section.appendChild(body);

        // Button
        if (buttonLabel && callbackName) {
            const btn = document.createElement('button');
            btn.className = 'step-button';
            btn.id = `btn-${sectionId}`;
            btn.textContent = buttonLabel;
            btn.addEventListener('click', () => {
                const cb = this._callbacks[callbackName];
                if (cb) cb();
            });
            section.appendChild(btn);
        }

        this.container.appendChild(section);
    }

    _buildLayerSelector() {
        const group = document.createElement('div');
        group.className = 'layer-selector';
        group.id = 'layer-selector';

        const label = document.createElement('label');
        label.className = 'section-label';
        label.textContent = 'Layer';
        group.appendChild(label);

        // Start with only elevation; plates get added dynamically
        this._layerGroup = group;
        this._layerRadios = {};
        this._addLayerOption('elevation', 'Elevation', true);

        // Insert into display section
        const displaySection = document.getElementById('section-display');
        if (displaySection) {
            displaySection.appendChild(group);
        }
    }

    _addLayerOption(name, displayName, checked = false) {
        if (this._layerRadios[name]) return;

        const wrapper = document.createElement('div');
        wrapper.className = 'layer-option';

        const radio = document.createElement('input');
        radio.type = 'radio';
        radio.name = 'active-layer';
        radio.value = name;
        radio.id = `layer-${name}`;
        radio.checked = checked;
        radio.addEventListener('change', () => {
            const cb = this._callbacks.onLayerChange;
            if (cb) cb(name);
        });

        const label = document.createElement('label');
        label.setAttribute('for', `layer-${name}`);
        label.textContent = displayName;

        wrapper.appendChild(radio);
        wrapper.appendChild(label);
        this._layerGroup.appendChild(wrapper);
        this._layerRadios[name] = radio;
    }

    // --- Public API ---

    /** Register a layer in the display section's radio group. */
    addLayer(name, displayName) {
        this._addLayerOption(name, displayName, false);
    }

    /** Get the currently selected layer name. */
    getActiveLayer() {
        for (const [name, radio] of Object.entries(this._layerRadios)) {
            if (radio.checked) return name;
        }
        return 'elevation';
    }

    /** Set a layer as active. */
    setActiveLayer(name) {
        if (this._layerRadios[name]) {
            this._layerRadios[name].checked = true;
        }
    }

    /** Mark a pipeline section as done/pending. */
    setSectionStatus(sectionId, status) {
        const indicator = this._sectionStatus[sectionId];
        if (indicator) {
            indicator.className = `status-indicator ${status}`;
        }
    }

    /** Enable/disable a specific step button. */
    setStepEnabled(sectionId, enabled) {
        const btn = document.getElementById(`btn-${sectionId}`);
        if (btn) btn.disabled = !enabled;
    }

    /** Enable/disable all step buttons. */
    setAllEnabled(enabled) {
        for (const btn of this.container.querySelectorAll('.step-button')) {
            btn.disabled = !enabled;
        }
    }

    getTerrainParams() {
        const k = parseInt(this._controls.k.value, 10);
        return {
            k,
            N: 1 << k,
            seed: parseInt(this._controls.terrain_seed.value, 10),
            octaves: parseInt(this._controls.octaves.value, 10),
            frequency: parseFloat(this._controls.frequency.value),
            warp: parseFloat(this._controls.warp.value),
            ocean_fraction: parseFloat(this._controls.ocean_fraction.value),
        };
    }

    getPlateParams() {
        return {
            num_plates: parseInt(this._controls.num_plates.value, 10),
            seed: parseInt(this._controls.plate_seed.value, 10),
            ocean_bias: parseFloat(this._controls.ocean_bias.value),
            weight_frequency: parseFloat(this._controls.weight_frequency.value),
            weight_octaves: parseInt(this._controls.weight_octaves.value, 10),
        };
    }

    getExaggeration() {
        return parseFloat(this._controls.exaggeration.value);
    }

    getK() {
        return parseInt(this._controls.k.value, 10);
    }

    // Callback registration
    onGenerateTerrain(cb) { this._callbacks.onGenerateTerrain = cb; }
    onGeneratePlates(cb) { this._callbacks.onGeneratePlates = cb; }
    onLayerChange(cb) { this._callbacks.onLayerChange = cb; }

    onExaggerationChange(cb) {
        const input = this._controls.exaggeration;
        if (input) {
            input.addEventListener('input', () => cb(parseFloat(input.value)));
        }
    }

    onKChange(cb) {
        const input = this._controls.k;
        if (input) {
            input.addEventListener('change', () => cb(parseInt(input.value, 10)));
        }
    }
}
