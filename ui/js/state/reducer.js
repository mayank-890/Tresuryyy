import { PANEL_DEFINITIONS, PLACEHOLDER_NO_DATA } from "./panels.js";

/** @typedef {{ id: string, title: string, body: string }} PanelViewState */

/**
 * @typedef {object} DashboardState
 * @property {boolean} connected
 * @property {PanelViewState[]} panels
 */

/**
 * @typedef {{ type: "INIT" }} InitAction
 * @typedef {{ type: "STATUS_SNAPSHOT", payload: { panels: PanelViewState[] } }} StatusSnapshotAction
 * @typedef {InitAction | StatusSnapshotAction} DashboardAction
 */

/** @returns {DashboardState} */
export function createInitialState() {
    return {
        connected: false,
        panels: PANEL_DEFINITIONS.map(({ id, title }) => ({
            id,
            title,
            body: PLACEHOLDER_NO_DATA,
        })),
    };
}

/**
 * Pure dashboard reducer (TI §4.3).
 *
 * @param {DashboardState} state
 * @param {DashboardAction} action
 * @returns {DashboardState}
 */
export function dashboardReducer(state, action) {
    switch (action.type) {
        case "INIT":
            return state;
        case "STATUS_SNAPSHOT":
            return {
                ...state,
                connected: true,
                panels: action.payload.panels,
            };
        default:
            return state;
    }
}
