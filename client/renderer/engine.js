/**
 * Three.js scene setup and render loop for WorldEngine.
 */

import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

export class Engine {
    /**
     * @param {HTMLElement} container - DOM element to mount the renderer into
     */
    constructor(container) {
        this.container = container;

        // Renderer
        this.renderer = new THREE.WebGLRenderer({ antialias: true });
        this.renderer.setPixelRatio(window.devicePixelRatio);
        this.renderer.setClearColor(0x0a0a1a, 1);
        this._updateSize();
        container.appendChild(this.renderer.domElement);

        // Scene
        this.scene = new THREE.Scene();

        // Camera
        const aspect = container.clientWidth / container.clientHeight;
        this.camera = new THREE.PerspectiveCamera(50, aspect, 0.01, 100);
        this.camera.position.set(0, 0, 3);

        // OrbitControls
        this.controls = new OrbitControls(this.camera, this.renderer.domElement);
        this.controls.enableDamping = true;
        this.controls.dampingFactor = 0.08;
        this.controls.minDistance = 1.2;
        this.controls.maxDistance = 10;

        // Lights
        this.dirLight = new THREE.DirectionalLight(0xffffff, 1.0);
        this.dirLight.position.set(5, 3, 4);
        this.scene.add(this.dirLight);

        this.hemiLight = new THREE.HemisphereLight(0x87ceeb, 0x1a1a2e, 0.4);
        this.scene.add(this.hemiLight);

        // Ocean sphere (semi-transparent blue at r=1.0)
        const oceanGeo = new THREE.SphereGeometry(1.0, 64, 48);
        const oceanMat = new THREE.MeshStandardMaterial({
            color: 0x0055aa,
            transparent: true,
            opacity: 0.35,
            roughness: 0.2,
            metalness: 0.0,
            side: THREE.FrontSide,
            depthWrite: false,
        });
        this.oceanMesh = new THREE.Mesh(oceanGeo, oceanMat);
        this.oceanMesh.renderOrder = 1;
        this.scene.add(this.oceanMesh);
        this.oceanVisible = true;

        // Current terrain mesh reference
        this.terrainMesh = null;

        // Resize listener
        this._onResize = () => this._updateSize();
        window.addEventListener('resize', this._onResize);

        // Start render loop
        this._animating = true;
        this._animate();
    }

    /**
     * Set or replace the terrain mesh in the scene.
     * @param {THREE.BufferGeometry} geometry - the terrain geometry to display
     * @param {THREE.Material} [material] - optional custom material
     */
    setMesh(geometry, material) {
        this.clear();

        const mat = material || new THREE.MeshStandardMaterial({
            vertexColors: true,
            roughness: 0.7,
            metalness: 0.0,
            side: THREE.DoubleSide,
            flatShading: true,
        });

        this.terrainMesh = new THREE.Mesh(geometry, mat);
        this.terrainMesh.renderOrder = 0;
        this.scene.add(this.terrainMesh);
    }

    /**
     * Remove the current terrain mesh from the scene.
     */
    clear() {
        if (this.terrainMesh) {
            this.scene.remove(this.terrainMesh);
            this.terrainMesh.geometry.dispose();
            if (this.terrainMesh.material.dispose) {
                this.terrainMesh.material.dispose();
            }
            this.terrainMesh = null;
        }
    }

    /**
     * Toggle the ocean sphere visibility.
     * @param {boolean} visible
     */
    setOceanVisible(visible) {
        this.oceanVisible = visible;
        this.oceanMesh.visible = visible;
    }

    /**
     * Update renderer and camera to match container size.
     */
    _updateSize() {
        const w = this.container.clientWidth;
        const h = this.container.clientHeight;
        this.renderer.setSize(w, h);
        if (this.camera) {
            this.camera.aspect = w / h;
            this.camera.updateProjectionMatrix();
        }
    }

    /**
     * Main animation loop.
     */
    _animate() {
        if (!this._animating) return;
        requestAnimationFrame(() => this._animate());
        this.controls.update();
        this.renderer.render(this.scene, this.camera);
    }

    /**
     * Clean up resources.
     */
    dispose() {
        this._animating = false;
        window.removeEventListener('resize', this._onResize);
        this.clear();
        this.controls.dispose();
        this.renderer.dispose();
        if (this.renderer.domElement.parentNode) {
            this.renderer.domElement.parentNode.removeChild(this.renderer.domElement);
        }
    }
}
