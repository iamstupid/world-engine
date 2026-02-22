/**
 * Colormap registry: gradient and categorical color mapping.
 * Dispatches to the appropriate colormap based on layer metadata.
 */

import { sampleColormap, mapFieldToColors, TERRAIN_COLORMAP } from './colormap.js';

/**
 * Generate a categorical color using the golden angle for maximum contrast.
 * @param {number} index - category index
 * @returns {number[]} [r, g, b] in 0-1 range
 */
export function categoricalColor(index) {
    const hue = (index * 137.508) % 360;
    const s = 0.65;
    const l = 0.55;
    return hslToRgb(hue / 360, s, l);
}

/**
 * Map categorical (uint16) data to RGB colors.
 * @param {Uint16Array} data - per-cell category IDs
 * @param {number} numCategories - total number of categories (for color generation)
 * @returns {Float32Array} flat RGB array (length = data.length * 3)
 */
export function mapCategoricalToColors(data, numCategories) {
    // Pre-compute palette
    const palette = new Float32Array(numCategories * 3);
    for (let i = 0; i < numCategories; i++) {
        const rgb = categoricalColor(i);
        palette[i * 3]     = rgb[0];
        palette[i * 3 + 1] = rgb[1];
        palette[i * 3 + 2] = rgb[2];
    }

    const colors = new Float32Array(data.length * 3);
    for (let i = 0; i < data.length; i++) {
        const cat = data[i] % numCategories;
        colors[i * 3]     = palette[cat * 3];
        colors[i * 3 + 1] = palette[cat * 3 + 1];
        colors[i * 3 + 2] = palette[cat * 3 + 2];
    }
    return colors;
}

/**
 * Map layer data to RGB colors, dispatching by colormap type.
 * @param {Float32Array|Uint16Array} data - per-cell values
 * @param {string} colormapKey - "terrain", "categorical", etc.
 * @param {Object} [opts] - options
 * @param {number} [opts.numCategories] - for categorical colormaps
 * @returns {Float32Array} flat RGB array (length = data.length * 3)
 */
export function mapLayerToColors(data, colormapKey, opts = {}) {
    if (colormapKey === 'categorical') {
        const numCats = opts.numCategories || (Math.max(...data) + 1);
        return mapCategoricalToColors(data, numCats);
    }
    // Default: gradient colormap
    const colormap = colormapKey === 'terrain' ? TERRAIN_COLORMAP : TERRAIN_COLORMAP;
    return mapFieldToColors(data, colormap);
}

// --- HSL to RGB utility ---

function hslToRgb(h, s, l) {
    let r, g, b;
    if (s === 0) {
        r = g = b = l;
    } else {
        const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
        const p = 2 * l - q;
        r = hue2rgb(p, q, h + 1/3);
        g = hue2rgb(p, q, h);
        b = hue2rgb(p, q, h - 1/3);
    }
    return [r, g, b];
}

function hue2rgb(p, q, t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1/6) return p + (q - p) * 6 * t;
    if (t < 1/2) return q;
    if (t < 2/3) return p + (q - p) * (2/3 - t) * 6;
    return p;
}

// Re-export for convenience
export { TERRAIN_COLORMAP, sampleColormap, mapFieldToColors };
