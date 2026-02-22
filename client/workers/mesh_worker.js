/**
 * Web Worker: loads the WASM module and handles mesh, FILD, and noise operations.
 *
 * Messages IN:
 *   { type: 'buildMesh', k, requestId }
 *   { type: 'parseFILD', buffer: ArrayBuffer, k, requestId }
 *   { type: 'generateNoise', k, params: {seed, octaves, frequency, ...}, requestId }
 *
 * Messages OUT:
 *   { type: 'meshReady', positions, faces, numCells, N, requestId }
 *   { type: 'fildReady', data, requestId }
 *   { type: 'noiseReady', data, numCells, requestId }
 *   { type: 'error', message, requestId }
 *
 * Typed arrays are transferred (zero-copy) to the main thread.
 */

let Module = null;

importScripts('/wasm/worldengine.js');

WorldEngineModule({
    locateFile: (path) => '/wasm/' + path
}).then(mod => {
    Module = mod;
    postMessage({ type: 'ready' });
});

onmessage = function(e) {
    const { type, requestId } = e.data;

    if (!Module) {
        postMessage({ type: 'error', message: 'WASM module not loaded yet', requestId });
        return;
    }

    try {
        if (type === 'buildMesh') {
            const k = e.data.k;

            // typed_memory_view gives a view into the WASM heap — must copy before transfer
            const positions = new Float32Array(Module.buildPositions(k));
            const faces = new Uint32Array(Module.buildFaces(k));
            const numCells = Module.getNumCells(k);
            const N = Module.getN(k);

            postMessage(
                { type: 'meshReady', positions, faces, numCells, N, requestId },
                [positions.buffer, faces.buffer]
            );

        } else if (type === 'parseFILD') {
            const buffer = e.data.buffer;
            const k = e.data.k;
            const bytes = new Uint8Array(buffer);

            // Copy buffer into WASM heap
            const ptr = Module._malloc(bytes.length);
            Module.HEAPU8.set(bytes, ptr);

            // Parse via C++ (returns typed_memory_view into static result)
            const resultView = Module.parseFILD(ptr, bytes.length, k);
            const data = new Float32Array(resultView);

            Module._free(ptr);

            postMessage(
                { type: 'fildReady', data, requestId },
                [data.buffer]
            );

        } else if (type === 'generateNoise') {
            const k = e.data.k;
            const p = e.data.params;

            const resultView = Module.generateNoise(
                k, p.seed, p.octaves, p.frequency,
                p.lacunarity, p.gain, p.warp_amplitude, p.ocean_fraction
            );
            const data = new Float32Array(resultView);
            const numCells = Module.getNumCells(k);

            postMessage(
                { type: 'noiseReady', data, numCells, requestId },
                [data.buffer]
            );
        }
    } catch (err) {
        postMessage({ type: 'error', message: err.message || String(err), requestId });
    }
};
