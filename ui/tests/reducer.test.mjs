import assert from "node:assert/strict";
import test from "node:test";

import { PLACEHOLDER_NO_DATA } from "../js/state/panels.js";
import { createInitialState, dashboardReducer } from "../js/state/reducer.js";

test("createInitialState exposes all USER_OPERATIONS §5.1 panels with no-data placeholders", () => {
    const state = createInitialState();
    assert.equal(state.connected, false);
    assert.equal(state.panels.length, 10);
    assert.deepEqual(
        state.panels.map((panel) => panel.title),
        [
            "System",
            "Kill and controls",
            "PnL",
            "Positions",
            "Orders",
            "Feeds",
            "Margin",
            "Risk events",
            "Reconciliation",
            "Alerts",
        ],
    );
    for (const panel of state.panels) {
        assert.equal(panel.body, PLACEHOLDER_NO_DATA);
    }
});

test("dashboardReducer is pure and handles STATUS_SNAPSHOT", () => {
    const before = createInitialState();
    const snapshot = {
        panels: before.panels.map((panel) =>
            panel.id === "system" ? { ...panel, body: "RUNNING" } : panel,
        ),
    };
    const after = dashboardReducer(before, { type: "STATUS_SNAPSHOT", payload: snapshot });

    assert.equal(before.connected, false);
    assert.equal(before.panels[0].body, PLACEHOLDER_NO_DATA);
    assert.equal(after.connected, true);
    assert.equal(after.panels[0].body, "RUNNING");
});

test("dashboardReducer ignores unknown actions", () => {
    const state = createInitialState();
    const next = dashboardReducer(state, { type: "UNKNOWN" });
    assert.equal(next, state);
});
