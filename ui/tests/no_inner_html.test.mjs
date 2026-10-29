import assert from "node:assert/strict";
import { readdirSync, readFileSync, statSync } from "node:fs";
import { join } from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const uiRoot = join(fileURLToPath(new URL("..", import.meta.url)));

/** @param {string} dir */
function walkJsFiles(dir) {
    /** @type {string[]} */
    const files = [];
    for (const entry of readdirSync(dir)) {
        const fullPath = join(dir, entry);
        const stat = statSync(fullPath);
        if (stat.isDirectory()) {
            if (entry === "node_modules" || entry === "e2e") {
                continue;
            }
            files.push(...walkJsFiles(fullPath));
            continue;
        }
        if (entry.endsWith(".js") || entry.endsWith(".mjs")) {
            files.push(fullPath);
        }
    }
    return files;
}

test("ui/js does not assign innerHTML (TI §4.3)", () => {
    const jsDir = join(uiRoot, "js");
    const offenders = [];
    for (const file of walkJsFiles(jsDir)) {
        const text = readFileSync(file, "utf8");
        if (text.includes(".innerHTML")) {
            offenders.push(file);
        }
    }
    assert.deepEqual(offenders, []);
});
