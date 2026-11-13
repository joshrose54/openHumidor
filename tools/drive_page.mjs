// Drives one dashboard page in headless Chrome (DevTools protocol; Node 22+, nothing to install):
// opens it, runs a bit of JavaScript in it (a click, a filter), waits, and takes a screenshot of the
// whole page. Used by tools/dashboard_shots.py for the pictures a plain URL can't give (a filtered
// Events list, an edited Settings form); review build only.
//   node tools/drive_page.mjs <port> <page> <js> <out.png> [width] [height]
import { spawn } from "node:child_process";
import { writeFileSync, mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const [port, pageName, js, out] = process.argv.slice(2, 6);
const width = Number(process.argv[6] || 1440), height = Number(process.argv[7] || 1500);
const CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
const prof = mkdtempSync(join(tmpdir(), "oh-page-"));
const chrome = spawn(CHROME, ["--headless=new", "--disable-gpu", "--hide-scrollbars", `--user-data-dir=${prof}`,
  "--remote-debugging-port=9334", `--window-size=${width},${height}`, "about:blank"], { stdio: "ignore" });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let targets;
for (let i = 0; i < 50; i++) {
  try { targets = await (await fetch("http://127.0.0.1:9334/json")).json(); if (targets.length) break; } catch (_) {}
  await sleep(200);
}
const page = targets.find((t) => t.type === "page");
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise((r) => (ws.onopen = r));
let id = 0;
const waiting = new Map();
ws.onmessage = (m) => { const d = JSON.parse(m.data); if (d.id && waiting.has(d.id)) { waiting.get(d.id)(d.result); waiting.delete(d.id); } };
const send = (method, params = {}) => new Promise((r) => { const i = ++id; waiting.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
await send("Page.enable");
await send("Runtime.enable");
await send("Page.navigate", { url: `http://127.0.0.1:${port}/#${pageName}` });
await sleep(2500);
const r = await send("Runtime.evaluate", { expression: js, returnByValue: true, awaitPromise: true });
if (r.exceptionDetails) { console.error("drive_page: the script failed:", r.exceptionDetails.text); process.exitCode = 1; }
await sleep(1500);
const s = await send("Page.captureScreenshot", { format: "png", clip: { x: 0, y: 0, width, height, scale: 1 } });
writeFileSync(out, Buffer.from(s.data, "base64"));
ws.close();
chrome.kill();
