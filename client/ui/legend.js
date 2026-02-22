/**
 * Legend component for gradient and categorical colormaps.
 * Renders into a container element in the sidebar.
 */

import { categoricalColor } from '../renderer/colormap_registry.js';

export class Legend {
    /**
     * @param {HTMLElement} container - DOM element to render legend into
     */
    constructor(container) {
        this.container = container;
    }

    /**
     * Show a gradient legend bar.
     * @param {Array} colormap - [[threshold, [r,g,b]], ...] entries
     * @param {string} minLabel - label for min end
     * @param {string} maxLabel - label for max end
     */
    showGradient(colormap, minLabel = '-1.0', maxLabel = '1.0') {
        this.container.innerHTML = '';
        this.container.style.display = 'block';

        // Build CSS gradient from colormap entries
        const stops = colormap.map(([t, [r, g, b]]) => {
            const pct = ((t - colormap[0][0]) / (colormap[colormap.length - 1][0] - colormap[0][0])) * 100;
            return `rgb(${Math.round(r * 255)},${Math.round(g * 255)},${Math.round(b * 255)}) ${pct.toFixed(1)}%`;
        });

        const bar = document.createElement('div');
        bar.className = 'legend-gradient-bar';
        bar.style.background = `linear-gradient(to right, ${stops.join(', ')})`;
        this.container.appendChild(bar);

        const labels = document.createElement('div');
        labels.className = 'legend-labels';

        const minSpan = document.createElement('span');
        minSpan.textContent = minLabel;
        labels.appendChild(minSpan);

        const maxSpan = document.createElement('span');
        maxSpan.textContent = maxLabel;
        labels.appendChild(maxSpan);

        this.container.appendChild(labels);
    }

    /**
     * Show a categorical legend with color swatches.
     * @param {number} numCategories - number of categories
     * @param {function} [labelFn] - maps index to label string
     */
    showCategorical(numCategories, labelFn) {
        this.container.innerHTML = '';
        this.container.style.display = 'block';

        const maxShow = Math.min(numCategories, 20);

        for (let i = 0; i < maxShow; i++) {
            const item = document.createElement('div');
            item.className = 'legend-swatch-item';

            const swatch = document.createElement('span');
            swatch.className = 'legend-swatch';
            const [r, g, b] = categoricalColor(i);
            swatch.style.backgroundColor = `rgb(${Math.round(r * 255)},${Math.round(g * 255)},${Math.round(b * 255)})`;
            item.appendChild(swatch);

            const label = document.createElement('span');
            label.className = 'legend-swatch-label';
            label.textContent = labelFn ? labelFn(i) : `Plate ${i}`;
            item.appendChild(label);

            this.container.appendChild(item);
        }

        if (numCategories > maxShow) {
            const more = document.createElement('div');
            more.className = 'legend-swatch-label';
            more.textContent = `... ${numCategories} total`;
            this.container.appendChild(more);
        }
    }

    /** Hide the legend. */
    clear() {
        this.container.innerHTML = '';
        this.container.style.display = 'none';
    }
}
