// Drives the dashboard's Update page in headless Chrome (DevTools protocol; Node 22+, nothing to
// install): picks an image in the file box and takes a screenshot of what the page says about it
// (<outdir>/<name>). With <seconds> > 0 it then presses Send and records the page over that time.
// Used by tools/dashboard_shots.py; review build only.
//   node tools/drive_update_page.mjs <port> <image.bin> <outdir> <seconds> [name]
import { spawn } from "node:child_process";
import { writeFileSync, mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const [port, image, out, secs, name] = [process.argv[2], process.argv[3], process.argv[4], Number(process.argv[5] || 0),
  process.argv[6] || "drive-0-chosen.png"];
const CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
const prof = mkdtempSync(join(tmpdir(), "oh-drive-"));
const chrome = spawn(CHROME, ["--headless=new", "--disable-gpu", `--user-data-dir=${prof}`, "--remote-debugging-port=9333",
  "--window-size=1440,1100", "about:blank"], { stdio: "ignore" });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let targets;
for (let i = 0; i < 50; i++) {
  try { targets = await (await fetch("http://127.0.0.1:9333/json")).json(); if (targets.length) break; } catch (_) {}
  await sleep(200);
}
const page = targets.find((t) => t.type === "page");
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise((r) => (ws.onopen = r));
let id = 0;
const waiting = new Map();
ws.onmessage = (m) => { const d = JSON.parse(m.data); if (d.id && waiting.has(d.id)) { waiting.get(d.id)(d.result); waiting.delete(d.id); } };
const send = (method, params = {}) => new Promise((r) => { const i = ++id; waiting.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
const ev = async (expr) => (await send("Runtime.evaluate", { expression: expr, returnByValue: true, awaitPromise: true })).result.value;
const shot = async (name) => { const r = await send("Page.captureScreenshot", { format: "png", clip: { x: 0, y: 0, width: 1440, height: 760, scale: 1 } }); writeFileSync(join(out, name), Buffer.from(r.data, "base64")); };

await send("Page.enable");
await send("Runtime.enable");
await send("Page.navigate", { url: `http://127.0.0.1:${port}/#update` });
await sleep(2500);
const doc = await send("DOM.getDocument");
const input = await send("DOM.querySelector", { nodeId: doc.root.nodeId, selector: "#u-file" });
await send("DOM.setFileInputFiles", { nodeId: input.nodeId, files: [image] });
await sleep(1500);
console.log("t=0 check:", await ev("document.querySelector('#u-check').textContent"));
console.log("     send enabled:", await ev("!document.querySelector('#u-send').disabled"));
await shot(name);
if (secs > 0) await ev("document.querySelector('#u-send').click()");
const t0 = Date.now();
let last = "", n = 1;
while (Date.now() - t0 < secs * 1000) {
  const s = await ev("[document.querySelector('#u-window').innerText.replace(/\\s+/g,' '), document.querySelector('#u-check').textContent, [...document.querySelectorAll('#u-facts div')].map(d=>d.innerText.replace(/\\s+/g,' ')).join(' | ')].join(' || ')");
  const key = s.replace(/\d+:\d\d/g, "m:ss").replace(/\d+ ?%/g, "n%");
  if (key !== last) {
    console.log(`t=${((Date.now() - t0) / 1000).toFixed(0)}s  ${s}`);
    await shot(`drive-${n++}.png`);
    last = key;
  }
  await sleep(1000);
}
ws.close();
chrome.kill();
