/**
 * WorldEngine API client.
 * Provides fetch wrappers for the server REST API and FILD binary parsing.
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

/**
 * FILD header layout (48 bytes total):
 *   0: uint32 magic      (0x46494C44 = "FILD")
 *   4: uint32 version
 *   8: uint32 N
 *  12: uint32 num_cells
 *  16: uint32 dtype       (0 = float32)
 *  20: uint32 compression (0 = none, 1 = gzip)
 *  24: uint32 payload_size
 *  28: char[16] name
 *  44: uint32 reserved
 */
const FILD_MAGIC = 0x46494C44;
const FILD_HEADER_SIZE = 48;

/**
 * Parse a FILD binary buffer into a Float32Array.
 * Handles optional gzip decompression via pako (loaded globally).
 * @param {ArrayBuffer} buffer - raw FILD data
 * @returns {{ name: string, N: number, numCells: number, data: Float32Array }}
 */
export function parseFILD(buffer) {
    if (buffer.byteLength < FILD_HEADER_SIZE) {
        throw new Error(`FILD buffer too small: ${buffer.byteLength} bytes (need at least ${FILD_HEADER_SIZE})`);
    }

    const headerView = new DataView(buffer);

    // Read header fields (little-endian)
    const magic = headerView.getUint32(0, true);
    if (magic !== FILD_MAGIC) {
        throw new Error(`Invalid FILD magic: 0x${magic.toString(16)} (expected 0x${FILD_MAGIC.toString(16)})`);
    }

    const version = headerView.getUint32(4, true);
    const N = headerView.getUint32(8, true);
    const numCells = headerView.getUint32(12, true);
    const dtype = headerView.getUint32(16, true);
    const compression = headerView.getUint32(20, true);
    const payloadSize = headerView.getUint32(24, true);

    // Read name (16-byte null-terminated string)
    const nameBytes = new Uint8Array(buffer, 28, 16);
    let nameEnd = nameBytes.indexOf(0);
    if (nameEnd === -1) nameEnd = 16;
    const name = new TextDecoder('ascii').decode(nameBytes.slice(0, nameEnd));

    // reserved at offset 44, skip

    // Extract payload
    const payloadBytes = new Uint8Array(buffer, FILD_HEADER_SIZE, payloadSize);

    let decompressed;
    if (compression === 1) {
        // Gzip compressed - decompress with pako
        if (typeof pako === 'undefined') {
            throw new Error('pako library not loaded; cannot decompress gzip FILD payload');
        }
        decompressed = pako.inflate(payloadBytes);
    } else if (compression === 0) {
        decompressed = payloadBytes;
    } else {
        throw new Error(`Unknown FILD compression type: ${compression}`);
    }

    // Convert to Float32Array
    // Ensure proper alignment by copying into a new ArrayBuffer
    const alignedBuffer = new ArrayBuffer(decompressed.byteLength);
    new Uint8Array(alignedBuffer).set(decompressed);
    const data = new Float32Array(alignedBuffer);

    if (data.length !== numCells) {
        console.warn(`FILD cell count mismatch: header says ${numCells}, got ${data.length} floats`);
    }

    return { name, N, numCells, data };
}
