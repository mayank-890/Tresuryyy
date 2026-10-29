import { createInitialState, dashboardReducer } from "./state/reducer.js";
import { createStore } from "./state/store.js";
import { startStatusPolling } from "./transport/poll.js";
import { renderDashboard } from "./views/dashboard.js";

const root = document.getElementById("dashboard-root");
if (!(root instanceof HTMLElement)) {
    throw new Error("dashboard root element missing");
}

const store = createStore(dashboardReducer, createInitialState());

store.subscribe((state) => {
    renderDashboard(root, state);
});

store.dispatch({ type: "INIT" });
startStatusPolling(store);
