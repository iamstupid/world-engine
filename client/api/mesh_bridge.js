/**
 * MeshBridge: Promise-based interface to the WASM Web Worker.
 * Replaces IcoGeodesicMesh (client-side JS mesh) and pako-based parseFILD.
 */

let worker = null;
let readyPromise = null;
const pendingRequests = new Map();
let nextId = 0;

function ensureWorker() {
    if (worker) return readyPromise;

    worker = new Worker('/workers/mesh_worker.js');
    readyPromise = new Promise((resolve) => {
        worker.onmessage = function handler(e) {
            if (e.data.type === 'ready') {
                worker.onmessage = handleMessage;
                resolve();
            }
        };
    });
    return readyPromise;
}

function handleMessage(e) {
    const { requestId } = e.data;
    const pending = pendingRequests.get(requestId);
    if (!pending) return;
    pendingRequests.delete(requestId);

    if (e.data.type === 'error') {
        pending.reject(new Error(e.data.message));
    } else {
        pending.resolve(e.data);
    }
}

function sendRequest(msg) {
    return new Promise((resolve, reject) => {
        const requestId = String(nextId++);
        msg.requestId = requestId;
        pendingRequests.set(requestId, { resolve, reject });
        worker.postMessage(msg, msg._transfer || []);
    });
}

/**
 * Build mesh geometry via WASM.
 * @param {number} k - subdivision exponent (N = 2^k)
 * @returns {Promise<{positions: Float32Array, faces: Uint32Array, num_cells: number, N: number}>}
 */
export async function buildMesh(k) {
    await ensureWorker();
    const result = await sendRequest({ type: 'buildMesh', k });
    return {
        positions: result.positions,
        faces: result.faces,
        num_cells: result.numCells,
        N: result.N,
    };
}

/**
 * Parse a FILD binary buffer via WASM (C++ zlib, replaces pako).
 * @param {ArrayBuffer} buffer - raw FILD response
 * @param {number} k - subdivision exponent
 * @returns {Promise<{data: Float32Array|Uint16Array, dtype: string}>}
 */
export async function parseFILDWasm(buffer, k) {
    await ensureWorker();
    const result = await sendRequest({ type: 'parseFILD', buffer, k });
    return { data: result.data, dtype: result.dtype || 'float32' };
}

/**
 * Generate noise terrain entirely in WASM (no server needed).
 * @param {number} k - subdivision exponent
 * @param {Object} params - noise parameters
 * @returns {Promise<{data: Float32Array, numCells: number}>}
 */
export async function generateNoiseWasm(k, params) {
    await ensureWorker();
    const result = await sendRequest({ type: 'generateNoise', k, params });
    return { data: result.data, numCells: result.numCells };
}
