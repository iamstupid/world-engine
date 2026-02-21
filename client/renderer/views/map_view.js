/**
 * Map view (2D equirectangular projection).
 * Renders the geodesic mesh field data onto a flat canvas
 * by projecting each cell center to lat/lon coordinates.
 */

import { sampleColormap, TERRAIN_COLORMAP } from '../colormap.js';

export class MapView {
    /**
     * @param {HTMLCanvasElement} canvas - the map canvas element
     */
    constructor(canvas) {
        this.canvas = canvas;
        this.ctx = canvas.getContext('2d');
        this.visible = false;
    }

    /**
     * Render the mesh field data as an equirectangular map image.
     *
     * @param {Object} meshData - mesh info with vertices, faces, num_cells
     * @param {Float32Array} fieldData - per-cell scalar values
     * @param {Array} [colormap=TERRAIN_COLORMAP] - colormap to use
     */
    render(meshData, fieldData, colormap = TERRAIN_COLORMAP) {
        if (!meshData || !fieldData) return;

        const { positions, faces, num_cells } = meshData;
        const canvas = this.canvas;
        const ctx = this.ctx;

        // Set canvas resolution to fill the viewport
        const parent = canvas.parentElement;
        const width = parent.clientWidth;
        const height = parent.clientHeight;
        canvas.width = width;
        canvas.height = height;

        // Create an image buffer
        const imageData = ctx.createImageData(width, height);
        const pixels = imageData.data;

        // Build a coverage buffer to track which pixels have been painted.
        const valueBuffer = new Float32Array(width * height).fill(NaN);

        // For each face, compute its center, convert to lat/lon, then to pixel coords.
        const numFaces = faces.length / 3;
        const isPerFace = fieldData.length === numFaces;

        for (let f = 0; f < numFaces; f++) {
            const i0 = faces[f * 3];
            const i1 = faces[f * 3 + 1];
            const i2 = faces[f * 3 + 2];

            // Face center from flat positions array
            const cx = (positions[i0 * 3] + positions[i1 * 3] + positions[i2 * 3]) / 3;
            const cy = (positions[i0 * 3 + 1] + positions[i1 * 3 + 1] + positions[i2 * 3 + 1]) / 3;
            const cz = (positions[i0 * 3 + 2] + positions[i1 * 3 + 2] + positions[i2 * 3 + 2]) / 3;

            // Convert to spherical coordinates
            const r = Math.sqrt(cx * cx + cy * cy + cz * cz);
            const lat = Math.asin(cy / r);                 // -PI/2 .. PI/2
            const lon = Math.atan2(cz, cx);                // -PI .. PI

            // Convert to pixel coordinates (equirectangular projection)
            const px = Math.floor(((lon + Math.PI) / (2 * Math.PI)) * width) % width;
            const py = Math.floor(((Math.PI / 2 - lat) / Math.PI) * height);

            const pyc = Math.max(0, Math.min(height - 1, py));
            const pxc = Math.max(0, Math.min(width - 1, px));

            // Get the field value
            let value;
            if (isPerFace) {
                value = fieldData[f];
            } else {
                value = (fieldData[i0] + fieldData[i1] + fieldData[i2]) / 3.0;
            }

            // Paint a small region around the center to reduce gaps.
            // The radius is based on the angular resolution of the mesh.
            const cellRadius = Math.max(1, Math.ceil(Math.sqrt(width * height / numFaces) * 0.6));

            for (let dy = -cellRadius; dy <= cellRadius; dy++) {
                for (let dx = -cellRadius; dx <= cellRadius; dx++) {
                    const sx = (pxc + dx + width) % width;
                    const sy = Math.max(0, Math.min(height - 1, pyc + dy));
                    const idx = sy * width + sx;
                    if (isNaN(valueBuffer[idx])) {
                        valueBuffer[idx] = value;
                    }
                }
            }
        }

        // Second pass: fill remaining NaN pixels with nearest-neighbor
        // Use a simple expansion approach
        let hasNaN = true;
        let iterations = 0;
        const maxIter = 20;

        while (hasNaN && iterations < maxIter) {
            hasNaN = false;
            iterations++;
            for (let y = 0; y < height; y++) {
                for (let x = 0; x < width; x++) {
                    const idx = y * width + x;
                    if (!isNaN(valueBuffer[idx])) continue;

                    // Check 4-connected neighbors
                    const neighbors = [
                        y > 0 ? valueBuffer[(y - 1) * width + x] : NaN,
                        y < height - 1 ? valueBuffer[(y + 1) * width + x] : NaN,
                        x > 0 ? valueBuffer[y * width + (x - 1)] : valueBuffer[y * width + (width - 1)],
                        x < width - 1 ? valueBuffer[y * width + (x + 1)] : valueBuffer[y * width + 0],
                    ];

                    let sum = 0;
                    let count = 0;
                    for (const n of neighbors) {
                        if (!isNaN(n)) {
                            sum += n;
                            count++;
                        }
                    }

                    if (count > 0) {
                        valueBuffer[idx] = sum / count;
                    } else {
                        hasNaN = true;
                    }
                }
            }
        }

        // Convert value buffer to pixel colors
        for (let i = 0; i < width * height; i++) {
            const val = isNaN(valueBuffer[i]) ? 0 : valueBuffer[i];
            const rgb = sampleColormap(val, colormap);
            const pi = i * 4;
            pixels[pi]     = Math.round(rgb[0] * 255);
            pixels[pi + 1] = Math.round(rgb[1] * 255);
            pixels[pi + 2] = Math.round(rgb[2] * 255);
            pixels[pi + 3] = 255;
        }

        ctx.putImageData(imageData, 0, 0);
    }

    /**
     * Show the map view canvas.
     */
    show() {
        this.visible = true;
        this.canvas.classList.add('active');
    }

    /**
     * Hide the map view canvas.
     */
    hide() {
        this.visible = false;
        this.canvas.classList.remove('active');
    }
}
