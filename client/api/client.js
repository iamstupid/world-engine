/**
 * WorldEngine API client.
 * Provides fetch wrappers for the server REST API.
 * FILD parsing is handled by the WASM Worker (see mesh_bridge.js).
 */

const API_BASE = '/api/v1';

/**
 * Fetch mesh info for an icosahedral subdivision level.
 * @param {number} N - subdivision parameter
 * @returns {Promise<Object>} mesh metadata (vertices, faces, adjacency, etc.)
 */
export async function getMeshInfo(N) {
    const resp = await fetch(`${API_BASE}/mesh/ico/${N}`);
    if (!resp.ok) {
        throw new Error(`getMeshInfo failed: ${resp.status} ${resp.statusText}`);
    }
    return resp.json();
}

/**
 * Request noise-based terrain generation.
 * @param {Object} params - generation parameters
 * @param {number} params.N - subdivision level
 * @param {number} params.seed - random seed
 * @param {number} params.octaves - noise octaves
 * @param {number} params.frequency - base frequency
 * @param {number} params.warp - domain warp amount
 * @param {number} params.ocean_fraction - fraction of surface that is ocean
 * @returns {Promise<Object>} world generation result with world_id
 */
export async function generateNoise(params) {
    const resp = await fetch(`${API_BASE}/generate/noise`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(params),
    });
    if (!resp.ok) {
        const text = await resp.text();
        throw new Error(`generateNoise failed: ${resp.status} ${resp.statusText} - ${text}`);
    }
    return resp.json();
}

/**
 * Fetch raw field data for a world in FILD binary format.
 * @param {string} worldId - world identifier
 * @param {string} fieldName - field name (e.g. "elevation")
 * @returns {Promise<ArrayBuffer>} raw FILD binary data
 */
export async function getFieldData(worldId, fieldName) {
    const resp = await fetch(`${API_BASE}/world/${worldId}/field/${fieldName}`);
    if (!resp.ok) {
        throw new Error(`getFieldData failed: ${resp.status} ${resp.statusText}`);
    }
    return resp.arrayBuffer();
}

/**
 * Fetch statistics for a field.
 * @param {string} worldId - world identifier
 * @param {string} fieldName - field name
 * @returns {Promise<Object>} field statistics (min, max, mean, etc.)
 */
export async function getFieldStats(worldId, fieldName) {
    const resp = await fetch(`${API_BASE}/world/${worldId}/field/${fieldName}/stats`);
    if (!resp.ok) {
        throw new Error(`getFieldStats failed: ${resp.status} ${resp.statusText}`);
    }
    return resp.json();
}
