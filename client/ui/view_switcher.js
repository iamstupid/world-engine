/**
 * View switcher UI.
 * Handles toggling between Globe (3D) and Map (2D) views.
 */

export class ViewSwitcher {
    /**
     * @param {HTMLButtonElement} globeBtn - the globe view button
     * @param {HTMLButtonElement} mapBtn   - the map view button
     */
    constructor(globeBtn, mapBtn) {
        this.globeBtn = globeBtn;
        this.mapBtn = mapBtn;
        this._currentView = 'globe';
        this._onSwitch = null;

        this.globeBtn.addEventListener('click', () => this._activate('globe'));
        this.mapBtn.addEventListener('click', () => this._activate('map'));
    }

    /**
     * Activate a view by name.
     * @param {'globe'|'map'} viewName
     */
    _activate(viewName) {
        if (this._currentView === viewName) return;

        this._currentView = viewName;

        // Update button states
        this.globeBtn.classList.toggle('active', viewName === 'globe');
        this.mapBtn.classList.toggle('active', viewName === 'map');

        // Fire callback
        if (this._onSwitch) {
            this._onSwitch(viewName);
        }
    }

    /**
     * Register a callback for view switches.
     * @param {function('globe'|'map'): void} callback
     */
    onSwitch(callback) {
        this._onSwitch = callback;
    }

    /**
     * Get the currently active view name.
     * @returns {'globe'|'map'}
     */
    getCurrentView() {
        return this._currentView;
    }
}
