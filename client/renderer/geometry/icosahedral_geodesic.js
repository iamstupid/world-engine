/**
 * IcoGeodesicMesh — Client-side reconstruction of icosahedral geodesic mesh
 * geometry from a subdivision level N. This is a direct port of the C++
 * IcoMesh implementation in library/mesh/icosahedral_geodesic.cpp.
 *
 * Layout:
 *   total_rows  = 3N + 1
 *   total_cells = 10N^2 + 2
 *
 * Row widths:
 *   row 0           -> 1            (north pole)
 *   rows 1..N-1     -> 5r           (north cap)
 *   rows N..2N      -> 5N           (equatorial band)
 *   rows 2N+1..3N-1 -> 5(3N - r)   (south cap)
 *   row 3N          -> 1            (south pole)
 *
 * @module icosahedral_geodesic
 */

import * as THREE from 'three';

// ============================================================
// Constants
// ============================================================

const WARP_ALPHA = 0.5372;
const WARP_BETA  = -0.4637;

/**
 * 20 icosahedral faces defined by indices into the 12 base vertices.
 * Vertex indices:
 *   0        = north pole
 *   1..5     = upper ring (z = +1/sqrt(5))
 *   6..10    = lower ring (z = -1/sqrt(5), offset 36 deg)
 *   11       = south pole
 */
const FACE_VERTS = [
    // North cap (faces 0-4): north pole + upper ring
    [0, 1, 2], [0, 2, 3], [0, 3, 4], [0, 4, 5], [0, 5, 1],
    // Equatorial band (faces 5-14): alternating up/down
    [1, 6, 2],  [6, 7, 2],    // pair 0
    [2, 7, 3],  [7, 8, 3],    // pair 1
    [3, 8, 4],  [8, 9, 4],    // pair 2
    [4, 9, 5],  [9, 10, 5],   // pair 3
    [5, 10, 1], [10, 6, 1],   // pair 4
    // South cap (faces 15-19): south pole + lower ring
    [11, 7, 6], [11, 8, 7], [11, 9, 8], [11, 10, 9], [11, 6, 10]
];

// ============================================================
// Icosahedron base vertices
// ============================================================

function buildIcoVerts() {
    const verts = new Array(12);

    // North pole
    verts[0] = [0, 0, 1];

    // Upper ring: 5 vertices at z = 1/sqrt(5)
    const upperZ = 1.0 / Math.sqrt(5.0);
    const upperR = 2.0 / Math.sqrt(5.0);
    for (let i = 0; i < 5; i++) {
        const angle = 2.0 * Math.PI * i / 5.0;
        verts[1 + i] = [
            upperR * Math.cos(angle),
            upperR * Math.sin(angle),
            upperZ
        ];
    }

    // Lower ring: 5 vertices at z = -1/sqrt(5), offset by 36 degrees
    for (let i = 0; i < 5; i++) {
        const angle = 2.0 * Math.PI * i / 5.0 + Math.PI / 5.0;
        verts[6 + i] = [
            upperR * Math.cos(angle),
            upperR * Math.sin(angle),
            -upperZ
        ];
    }

    // South pole
    verts[11] = [0, 0, -1];

    return verts;
}

// ============================================================
// IcoGeodesicMesh class
// ============================================================

export class IcoGeodesicMesh {
    /**
     * @param {number} N - Subdivision level (must be >= 1)
     */
    constructor(N) {
        if (N < 1) {
            throw new Error('Subdivision level N must be >= 1');
        }

        this.N = N;
        this.totalRows = 3 * N + 1;
        this.totalCells = 10 * N * N + 2;

        this._icoVerts = buildIcoVerts();

        // Build row table
        this._rowStart = new Int32Array(this.totalRows);
        this._rowWidth = new Int32Array(this.totalRows);
        this._buildRowTable();

        // Lazy caches
        this._positions = null;
        this._faces = null;
    }

    // --------------------------------------------------------
    // Row table
    // --------------------------------------------------------

    _buildRowTable() {
        const N = this.N;
        const totalRows = this.totalRows;
        let offset = 0;

        for (let r = 0; r < totalRows; r++) {
            this._rowStart[r] = offset;

            if (r === 0 || r === totalRows - 1) {
                this._rowWidth[r] = 1;
            } else if (r <= N - 1) {
                this._rowWidth[r] = 5 * r;
            } else if (r <= 2 * N) {
                this._rowWidth[r] = 5 * N;
            } else {
                this._rowWidth[r] = 5 * (3 * N - r);
            }

            offset += this._rowWidth[r];
        }

        // Sanity check
        if (offset !== this.totalCells) {
            throw new Error(
                `Row table mismatch: accumulated ${offset} cells, expected ${this.totalCells}`
            );
        }
    }

    /**
     * Return the starting flat index for a given row.
     * @param {number} row
     * @returns {number}
     */
    rowStart(row) {
        return this._rowStart[row];
    }

    /**
     * Return the width (number of cells) for a given row.
     * @param {number} row
     * @returns {number}
     */
    rowWidth(row) {
        return this._rowWidth[row];
    }

    /**
     * Binary search for the row that contains flat index `idx`.
     * @param {number} idx
     * @returns {number}
     */
    rowOf(idx) {
        let lo = 0;
        let hi = this.totalRows - 1;
        while (lo < hi) {
            const mid = (lo + hi + 1) >> 1;
            if (this._rowStart[mid] <= idx) {
                lo = mid;
            } else {
                hi = mid - 1;
            }
        }
        return lo;
    }

    /**
     * Return the column within its row for flat index `idx`.
     * @param {number} idx
     * @returns {number}
     */
    colOf(idx) {
        const r = this.rowOf(idx);
        return idx - this._rowStart[r];
    }

    // --------------------------------------------------------
    // Position computation — fused face_id + barycentrics + warp
    // --------------------------------------------------------

    /**
     * Compute the unit-sphere position for a cell at (row, col).
     * Matches the C++ IcoMesh::compute_position exactly.
     *
     * @param {number} row
     * @param {number} col
     * @returns {number[]} [x, y, z]
     */
    _computePosition(row, col) {
        const N = this.N;
        const icoVerts = this._icoVerts;

        // Poles
        if (row === 0) return [icoVerts[0][0], icoVerts[0][1], icoVerts[0][2]];
        if (row === 3 * N) return [icoVerts[11][0], icoVerts[11][1], icoVerts[11][2]];

        const N2 = 2 * N;

        // Zone classification
        const zone = (row <= N) ? 0 : ((row > N2) ? 2 : 1);
        const stride = (zone === 0) ? row : ((zone === 2) ? (3 * N - row) : N);
        const colNo  = (col / stride) | 0;
        const colRem = col % stride;

        // Face ID
        let fid;
        if (zone === 1) {
            const d = row - N;
            const isDown = (d + colRem) > N;
            fid = 5 + colNo * 2 + (isDown ? 1 : 0);
        } else {
            fid = (zone === 0 ? 0 : 15) + colNo;
        }

        // Barycentric coordinates
        let u, v, w;
        if (zone === 0) {
            // North cap
            u = (N - row) / N;
            v = (row - colRem) / N;
            w = colRem / N;
        } else if (zone === 2) {
            // South cap
            const mirror = 3 * N - row;
            u = (N - mirror) / N;
            v = colRem / N;
            w = (mirror - colRem) / N;
        } else {
            // Equatorial band
            const d = row - N;
            if ((d + colRem) <= N) {
                // Up-face
                u = (N - d - colRem) / N;
                v = d / N;
                w = colRem / N;
            } else {
                // Down-face
                u = (N - colRem) / N;
                v = (d + colRem - N) / N;
                w = (N - d) / N;
            }
        }

        // Coupled polynomial warp
        let uw = u + WARP_ALPHA * u * (1.0 - u) * (0.5 - u);
        let vw = v + WARP_ALPHA * v * (1.0 - v) * (0.5 - v);
        let ww = w + WARP_ALPHA * w * (1.0 - w) * (0.5 - w);

        const coupled = u * v * w * WARP_BETA;
        uw += coupled;
        vw += coupled;
        ww += coupled;

        const s = uw + vw + ww;
        uw /= s;
        vw /= s;
        ww /= s;

        // Project to sphere: pos = A*uw + B*vw + C*ww, then normalize
        const face = FACE_VERTS[fid];
        const A = icoVerts[face[0]];
        const B = icoVerts[face[1]];
        const C = icoVerts[face[2]];

        const px = A[0] * uw + B[0] * vw + C[0] * ww;
        const py = A[1] * uw + B[1] * vw + C[1] * ww;
        const pz = A[2] * uw + B[2] * vw + C[2] * ww;

        const len = Math.sqrt(px * px + py * py + pz * pz);
        return [px / len, py / len, pz / len];
    }

    // --------------------------------------------------------
    // Lazy positions builder
    // --------------------------------------------------------

    /**
     * Build and cache all vertex positions as a flat Float32Array.
     * Layout: [x0, y0, z0, x1, y1, z1, ...]
     */
    _buildPositions() {
        const count = this.totalCells;
        const positions = new Float32Array(count * 3);

        let idx = 0;
        for (let r = 0; r < this.totalRows; r++) {
            const w = this._rowWidth[r];
            for (let c = 0; c < w; c++) {
                const p = this._computePosition(r, c);
                positions[idx * 3]     = p[0];
                positions[idx * 3 + 1] = p[1];
                positions[idx * 3 + 2] = p[2];
                idx++;
            }
        }

        this._positions = positions;
    }

    /**
     * Return a Float32Array of all cell positions (numCells * 3).
     * Lazily computed on first call.
     *
     * @returns {Float32Array}
     */
    getPositions() {
        if (this._positions === null) {
            this._buildPositions();
        }
        return this._positions;
    }

    // --------------------------------------------------------
    // Lazy face generation
    // --------------------------------------------------------

    /**
     * Map face-local grid (i, j) to global cell index.
     * i, j are integer barycentric weights: i + j <= N, k = N - i - j.
     *
     * @param {number} fid - Icosahedral face index (0..19)
     * @param {number} i - First barycentric weight (0..N)
     * @param {number} j - Second barycentric weight (0..N-i)
     * @returns {number} Global cell index
     */
    _faceLocalToCell(fid, i, j) {
        const N = this.N;
        let row, colRaw, rowW;

        if (fid < 5) {
            // North cap face f: {pole, upper[f], upper[f+1]}
            row = N - i;
            colRaw = fid * (N - i) + (N - i - j);
            rowW = (row === 0) ? 1 : 5 * row;
        } else if (fid < 15) {
            const s = (fid - 5) >> 1;
            if ((fid - 5) % 2 === 0) {
                // Equatorial up-face: {upper[s], lower[s], upper[s+1]}
                row = N + j;
                colRaw = s * N + (N - i - j);
            } else {
                // Equatorial down-face: {lower[s], lower[s+1], upper[s+1]}
                row = N + i + j;
                colRaw = s * N + (N - i);
            }
            rowW = 5 * N;
        } else {
            // South cap face f: {pole, lower[(f+1)%5], lower[f]}
            const f = fid - 15;
            row = 2 * N + i;
            colRaw = f * (N - i) + j;
            rowW = (row === 3 * N) ? 1 : 5 * (3 * N - row);
        }

        const col = ((colRaw % rowW) + rowW) % rowW;
        return this._rowStart[row] + col;
    }

    /**
     * Build the triangle face list by iterating over the 20 icosahedral faces.
     * Each face is subdivided into N^2 sub-triangles aligned with the face grid.
     * Total: 20 * N^2 triangles stored as three consecutive vertex indices.
     */
    _buildFaces() {
        const N = this.N;
        const faceList = [];

        for (let fid = 0; fid < 20; fid++) {
            for (let i = 0; i < N; i++) {
                for (let j = 0; j < N - i; j++) {
                    // Up sub-triangle: (i,j) -> (i+1,j) -> (i,j+1)
                    const a = this._faceLocalToCell(fid, i, j);
                    const b = this._faceLocalToCell(fid, i + 1, j);
                    const c = this._faceLocalToCell(fid, i, j + 1);
                    faceList.push(a, b, c);

                    // Down sub-triangle: (i+1,j) -> (i+1,j+1) -> (i,j+1)
                    if (i + j + 1 < N) {
                        const d = this._faceLocalToCell(fid, i + 1, j + 1);
                        faceList.push(b, d, c);
                    }
                }
            }
        }

        this._faces = new Uint32Array(faceList);
    }

    /**
     * Return a Uint32Array of triangle indices (numFaces * 3).
     * Lazily computed on first call.
     *
     * @returns {Uint32Array}
     */
    getFaces() {
        if (this._faces === null) {
            this._buildFaces();
        }
        return this._faces;
    }

    /**
     * Return the number of triangular faces.
     * @returns {number}
     */
    get numFaces() {
        return this.getFaces().length / 3;
    }

    // --------------------------------------------------------
    // Analytic O(1) neighbor computation
    // --------------------------------------------------------

    /**
     * Compute the neighbors of cell at (row, col) analytically.
     *
     * In (Δrow, Δcol_rem) space the offsets are:
     *   row-1 (contracting): col_rem + {0, -1}
     *   row  0 (same row):   col     + {-1, +1}
     *   row+1 (expanding):   col_rem + {0, +1}
     *
     * Edge cells at col_rem=0 sit on icosahedral face boundaries:
     * the contracting c-1 wraps wrong, so use only c+0; the
     * expanding direction gains a third cross-face neighbor at c-1.
     * Deduplication collapses duplicates at pentagon vertices (5 nbrs).
     *
     * @param {number} row
     * @param {number} col
     * @returns {number[]} Array of neighbor cell indices (length 5 or 6)
     */
    computeNeighbors(row, col) {
        const N = this.N;
        const out = [];

        const add = (r, c) => {
            const w = this._rowWidth[r];
            const cc = ((c % w) + w) % w;
            const idx = this._rowStart[r] + cc;
            if (!out.includes(idx)) out.push(idx);
        };

        // Poles
        if (row === 0) {
            for (let k = 0; k < 5; k++) add(1, k);
            return out;
        }
        if (row === 3 * N) {
            for (let k = 0; k < 5; k++) add(3 * N - 1, k);
            return out;
        }

        // Same-row neighbors (all zones)
        add(row, col - 1);
        add(row, col + 1);

        if (row <= N - 1) {
            // North cap interior (rows 1..N-1)
            const f = (col / row) | 0;
            const c = col % row;

            // Down (expanding): row+1, stride grows by 1
            const next = (row + 1 <= N - 1) ? (row + 1) : N;
            const rdown = row + 1;
            add(rdown, f * next + c);
            add(rdown, f * next + c + 1);
            if (c === 0) add(rdown, f * next + c - 1);

            // Up (contracting): row-1
            if (row === 1) {
                add(0, 0);
            } else {
                const prev = row - 1;
                if (c === 0) {
                    add(row - 1, f * prev + c);
                } else {
                    add(row - 1, f * prev + c - 1);
                    add(row - 1, f * prev + c);
                }
            }

        } else if (row === N) {
            // Row N: north cap / equatorial boundary
            const f = (col / N) | 0;
            const c = col % N;

            // Down into equatorial
            add(N + 1, col - 1);
            add(N + 1, col);

            // Up into cap (contracting)
            if (N === 1) {
                add(0, 0);
            } else if (c === 0) {
                add(N - 1, f * (N - 1) + c);
            } else {
                add(N - 1, f * (N - 1) + c - 1);
                add(N - 1, f * (N - 1) + c);
            }

        } else if (row > N && row < 2 * N) {
            // Equatorial interior (rows N+1..2N-1)
            add(row - 1, col);
            add(row - 1, col + 1);
            add(row + 1, col - 1);
            add(row + 1, col);

        } else if (row === 2 * N) {
            // Row 2N: equatorial / south cap boundary
            const f = (col / N) | 0;
            const c = col % N;

            // Up into equatorial
            add(2 * N - 1, col);
            add(2 * N - 1, col + 1);

            // Down into south cap (contracting)
            if (N === 1) {
                add(3 * N, 0);
            } else if (c === 0) {
                add(2 * N + 1, f * (N - 1) + c);
            } else {
                add(2 * N + 1, f * (N - 1) + c - 1);
                add(2 * N + 1, f * (N - 1) + c);
            }

        } else {
            // South cap interior (rows 2N+1..3N-1)
            const mirror = 3 * N - row;
            const f = (col / mirror) | 0;
            const c = col % mirror;

            // Up (expanding): row-1, stride grows by 1
            const next = (row - 1 >= 2 * N + 1) ? (mirror + 1) : N;
            const rup = row - 1;
            add(rup, f * next + c);
            add(rup, f * next + c + 1);
            if (c === 0) add(rup, f * next + c - 1);

            // Down (contracting): row+1
            if (mirror === 1) {
                add(3 * N, 0);
            } else {
                const prev = mirror - 1;
                if (c === 0) {
                    add(row + 1, f * prev + c);
                } else {
                    add(row + 1, f * prev + c - 1);
                    add(row + 1, f * prev + c);
                }
            }
        }

        return out;
    }

    /**
     * Compute neighbors for a cell given its flat index.
     * @param {number} idx - Flat cell index
     * @returns {number[]} Array of neighbor cell indices
     */
    computeNeighborsByIdx(idx) {
        const row = this.rowOf(idx);
        const col = idx - this._rowStart[row];
        return this.computeNeighbors(row, col);
    }

    // --------------------------------------------------------
    // THREE.js geometry builders
    // --------------------------------------------------------

    /**
     * Create a THREE.BufferGeometry from field data and a colormap.
     *
     * Positions are unit-sphere vertices. Colors are mapped from fieldData
     * through the colormap function.
     *
     * @param {Float32Array|number[]} fieldData - Scalar value per cell (length = totalCells)
     * @param {function(number): [number, number, number]} colormap -
     *     Maps a scalar value to [r, g, b] each in [0, 1].
     * @returns {THREE.BufferGeometry}
     */
    createGeometry(fieldData, colormap) {
        const positions = this.getPositions();
        const faces = this.getFaces();
        const numCells = this.totalCells;

        // Build per-vertex colors
        const colors = new Float32Array(numCells * 3);
        for (let i = 0; i < numCells; i++) {
            const rgb = colormap(fieldData[i]);
            colors[i * 3]     = rgb[0];
            colors[i * 3 + 1] = rgb[1];
            colors[i * 3 + 2] = rgb[2];
        }

        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
        geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
        geometry.setIndex(new THREE.BufferAttribute(faces, 1));
        geometry.computeVertexNormals();

        return geometry;
    }

    /**
     * Create a THREE.BufferGeometry with radial displacement.
     *
     * Each vertex is pushed outward from the unit sphere by an amount
     * proportional to its field value, scaled by the exaggeration factor.
     * The final radius for cell i is: 1.0 + fieldData[i] * exaggeration.
     *
     * @param {Float32Array|number[]} fieldData - Scalar value per cell (length = totalCells)
     * @param {function(number): [number, number, number]} colormap -
     *     Maps a scalar value to [r, g, b] each in [0, 1].
     * @param {number} exaggeration - Scale factor for radial displacement.
     * @returns {THREE.BufferGeometry}
     */
    createDisplacedGeometry(fieldData, colormap, exaggeration) {
        const basePositions = this.getPositions();
        const faces = this.getFaces();
        const numCells = this.totalCells;

        // Build displaced positions and colors
        const displacedPositions = new Float32Array(numCells * 3);
        const colors = new Float32Array(numCells * 3);

        for (let i = 0; i < numCells; i++) {
            const val = fieldData[i];
            const radius = 1.0 + val * exaggeration;

            displacedPositions[i * 3]     = basePositions[i * 3]     * radius;
            displacedPositions[i * 3 + 1] = basePositions[i * 3 + 1] * radius;
            displacedPositions[i * 3 + 2] = basePositions[i * 3 + 2] * radius;

            const rgb = colormap(val);
            colors[i * 3]     = rgb[0];
            colors[i * 3 + 1] = rgb[1];
            colors[i * 3 + 2] = rgb[2];
        }

        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute('position', new THREE.BufferAttribute(displacedPositions, 3));
        geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
        geometry.setIndex(new THREE.BufferAttribute(faces, 1));
        geometry.computeVertexNormals();

        return geometry;
    }
}
