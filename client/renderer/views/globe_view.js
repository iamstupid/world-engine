/**
 * Globe view manager.
 * Builds and updates a Three.js mesh from the icosahedral geodesic data
 * using indexed geometry with per-vertex coloring and elevation displacement.
 */

import * as THREE from 'three';
import { sampleColormap, TERRAIN_COLORMAP } from '../colormap.js';

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
     * Build the globe geometry with field data using indexed geometry.
     * Position buffer has numCells vertices (shared via index buffer),
     * each displaced radially by its own elevation.
     *
     * @param {Float32Array} fieldData - per-cell scalar data (length = num_cells)
     * @param {Array} [colormap=TERRAIN_COLORMAP] - colormap to apply
     * @param {number} [exaggeration=0.05] - radial displacement scale
     */
    update(fieldData, colormap = TERRAIN_COLORMAP, exaggeration = 0.05) {
        if (!this.meshData) {
            console.warn('GlobeView.update: no mesh data set');
            return;
        }

        const { positions: basePositions, faces, num_cells } = this.meshData;

        // Build displaced positions and per-vertex colors
        const positions = new Float32Array(num_cells * 3);
        const colors = new Float32Array(num_cells * 3);

        for (let i = 0; i < num_cells; i++) {
            const bx = basePositions[i * 3];
            const by = basePositions[i * 3 + 1];
            const bz = basePositions[i * 3 + 2];

            // Positions are already on unit sphere, just scale by radius
            const radius = 1.0 + fieldData[i] * exaggeration;
            positions[i * 3]     = bx * radius;
            positions[i * 3 + 1] = by * radius;
            positions[i * 3 + 2] = bz * radius;

            const rgb = sampleColormap(fieldData[i], colormap);
            colors[i * 3]     = rgb[0];
            colors[i * 3 + 1] = rgb[1];
            colors[i * 3 + 2] = rgb[2];
        }

        // Always create a fresh geometry (engine.setMesh disposes the old one)
        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
        geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
        geometry.setIndex(new THREE.BufferAttribute(faces, 1));
        geometry.computeVertexNormals();

        const material = new THREE.MeshStandardMaterial({
            vertexColors: true,
            roughness: 0.7,
            metalness: 0.0,
            side: THREE.DoubleSide,
            flatShading: false,
        });

        this.engine.setMesh(geometry, material);

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
