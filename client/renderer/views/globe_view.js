/**
 * Globe view manager.
 * Builds and updates a Three.js mesh from the icosahedral geodesic data
 * using indexed geometry with per-vertex coloring and elevation displacement.
 *
 * Displacement is always driven by elevation data (or flat if null).
 * Coloring is driven by pre-computed RGB colors passed by the caller.
 */

import * as THREE from 'three';

export class GlobeView {
    /**
     * @param {import('../engine.js').Engine} engine - the 3D engine instance
     */
    constructor(engine) {
        this.engine = engine;
        this.meshData = null;
        this.visible = true;
    }

    /**
     * Set the mesh topology.
     * @param {Object} meshData
     *   meshData.positions: Float32Array (numCells * 3)
     *   meshData.faces: Uint32Array (numFaces * 3)
     *   meshData.num_cells: number
     */
    setMesh(meshData) {
        this.meshData = meshData;
    }

    /**
     * Build the globe geometry with pre-computed colors and optional elevation displacement.
     *
     * @param {Float32Array|null} elevationData - per-cell elevation for displacement (null = flat sphere)
     * @param {Float32Array} colors - pre-computed RGB (num_cells * 3)
     * @param {number} [exaggeration=0.05] - radial displacement scale
     */
    update(elevationData, colors, exaggeration = 0.05) {
        if (!this.meshData) {
            console.warn('GlobeView.update: no mesh data set');
            return;
        }

        const { positions: basePositions, faces, num_cells } = this.meshData;

        // Build displaced positions
        const positions = new Float32Array(num_cells * 3);

        for (let i = 0; i < num_cells; i++) {
            const bx = basePositions[i * 3];
            const by = basePositions[i * 3 + 1];
            const bz = basePositions[i * 3 + 2];

            const elev = elevationData ? elevationData[i] : 0;
            const radius = 1.0 + elev * exaggeration;
            positions[i * 3]     = bx * radius;
            positions[i * 3 + 1] = by * radius;
            positions[i * 3 + 2] = bz * radius;
        }

        // Always create a fresh geometry (engine.setMesh disposes the old one)
        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
        geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
        geometry.setIndex(new THREE.BufferAttribute(faces, 1));

        // WebGL limits indices per draw call (~30M). Split into groups if needed.
        const MAX_INDICES = 29999997; // largest multiple of 3 under 30M
        if (faces.length > MAX_INDICES) {
            for (let off = 0; off < faces.length; off += MAX_INDICES) {
                geometry.addGroup(off, Math.min(MAX_INDICES, faces.length - off), 0);
            }
        }

        geometry.computeVertexNormals();

        const material = new THREE.MeshStandardMaterial({
            vertexColors: true,
            roughness: 0.7,
            metalness: 0.0,
            side: THREE.DoubleSide,
            flatShading: false,
        });

        // Pass material as array when using draw groups so Three.js dispatches per-group
        const mat = geometry.groups.length > 0 ? [material] : material;
        this.engine.setMesh(geometry, mat);

        if (!this.visible) {
            this.hide();
        }
    }

    show() {
        this.visible = true;
        if (this.engine.terrainMesh) {
            this.engine.terrainMesh.visible = true;
        }
        this.engine.oceanMesh.visible = this.engine.oceanVisible;
        this.engine.renderer.domElement.style.display = 'block';
    }

    hide() {
        this.visible = false;
        if (this.engine.terrainMesh) {
            this.engine.terrainMesh.visible = false;
        }
        this.engine.oceanMesh.visible = false;
    }
}
