/**
 * Terrain colormap for elevation-to-color mapping.
 * Matches the WorldEngine terrain specification.
 */

/**
 * Default terrain colormap.
 * Each entry: [normalized_elevation, [r, g, b]] where colors are in 0-1 range.
 */
export const TERRAIN_COLORMAP = [
    [-1.00, [0.00, 0.00, 0.13]],  // deep ocean
    [-0.40, [0.00, 0.15, 0.40]],  // mid ocean
    [-0.05, [0.00, 0.30, 0.70]],  // shallow water
    [ 0.00, [0.76, 0.70, 0.50]],  // coastline
    [ 0.05, [0.13, 0.55, 0.13]],  // lowland green
    [ 0.25, [0.09, 0.45, 0.09]],  // forest green
    [ 0.45, [0.55, 0.35, 0.17]],  // highland brown
    [ 0.70, [0.55, 0.55, 0.55]],  // mountain gray
    [ 0.90, [0.85, 0.85, 0.85]],  // snow line
    [ 1.00, [1.00, 1.00, 1.00]],  // peak white
];

/**
 * Sample a colormap at a given value using linear interpolation.
 * @param {number} value - input value (typically normalized elevation in [-1, 1])
 * @param {Array} colormap - array of [threshold, [r, g, b]] entries, sorted ascending
 * @returns {number[]} [r, g, b] in 0-1 range
 */
export function sampleColormap(value, colormap) {
    if (!colormap || colormap.length === 0) {
        return [1, 1, 1];
    }

    // Clamp to colormap range
    if (value <= colormap[0][0]) {
        return [...colormap[0][1]];
    }
    if (value >= colormap[colormap.length - 1][0]) {
        return [...colormap[colormap.length - 1][1]];
    }

    // Find bracketing entries and interpolate
    for (let i = 0; i < colormap.length - 1; i++) {
        const lo = colormap[i];
        const hi = colormap[i + 1];

        if (value >= lo[0] && value <= hi[0]) {
            const range = hi[0] - lo[0];
            if (range === 0) {
                return [...lo[1]];
            }
            const t = (value - lo[0]) / range;
            return [
                lo[1][0] + t * (hi[1][0] - lo[1][0]),
                lo[1][1] + t * (hi[1][1] - lo[1][1]),
                lo[1][2] + t * (hi[1][2] - lo[1][2]),
            ];
        }
    }

    // Fallback (should not reach here)
    return [...colormap[colormap.length - 1][1]];
}

/**
 * Map an entire field data array to vertex colors using a colormap.
 * @param {Float32Array} fieldData - per-cell scalar values (normalized to [-1, 1])
 * @param {Array} colormap - colormap to use
 * @returns {Float32Array} flat RGB array (length = fieldData.length * 3)
 */
export function mapFieldToColors(fieldData, colormap) {
    const colors = new Float32Array(fieldData.length * 3);

    for (let i = 0; i < fieldData.length; i++) {
        const rgb = sampleColormap(fieldData[i], colormap);
        colors[i * 3]     = rgb[0];
        colors[i * 3 + 1] = rgb[1];
        colors[i * 3 + 2] = rgb[2];
    }

    return colors;
}
