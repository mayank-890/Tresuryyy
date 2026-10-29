/**
 * Renders dashboard panels from store state using textContent only (TI §4.3).
 *
 * @param {HTMLElement} root
 * @param {import("../state/reducer.js").DashboardState} state
 */
export function renderDashboard(root, state) {
    root.replaceChildren();

    for (const panel of state.panels) {
        const section = document.createElement("section");
        section.className = "dashboard-panel";
        section.dataset.panelId = panel.id;

        const heading = document.createElement("h2");
        heading.textContent = panel.title;

        const body = document.createElement("p");
        body.className = "panel-placeholder";
        body.textContent = panel.body;

        section.append(heading, body);
        root.append(section);
    }
}
