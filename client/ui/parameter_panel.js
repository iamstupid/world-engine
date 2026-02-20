/**
 * Parameter panel UI.
 * Creates and manages the control sliders and inputs for world generation.
 */

export class ParameterPanel {
    /**
     * @param {HTMLElement} container - the DOM element to inject controls into
     */
    constructor(container) {
        this.container = container;
        this._generateCallback = null;
        this._controls = {};

        this._build();
    }

    /**
     * Build the parameter controls DOM.
     */
    _build() {
        const defs = [
            { id: 'N',              label: 'Subdivisions (N)', type: 'range', min: 16,  max: 512, step: 1,    value: 100  },
            { id: 'seed',           label: 'Seed',             type: 'number', min: 0,  max: 999999, step: 1, value: 42   },
            { id: 'octaves',        label: 'Octaves',          type: 'range', min: 1,   max: 10,  step: 1,    value: 6    },
            { id: 'frequency',      label: 'Frequency',        type: 'range', min: 0.5, max: 5.0, step: 0.1,  value: 1.5  },
            { id: 'warp',           label: 'Warp',             type: 'range', min: 0,   max: 1.0, step: 0.01, value: 0    },
            { id: 'ocean_fraction', label: 'Ocean Fraction',   type: 'range', min: 0.3, max: 0.8, step: 0.01, value: 0.55 },
            { id: 'exaggeration',   label: 'Exaggeration',     type: 'range', min: 0,   max: 0.3, step: 0.005,value: 0.05 },
        ];

        for (const def of defs) {
            const group = document.createElement('div');
            group.className = 'param-group';

            const label = document.createElement('label');
            label.setAttribute('for', `param-${def.id}`);

            const labelText = document.createTextNode(def.label + ' ');
            label.appendChild(labelText);

            const valueSpan = document.createElement('span');
            valueSpan.className = 'param-value';
            valueSpan.id = `param-${def.id}-value`;
            valueSpan.textContent = def.value;
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
                    valueSpan.textContent = input.value;
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
            this.container.appendChild(group);
            this._controls[def.id] = input;
        }

        // Generate button
        const btn = document.createElement('button');
        btn.id = 'btn-generate';
        btn.textContent = 'Generate';
        btn.addEventListener('click', () => {
            if (this._generateCallback) {
                this._generateCallback(this.getParams());
            }
        });
        this.container.appendChild(btn);
    }

    /**
     * Get the current parameter values.
     * @returns {Object} parameter object ready for the API
     */
    getParams() {
        return {
            N:              parseInt(this._controls.N.value, 10),
            seed:           parseInt(this._controls.seed.value, 10),
            octaves:        parseInt(this._controls.octaves.value, 10),
            frequency:      parseFloat(this._controls.frequency.value),
            warp:           parseFloat(this._controls.warp.value),
            ocean_fraction: parseFloat(this._controls.ocean_fraction.value),
            exaggeration:   parseFloat(this._controls.exaggeration.value),
        };
    }

    /**
     * Get just the exaggeration value (used for rendering updates without regenerating).
     * @returns {number}
     */
    getExaggeration() {
        return parseFloat(this._controls.exaggeration.value);
    }

    /**
     * Register a callback for the Generate button.
     * @param {function(Object): void} callback - receives getParams() result
     */
    onGenerate(callback) {
        this._generateCallback = callback;
    }

    /**
     * Enable or disable the generate button.
     * @param {boolean} enabled
     */
    setEnabled(enabled) {
        const btn = document.getElementById('btn-generate');
        if (btn) {
            btn.disabled = !enabled;
        }
    }
}
