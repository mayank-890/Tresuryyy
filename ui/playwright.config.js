import { defineConfig } from "@playwright/test";

export default defineConfig({
    testDir: "./e2e",
    fullyParallel: false,
    workers: 1,
    timeout: 30_000,
    forbidOnly: Boolean(process.env.CI),
    retries: process.env.CI ? 1 : 0,
    use: {
        baseURL: "http://127.0.0.1:4173",
        ...(process.env.CI ? {} : { channel: "chrome" }),
    },
    webServer: {
        command: "python3 -m http.server 4173 --bind 127.0.0.1",
        cwd: import.meta.dirname,
        url: "http://127.0.0.1:4173",
        reuseExistingServer: false,
    },
});
