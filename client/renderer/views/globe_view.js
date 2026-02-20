/**
 * Globe view manager.
 * Builds and updates a Three.js mesh from the icosahedral geodesic data
 * with per-vertex coloring and elevation displacement.
 */

import * as THREE from 'three';
import { sampleColormap, TERRAIN_COLORMAP } from '../colormap.js';

export class GlobeView {
    /**
     * @param {import('../engine.js').Engine} engine - the 3D engine instance
     */
    constructor(engine) {
        this.engine = engine;
        this.meshData = null;    // raw mesh info from API (vertices, faces)
        this.geometry = null;    // THREE.BufferGeometry
        this.visible = true;
    }

    /**
     * Set the mesh topology from the server.
     * @param {Object} meshData - mesh info object with vertices and faces arrays
     *   meshData.vertices: Array of [x, y, z]
     *   meshData.faces: Array of [i0, i1, i2]
     *   meshData.num_cells: number of cells
     */
    setMesh(meshData) {
        this.meshData = meshData;
    }

    /**
     * Build or update the globe geometry with field data.
     * Each face gets its own set of vertices to allow per-face or per-vertex coloring
     * without index-sharing issues.
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

        const { vertices, faces } = this.meshData;
        const numFaces = faces.length;

        // Each face has 3 unique vertices (un-indexed for per-face vertex colors)
        const positions = new Float32Array(numFaces * 3 * 3);
        const colors = new Float32Array(numFaces * 3 * 3);

        for (let f = 0; f < numFaces; f++) {
            const [i0, i1, i2] = faces[f];

            // Determine the elevation for this face.
            // Use the face index if fieldData has per-face values,
            // otherwise average the vertex elevations.
            let elevation;
            if (fieldData.length === numFaces) {
                // Per-face field data
                elevation = fieldData[f];
            } else {
                // Per-vertex field data - average the three vertex values
                elevation = (fieldData[i0] + fieldData[i1] + fieldData[i2]) / 3.0;
            }

            const rgb = sampleColormap(elevation, colormap);
            const radius = 1.0 + elevation * exaggeration;

            // Process each vertex of the face
            const vertexIndices = [i0, i1, i2];
            for (let v = 0; v < 3; v++) {
                const vi = vertexIndices[v];
                const vx = vertices[vi][0];
                const vy = vertices[vi][1];
                const vz = vertices[vi][2];

                // Normalize to unit sphere then scale by radius
                const len = Math.sqrt(vx * vx + vy * vy + vz * vz);
                const nx = vx / len;
                const ny = vy / len;
                const nz = vz / len;

                const offset = (f * 3 + v) * 3;
                positions[offset]     = nx * radius;
                positions[offset + 1] = ny * radius;
                positions[offset + 2] = nz * radius;

                colors[offset]     = rgb[0];
                colors[offset + 1] = rgb[1];
                colors[offset + 2] = rgb[2];
            }
        }

        // Build or update geometry
        if (this.geometry) {
            this.geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
            this.geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
            this.geometry.computeVertexNormals();
            this.geometry.attributes.position.needsUpdate = true;
            this.geometry.attributes.color.needsUpdate = true;
        } else {
            this.geometry = new THREE.BufferGeometry();
            this.geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
            this.geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
            this.geometry.computeVertexNormals();
        }

        // Set the mesh on the engine
        const material = new THREE.MeshStandardMaterial({
            vertexColors: true,
            roughness: 0.7,
            metalness: 0.0,
            side: THREE.DoubleSide,
            flatShading: true,
        });

        this.engine.setMesh(this.geometry, material);

        if (!this.visible) {
            this.hide();
        }
    }

    /**
     * Show the globe view.
     */
    show() {
        this.visible = true;
        if (this.engine.terrainMesh) {
            this.engine.terrainMesh.visible = true;
        }
        this.engine.oceanMesh.visible = this.engine.oceanVisible;
        this.engine.renderer.domElement.style.display = 'block';
    }

    /**
     * Hide the globe view.
     */
    hide() {
        this.visible = false;
        if (this.engine.terrainMesh) {
            this.engine.terrainMesh.visible = false;
        }
        this.engine.oceanMesh.visible = false;
    }
}
