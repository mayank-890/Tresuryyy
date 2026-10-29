/** Placeholder for GET /api/status polling (TI §4.3). Wired in a later phase. */

const POLL_INTERVAL_MS = 2000;

/**
 * @param {{ dispatch: (action: import("../state/reducer.js").DashboardAction) => void }} _store
 * @returns {() => void} stop handle
 */
export function startStatusPolling(_store) {
    void POLL_INTERVAL_MS;
    return () => {};
}
