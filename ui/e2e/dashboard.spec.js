import { expect, test } from "@playwright/test";

const PANEL_HEADINGS = [
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
];

test("dashboard shell lists every panel heading", async ({ page }) => {
    await page.goto("/");

    for (const name of PANEL_HEADINGS) {
        await expect(page.getByRole("heading", { level: 2, name })).toBeVisible();
    }

    await expect(page.getByText("No data")).toHaveCount(PANEL_HEADINGS.length);
});
