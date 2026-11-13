/* openHumidor dashboard (spec 5.5, docs/api.md API version 1). Plain JavaScript, no framework.
 *
 * docs/api.md API version 4.
 * Fetch rules (5.5): only what the open page shows, and only while the tab is visible; nothing
 * while it is hidden or the phone screen is off; on return, everything due is fetched at once.
 *   every page   the status every 30 s (the header: mode icons, time, alert)
 *   Overview     the status; the settings once (the bands' edges are tuning values)
 *   Sensors      the status
 *   Graphs       on opening and on a range change; the hour view adds its newest points every
 *                30 s; day, week and month refresh every 5 minutes
 *   Events       on opening, then new ones every 30 s; 50 at a time, older on request
 *   Settings     on opening; never refreshed while open (it would overwrite an edit)
 *   Logs         on opening
 *   Update       on opening; the window counts down in the browser; after an update the board is
 *                asked every few seconds until it answers with its new version
 * Times are UK local time from the board, never the browser's own zone. */
"use strict";

const API_VERSION = 4;
const STATUS_EVERY = 30000;
const LONG_EVERY = 300000;

// ---------------------------------------------------------------- small helpers
const $ = (s, el = document) => el.querySelector(s);
const $$ = (s, el = document) => [...el.querySelectorAll(s)];
const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
const icon = (name, cls = "") => `<svg class="i ${cls}" aria-hidden="true"><use href="#i-${name}"/></svg>`;
const fmt = (v, d = 1) => (v === null || v === undefined || Number.isNaN(v) ? "--" : Number(v).toFixed(d));
const num = (v) => Number(v).toLocaleString("en-GB");
const mss = (s) => `${Math.floor(s / 60)}:${String(Math.floor(s % 60)).padStart(2, "0")}`;
function ago(s) {
  if (s < 60) return `${Math.round(s)} s`;
  if (s < 3600) return `${Math.round(s / 60)} min`;
  if (s < 86400) { const h = Math.floor(s / 3600), m = Math.round((s % 3600) / 60); return m ? `${h} h ${m} min` : `${h} h`; }
  return `${Math.floor(s / 86400)} d ${Math.round((s % 86400) / 3600)} h`;
}
function bytes(n) {
  if (n >= 1e9) return `${(n / 1e9).toFixed(1)} GB`;
  if (n >= 1e6) return `${(n / 1e6).toFixed(1)} MB`;
  if (n >= 1e3) return `${Math.round(n / 1e3)} KB`;
  return `${n} B`;
}

const MODES = {
  cooling: { icon: "ac_unit", c: "cool", label: "Cooling" },
  heating: { icon: "local_fire_department", c: "heat", label: "Heating" },
  humidifying: { icon: "water_drop", c: "humid", label: "Humidifying" },
  drying: { icon: "humidity_low", c: "dry", label: "Drying" },
  standby: { icon: "mode_standby", c: "", label: "Standby" },
  starting: { icon: "hourglass_top", c: "acc", label: "Starting" },
  updating: { icon: "system_update_alt", c: "acc", label: "Updating" },
  fault: { icon: "warning", c: "fault", label: "Fault" },
};
const COLOURS = { cool: "#62B8EB", heat: "#F28C4E", humid: "#46C2AE", dry: "#E0B850", acc: "#D9A55B", fault: "#EF5A55", text: "#F3EDE7", muted: "#A69C93", line: "#3A332E" };

// ---------------------------------------------------------------- the API
class ApiError extends Error {
  constructor(status, body) { super(body?.message || `HTTP ${status}`); this.status = status; this.body = body; }
}
async function api(path, opts = {}) {
  const r = await fetch(path, { cache: "no-store", ...opts });
  let body = null;
  const t = r.headers.get("content-type") || "";
  if (t.includes("json")) body = await r.json();
  if (!r.ok) throw new ApiError(r.status, body);
  if (body && body.api !== undefined && body.api !== API_VERSION) {
    throw new ApiError(0, { message: `The board speaks API version ${body.api}; this page knows version ${API_VERSION}. Reload the page.` });
  }
  return body;
}
const post = (path, obj) => api(path, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(obj) });

// ---------------------------------------------------------------- jobs: fetched only while visible
const jobs = new Map();   // name -> { fn, every, timer, last }
function job(name, fn, every) {
  stopJob(name);
  const j = { fn, every, timer: null, last: 0 };
  jobs.set(name, j);
  runJob(j);
}
function stopJob(name) { const j = jobs.get(name); if (j) { clearTimeout(j.timer); jobs.delete(name); } }
async function runJob(j) {
  clearTimeout(j.timer);
  if (document.hidden) return;
  j.last = Date.now();
  try { await j.fn(); } catch (e) { boardTrouble(e); }
  if (j.every && [...jobs.values()].includes(j)) j.timer = setTimeout(() => runJob(j), j.every);
}
document.addEventListener("visibilitychange", () => {
  if (document.hidden) { for (const j of jobs.values()) clearTimeout(j.timer); return; }
  for (const j of jobs.values()) {   // catch up: anything due runs now
    if (!j.every || Date.now() - j.last >= j.every - 500) runJob(j);
    else j.timer = setTimeout(() => runJob(j), j.every - (Date.now() - j.last));
  }
});

// ---------------------------------------------------------------- the board's answers
let status = null, statusAt = 0, settings = null;
const note = $("#board-note");
function boardTrouble(e) {
  if (e instanceof ApiError && e.status === 0) { note.textContent = e.message; note.hidden = false; return; }
  if (e instanceof ApiError && e.status === 503 && e.body?.error === "busy") {
    note.textContent = "The board is busy with a download or an update; trying again shortly.";
  } else {
    note.textContent = statusAt ? `The board isn't answering. Showing what it said at ${status?.time?.uk ?? "--:--"}; trying again.` : "The board isn't answering yet; trying again.";
  }
  note.hidden = false;
  $("#hdr-fresh").classList.add("stale");
}

async function loadStatus() {
  status = await api("/api/status");
  statusAt = Date.now();
  note.hidden = true;
  $("#hdr-fresh").classList.remove("stale");
  renderHeader();
  const p = page();
  if (p === "overview") renderOverview();
  if (p === "sensors") renderSensors();
  if (p === "update") renderUpdateWindow();
}
async function loadSettings() { settings = await api("/api/settings"); }

setInterval(() => {   // freshness is local arithmetic, never a fetch
  if (!statusAt) return;
  const s = (Date.now() - statusAt) / 1000;
  const f = $("#hdr-fresh");
  f.textContent = s < 5 ? "updated just now" : `updated ${ago(s)} ago`;
  f.classList.toggle("stale", s > 75);
}, 1000);

// ---------------------------------------------------------------- header
function renderHeader() {
  const s = status;
  $("#hdr-time").textContent = s.time.uk;
  $("#hdr-modes").innerHTML = s.modes.map((m) => {
    const d = MODES[m] || MODES.standby;
    const left = m === "starting" ? ` ${mss(s.boot_hold_s)}` : m === "updating" ? ` ${s.update.pct} %` : "";
    return `<span class="mode c-${d.c}">${icon(d.icon)}<span class="mode-word">${d.label}${left}</span></span>`;
  }).join("");
  const tag = s.build.profile === "bench" ? (s.build.test ? "BENCH TEST" : "BENCH") : s.build.test ? "TEST" : "";
  $("#hdr-tag").textContent = tag;
  $("#hdr-tag").hidden = !tag;
}

// ---------------------------------------------------------------- Overview: the bands and why
const tune = (name) => settings?.tuning.find((t) => t.name === name)?.value;
const guardOf = (load) => status.guards.find((g) => g.load === load);
const heldText = (g) => `held: ${g.reason}${g.left_s !== null ? ` ${mss(g.left_s)}` : ""}`;

function band(el, { lo, hi, zones, ticks, value, set, label, unit }) {
  const span = hi - lo;
  const pos = (v) => Math.min(100, Math.max(0, ((v - lo) / span) * 100));
  const narrow = matchMedia("(max-width: 760px)").matches;
  const here = value === null ? -1 : zones.findIndex((z) => value >= z.from && value <= z.to);
  el.classList.toggle("unknown", value === null);
  el.setAttribute("aria-label", `${label}: ${value === null ? "no reading" : value + unit}, ${zones.map((z) => z.label).join(", ")}`);
  if (narrow) ticks = ticks.filter((t) => t.set);
  // the zone holding the marker puts its words at the end away from it, so the marker never cuts them
  const labPos = (z, i) => {
    if (i !== here || value === null) return "";
    const mid = (Math.max(z.from, lo) + Math.min(z.to, hi)) / 2;
    return value < mid ? " lab-end" : " lab-start";
  };
  const prev = el.dataset.prev;
  el.innerHTML =
    // each zone placed from its values alone, so its edges sit exactly on their ticks
    `<div class="track">${zones.map((z, i) => { const a = pos(Math.max(z.from, lo)), b = pos(Math.min(z.to, hi)); return `<div class="z ${z.cls}${i === here ? " here" : ""}${labPos(z, i)}" style="left:${a}%;width:${b - a}%">${esc(narrow ? z.short : z.label)}</div>`; }).join("")}</div>` +
    `<div class="setline" style="left:${pos(set)}%"></div>` +
    ticks.map((t) => `<span class="tick${t.set ? " set" : ""}" style="left:${pos(t.v)}%">${esc(t.text)}</span>`).join("") +
    (value === null ? "" : `<div class="mark" style="left:${prev ?? pos(value)}%"><span>${value < lo ? "◂ " : ""}${fmt(value, unit === "%" ? 0 : 1)}${value > hi ? " ▸" : ""}</span></div>`);
  if (value !== null) {   // one authored moment: the marker moves along its band to the new reading
    const m = $(".mark", el), to = pos(value);
    el.dataset.prev = String(to);
    if (prev !== undefined && Number(prev) !== to) requestAnimationFrame(() => requestAnimationFrame(() => { m.style.left = `${to}%`; }));
  }
}

function tempWhy(s) {
  const c = s.cabinet, set = s.setpoints.t, L = s.loads;
  const hyst = tune("cooling_hysteresis_c") ?? 0.5, db = tune("heating_deadband_c") ?? 1.5, stop = tune("heating_stop_below_c") ?? 1.0;
  const dosed = tune("dosed_control") === true;
  if (s.fault) return `<b class="c-fault">Fault</b> — ${esc(s.fault.replace(/^Fault:\s*/, ""))}. The loads are held until it clears.`;
  if (s.modes.includes("updating")) return `<b class="c-acc">Updating</b> — every load is off until the update has finished.`;
  if (s.boot_hold_s > 0) return `<b class="c-acc">Starting</b> — the loads wait ${mss(s.boot_hold_s)} after a boot.`;
  if (c.t === null) return `<span class="q">No reading: too few healthy cabinet sensors, or none read recently.</span>`;
  if (L.compressor.on && L.compressor.stripping) return `<b class="c-dry">Drying</b> — the compressor runs to take water out (an RH strip).`;
  if (L.compressor.on) return `<b class="c-cool">Cooling</b> — started above ${fmt(set + hyst)} °C (set ${fmt(set)} + ${fmt(hyst)})${dosed ? "" : `; runs until ${fmt(set)} °C`}.`;
  if (L.heater.on) return `<b class="c-heat">Heating</b> — started below ${fmt(set - db)} °C (set ${fmt(set)} − ${fmt(db)})${dosed ? "" : `; runs until ${fmt(set - stop)} °C`}.`;
  if (c.zone === "cool") { const g = guardOf("compressor"); return g ? `<b class="c-cool">Cooling wanted</b> — ${esc(heldText(g))}.` : `<b class="c-cool">Above ${fmt(set + hyst)} °C</b> — cooling starts on the next pass.`; }
  if (c.zone === "heat") {
    if (settings && settings.user.heater_enable.value === false) return `<b class="c-heat">Below ${fmt(set - db)} °C</b> — the heater is switched off in Settings.`;
    const g = guardOf("heater"); return g ? `<b class="c-heat">Heating wanted</b> — ${esc(heldText(g))}.` : `<b class="c-heat">Below ${fmt(set - db)} °C</b> — heating starts on the next pass.`;
  }
  return `<span class="q">In the dead zone, ${fmt(set - db)}–${fmt(set + hyst)} °C: nothing needs to run.</span>`;
}

function rhWhy(s) {
  const c = s.cabinet, set = s.setpoints.rh, L = s.loads;
  const bandPts = tune("humidify_band_pts") ?? 2, off = tune("rh_strip_offset_pct") ?? 5;
  const persist = tune("rh_strip_persist_min") ?? 30, hp = tune("humidify_persist_min") ?? 10;
  if (s.fault || s.modes.includes("updating") || s.boot_hold_s > 0) return `<span class="q">Waiting with the other loads.</span>`;
  if (c.rh === null) return `<span class="q">No reading.</span>`;
  if (L.humidifier.on) return `<b class="c-humid">Humidifying</b> — started below ${fmt(set - bandPts, 0)} % (set ${fmt(set, 0)} − ${fmt(bandPts, 0)}); runs until ${fmt(set, 0)} %.`;
  if (L.compressor.on && L.compressor.stripping) return `<b class="c-dry">Drying</b> — above ${fmt(set + off, 0)} % (set ${fmt(set, 0)} + ${fmt(off, 0)}); the strip ends when the water is back to target.`;
  if (c.rh < set - bandPts) {
    if (settings && settings.user.humidifier_enable.value === false) return `<b class="c-humid">Below ${fmt(set - bandPts, 0)} %</b> — the humidifier is switched off in Settings.`;
    const g = guardOf("humidifier"); return g ? `<b class="c-humid">Humidifying wanted</b> — ${esc(heldText(g))}.` : `<b class="c-humid">Below ${fmt(set - bandPts, 0)} %</b> — humidifying starts after ${fmt(hp, 0)} min below.`;
  }
  if (c.rh > set + off) return `<b class="c-dry">Above ${fmt(set + off, 0)} %</b> — an RH strip starts if it stays above for ${fmt(persist, 0)} min.`;
  return `<span class="q">Holding, between ${fmt(set - bandPts, 0)} and ${fmt(set + off, 0)} %.</span>`;
}

function renderOverview() {
  const s = status;
  if (!s) return;
  const c = s.cabinet;
  const T = $("#ov-temp"), H = $("#ov-rh");
  $("[data-k=t]", T).textContent = fmt(c.t);
  $("[data-k=t]", T).classList.toggle("dash", c.t === null);
  $("[data-k=rh]", H).textContent = fmt(c.rh, 0);
  $("[data-k=rh]", H).classList.toggle("dash", c.rh === null);
  $("[data-k=why]", T).innerHTML = tempWhy(s);
  $("[data-k=why]", H).innerHTML = rhWhy(s);
  $("[data-k=spread]", T).textContent = c.t === null ? `${c.healthy} of 4 cabinet sensors healthy`
    : `${fmt(c.t_min)}–${fmt(c.t_max)} °C across the cabinet sensors · spread ${fmt(c.spread_t)} · ${c.healthy} of 4 healthy · read ${ago(c.age_s)} ago`;
  // the bands: edges from the setpoints and the tuning values (5.5: read only)
  const st = s.setpoints.t, hyst = tune("cooling_hysteresis_c") ?? 0.5, db = tune("heating_deadband_c") ?? 1.5;
  band($("[data-k=band]", T), {
    label: "Temperature band", unit: "°C", value: c.t, set: st,
    lo: st - db - 1.5, hi: st + hyst + 1.5,
    zones: [
      { from: -99, to: st - db, cls: "z-heat", label: `Heat below ${fmt(st - db)}`, short: `Heat < ${fmt(st - db)}` },
      { from: st - db, to: st + hyst, cls: "z-dead", label: "Dead zone · nothing starts", short: "Dead zone" },
      { from: st + hyst, to: 99, cls: "z-cool", label: `Cool above ${fmt(st + hyst)}`, short: `Cool > ${fmt(st + hyst)}` },
    ].map((z, i, a) => ({ ...z, from: i === 0 ? st - db - 1.5 : z.from, to: i === a.length - 1 ? st + hyst + 1.5 : z.to })),
    ticks: [{ v: st - db, text: fmt(st - db) }, { v: st, text: `${fmt(st)} set`, set: true }, { v: st + hyst, text: fmt(st + hyst) }],
  });
  const sr = s.setpoints.rh, b = tune("humidify_band_pts") ?? 2, off = tune("rh_strip_offset_pct") ?? 5;
  const rlo = sr - b - 4, rhi = sr + off + 3;
  band($("[data-k=band]", H), {
    label: "Humidity band", unit: "%", value: c.rh, set: sr, lo: rlo, hi: rhi,
    zones: [
      { from: rlo, to: sr - b, cls: "z-humid", label: `Humidify below ${fmt(sr - b, 0)}`, short: `< ${fmt(sr - b, 0)}` },
      { from: sr - b, to: sr + off, cls: "z-dead", label: "Holding", short: "Holding" },
      { from: sr + off, to: rhi, cls: "z-dry", label: `Strip above ${fmt(sr + off, 0)}`, short: `Strip > ${fmt(sr + off, 0)}` },
    ],
    ticks: [{ v: sr - b, text: fmt(sr - b, 0) }, { v: sr, text: `${fmt(sr, 0)} set`, set: true }, { v: sr + off, text: fmt(sr + off, 0) }],
  });
  // the lines: boot hold, update, the window
  const lines = [];
  if (s.boot_hold_s > 0) lines.push(`<div class="pill acc">${icon("hourglass_top")}<span>Starting: the loads wait ${mss(s.boot_hold_s)}</span></div>`);
  if (s.update.updating) lines.push(`<div class="pill acc">${icon("system_update_alt")}<span>Updating ${s.update.pct} %</span></div>`);
  if (s.update.window_open) lines.push(`<div class="pill acc">${icon("system_update_alt")}<span>Update allowed: ${mss(s.update.window_s)} left · <a href="#update">Update</a></span></div>`);
  $("#ov-lines").innerHTML = lines.join("");
  // alerts: every one, most serious first (the LCD's list)
  $("#ov-alerts").innerHTML = s.alerts.length
    ? s.alerts.map((a) => `<li class="${a.fault ? "fault" : ""}">${icon("warning")}<span>${esc(a.title)}</span><span class="when">${esc(a.since)}</span></li>`).join("")
    : `<li class="none">No active alerts.</li>`;
  // loads, as running, and what holds them
  const L = s.loads, rows = [];
  const row = (name, ic, cls, on, text, held) => rows.push(`<div class="${on ? "on" : ""}${held ? " held" : ""}"><dt class="${on ? cls : ""}">${icon(ic)}${name}</dt><dd>${text}</dd></div>`);
  const gc = guardOf("compressor"), gh = guardOf("heater"), gu = guardOf("humidifier");
  row("Compressor", L.compressor.stripping ? "humidity_low" : "ac_unit", L.compressor.stripping ? "c-dry" : "c-cool", L.compressor.on,
      L.compressor.on ? `on ${ago(L.compressor.for_s)}${L.compressor.stripping ? " · RH strip" : ""}` : gc ? esc(heldText(gc)) : L.compressor.stopped_ago_s !== null ? `off · stopped ${ago(L.compressor.stopped_ago_s)} ago` : "off", !L.compressor.on && gc);
  row("Heater", "local_fire_department", "c-heat", L.heater.on, L.heater.on ? `on · ${L.heater.duty_pct} %` : gh ? esc(heldText(gh)) : "off", !L.heater.on && gh);
  row("Humidifier", "water_drop", "c-humid", L.humidifier.on, L.humidifier.on ? "on" : gu ? esc(heldText(gu)) : "off", !L.humidifier.on && gu);
  row("Plenum fan", "air", "", L.fan.rpm > 0, `${L.fan.pwm} % · ${num(L.fan.rpm)} rpm`, L.fan.pwm >= 20 && L.fan.rpm === 0);
  row("Interlock", "settings", "", false, `${L.interlock} side`);
  if (L.blowers) {   // Rev Q (4.6b): the mode decides the duty; full speed in boost or a fault
    const B = L.blowers;
    const text = B.mode === "idle" ? (B.idle_is_full ? `idle (${B.pwm} % until BP2 test 22)` : `${B.pwm} % · idle`)
      : B.mode === "updating" ? `${B.pwm} % · idle while updating`
      : B.mode === "full" ? `full speed · ${B.why === "fault" ? "fault" : B.why === "no fresh reading" ? "no fresh reading" : "no reading yet"}`
      : `boost · spread ${fmt(B.spread_t, 2)} °C, ${fmt(B.spread_rh, 1)} pts${B.idle_in_s !== null ? ` · idle in ${mss(B.idle_in_s)}` : ""}`;
    row("Blowers", "air", "", true, text, B.mode === "full" && B.why === "fault");
  }
  if (L.ion) {       // Rev Q (4.6c): short runs from the clock; times are UK local from the board
    const I = L.ion;
    const skipped = I.last_skipped ? ` · last run skipped${I.last_skipped.at ? ` (${esc(I.last_skipped.at)})` : ""}: ${esc(I.last_skipped.why)}` : "";
    const text = I.state === "running" ? `on · ${mss(I.left_s)} left`
      : I.state === "waiting" ? (I.next ? `off · next run ${esc(I.next)}` : `off · next run in ${ago(I.next_in_s)} (from boot: the time isn't trusted)`) + skipped
      : I.state === "held" ? `held: ${esc(I.why)}${I.next ? ` · next ${esc(I.next)}` : ""}`
      : I.state === "off" ? "switched off on Settings"
      : I.state === "no_runs" ? "no runs set on Settings"
      : "off · starting";
    row("Ion generator", "air", "", I.on, text, I.state === "held");
  }
  $("#ov-loads").innerHTML = rows.join("");
  // the last 7 days
  const w = s.week, wk = (label, v) => `<div><dt>${label}</dt><dd>${v === null ? "counting…" : num(v)}</dd></div>`;
  $("#ov-week").innerHTML = wk("Crossovers", w.crossovers) + wk("RH strips", w.strips) + wk("Humidifier runs", w.humid_runs) +
    `<div><dt>Disturbances</dt><dd>${w.disturbances === null ? (w.ready ? "detection not switched on" : "counting…") : num(w.disturbances)}</dd></div>`;
  // the Board section (5.5)
  const brd = s.board, v12 = (v) => (v === null ? "--" : `${fmt(v, 2)}\u00a0V`);
  const rows2 = [
    ["Firmware", esc(brd.version)],
    ["Running for", ago(brd.uptime_s)],
    ["Last restart", esc(brd.restart_reason || "--")],
    ["12 V supply", brd.v12_now === null ? "--" : `${v12(brd.v12_now)} · lowest today ${v12(brd.v12_low_today)}`],
    ["WiFi signal", `${brd.wifi_rssi}\u00a0dBm · ${brd.wifi_rssi >= -60 ? "good" : brd.wifi_rssi >= -70 ? "fair" : "weak"}`],
    ["Free memory", `${Math.round(brd.heap_free_bytes / 1024)}\u00a0KB`],
    ["SD card", brd.card_free_bytes === null ? "not available" : `${bytes(brd.card_free_bytes)} free of ${bytes(brd.card_total_bytes)}`],
  ];
  $("#ov-board").innerHTML = rows2.map(([k, v]) => `<div><dt>${k}</dt><dd>${v}</dd></div>`).join("");
  $("#page-overview").classList.remove("loading");
}

// ---------------------------------------------------------------- Graphs (uPlot, UK time)
const UK = (ts) => uPlot.tzDate(new Date(ts * 1e3), "Europe/London");
const ukTime = new Intl.DateTimeFormat("en-GB", { timeZone: "Europe/London", hour: "2-digit", minute: "2-digit", hourCycle: "h23" });
const ukDay = new Intl.DateTimeFormat("en-GB", { timeZone: "Europe/London", day: "numeric", month: "short" });
function ukWhen(utcS) { const d = new Date(utcS * 1000); return `${ukDay.format(d)} ${ukTime.format(d)}`; }
// the time axis in UK time, 24-hour (5.5): times over a day or less, dates beyond
const timeAxis = () => axis({ values: (u, splits) => {
  const wide = u.scales.x.max - u.scales.x.min > 2 * 86400;
  return splits.map((ts) => (wide ? ukDay : ukTime).format(new Date(ts * 1000)));
} });
let gRange = "day", gData = null, charts = [], shown = new Set();
const SENSOR_COLOURS = ["#F3EDE7", "#C9B79C", "#8FA3B0", "#B48EAD", "#62B8EB", "#7F8C5A"];

function axis(extra = {}) {
  return { stroke: COLOURS.muted, grid: { stroke: "rgba(58,51,46,0.7)", width: 1 }, ticks: { stroke: COLOURS.line, width: 1 }, font: "12px -apple-system, sans-serif", ...extra };
}
function chartWidth() { return Math.max(280, $("#g-temp").clientWidth || 600); }

// The Overview's bands behind the graphs: heat / cool, humidify / strip, in the same tints.
function zones(list) {
  return (u) => {
    const { ctx, bbox } = u;
    ctx.save();
    for (const [from, to, colour] of list) {
      if (from === null || to === null) continue;
      const y0 = u.valToPos(Math.min(to, u.scales.y.max), "y", true), y1 = u.valToPos(Math.max(from, u.scales.y.min), "y", true);
      if (y1 <= y0) continue;
      ctx.fillStyle = colour;
      ctx.fillRect(bbox.left, y0, bbox.width, y1 - y0);
    }
    ctx.restore();
  };
}
function makeCharts() {
  charts.forEach((c) => c.destroy());
  charts = [];
  const d = gData;
  if (!d || d.t.length === 0) return;
  const sync = { key: "oh", setSeries: false };
  const setT = d.t.map(() => status?.setpoints.t ?? null), setRh = d.t.map(() => status?.setpoints.rh ?? null);
  const w = chartWidth(), phone = w < 500;
  // temperature: mean, coldest-to-warmest band, setpoint, each sensor on request
  const tSeries = [{}, { label: "warmest", stroke: "transparent", points: { show: false } }, { label: "coldest", stroke: "transparent", points: { show: false } },
    { label: "cabinet", stroke: COLOURS.text, width: 2, points: { show: false } },
    { label: "set", stroke: COLOURS.muted, width: 1, dash: [5, 5], points: { show: false } }];
  const tData = [d.t, d.cab_t_max, d.cab_t_min, d.cab_t, setT];
  for (let ch = 0; ch < 6; ch++) if (shown.has(ch)) { tSeries.push({ label: `ch${ch}`, stroke: SENSOR_COLOURS[ch], width: 1.25, points: { show: false } }); tData.push(d[`ch${ch}_t`]); }
  const st = status?.setpoints.t ?? null, sr = status?.setpoints.rh ?? null;
  const hyst = tune("cooling_hysteresis_c") ?? 0.5, db = tune("heating_deadband_c") ?? 1.5;
  const hb = tune("humidify_band_pts") ?? 2, off = tune("rh_strip_offset_pct") ?? 5;
  charts.push(new uPlot({
    width: w, height: phone ? 200 : 260, tzDate: UK, cursor: { sync, points: { show: false } }, legend: { show: false },
    hooks: { drawClear: [zones([[-99, st === null ? null : st - db, "rgba(242,140,78,0.10)"], [st === null ? null : st + hyst, 99, "rgba(98,184,235,0.10)"]])] },
    series: tSeries, bands: [{ series: [1, 2], fill: "rgba(98,184,235,0.20)" }],
    axes: [timeAxis(), axis({ size: 58, incrs: [0.25, 0.5, 1, 2, 5], values: (u, v) => v.map((x) => x.toFixed(1)) })],
  }, tData, $("#g-temp")));
  const rSeries = [{}, { label: "cabinet", stroke: COLOURS.humid, width: 2, points: { show: false } }, { label: "set", stroke: COLOURS.muted, width: 1, dash: [5, 5], points: { show: false } }];
  const rData = [d.t, d.cab_rh, setRh];
  for (let ch = 0; ch < 6; ch++) if (shown.has(ch)) { rSeries.push({ label: `ch${ch}`, stroke: SENSOR_COLOURS[ch], width: 1.25, points: { show: false } }); rData.push(d[`ch${ch}_rh`]); }
  charts.push(new uPlot({
    width: w, height: phone ? 170 : 220, tzDate: UK, cursor: { sync, points: { show: false } }, legend: { show: false },
    hooks: { drawClear: [zones([[-99, sr === null ? null : sr - hb, "rgba(70,194,174,0.11)"], [sr === null ? null : sr + off, 999, "rgba(224,184,80,0.10)"]])] },
    series: rSeries, axes: [timeAxis(), axis({ size: 58, incrs: [1, 2, 5, 10], values: (u, v) => v.map((x) => x.toFixed(0)) })],
  }, rData, $("#g-rh")));
  // what ran: three rows, each filled to its own base
  const rowOf = (arr, base) => arr.map((v) => (v === null ? null : base + v * 0.72));
  const lSeries = [{}];
  const lData = [d.t];
  [["compressor", COLOURS.cool, 2], ["heater", COLOURS.heat, 1], ["humidifier", COLOURS.humid, 0]].forEach(([k, col, base]) => {
    lSeries.push({ label: k, stroke: col, width: 1, fill: col + "B3", fillTo: () => base, paths: uPlot.paths.stepped({ align: 1 }), points: { show: false } });
    lData.push(rowOf(d[k], base));
  });
  charts.push(new uPlot({
    width: w, height: 112, tzDate: UK, cursor: { sync, points: { show: false } }, legend: { show: false },
    scales: { y: { range: [0, 2.9] } }, series: lSeries,
    axes: [timeAxis(), axis({ size: 58, splits: () => [0.36, 1.36, 2.36], values: () => ["Humid", "Heat", "Comp"], grid: { show: false } })],
  }, lData, $("#g-loads")));
}

function renderGraphSummary() {
  const d = gData, s = status;
  const pct = (v) => (v === null ? "--" : `${Math.round(v * 100)} %`);
  const items = [["Compressor on", pct(d.duty.compressor)], ["Heater on", pct(d.duty.heater)], ["Humidifier on", pct(d.duty.humidifier)]];
  if (s && (gRange === "week" || gRange === "month")) {
    items.push(["Crossovers, 7 days", s.week.crossovers ?? "…"], ["RH strips, 7 days", s.week.strips ?? "…"], ["Humidifier runs, 7 days", s.week.humid_runs ?? "…"]);
  }
  $("#g-sum").innerHTML = items.map(([k, v]) => `<div><dt>${k}</dt><dd>${v}</dd></div>`).join("");
  const empty = d.t.length === 0;
  $("#g-empty").hidden = !empty;
  $$("#page-graphs .chart, #g-sensors, #g-sum").forEach((el) => (el.hidden = empty));
  $("#g-empty").textContent = "Nothing logged in this range yet. After a boot the graphs refill from the card in the background.";
}

async function loadGraphs(full = true) {
  if (full || !gData || gRange !== "hour") {
    gData = await api(`/api/history?range=${gRange}`);
  } else {   // the hour view: only the newest points
    const last = gData.t.length ? gData.t[gData.t.length - 1] : 0;
    const add = await api(`/api/history?range=hour&since=${last}`);
    const keep = add.now - 3600;
    for (const k of Object.keys(gData)) if (Array.isArray(gData[k])) gData[k] = gData[k].concat(add[k]);
    while (gData.t.length && gData.t[0] <= keep) for (const k of Object.keys(gData)) if (Array.isArray(gData[k])) gData[k].shift();
    gData.duty = add.duty;
  }
  renderGraphSummary();
  makeCharts();
}

function graphsPage() {
  if (!settings) job("settings-once", async () => { await loadSettings(); if (gData) makeCharts(); }, 0);
  $$("#g-range button").forEach((b) => b.setAttribute("aria-checked", String(b.dataset.range === gRange)));
  job("graphs", () => loadGraphs(true), 0);
  if (gRange === "hour") job("graphs-more", () => loadGraphs(false), STATUS_EVERY);
  else job("graphs-more", () => loadGraphs(true), LONG_EVERY);
}
$("#g-range").addEventListener("click", (e) => {
  const b = e.target.closest("button[data-range]");
  if (!b || b.dataset.range === gRange) return;
  gRange = b.dataset.range;
  graphsPage();
});
$("#g-sensors").insertAdjacentHTML("beforeend", ["top_front", "bottom_front", "mid_front", "mid_rear", "chamber", "ambient"]
  .map((n, ch) => `<label style="color:${SENSOR_COLOURS[ch]}"><input type="checkbox" value="${ch}"><span style="color:var(--text)">ch${ch} ${n}</span></label>`).join(""));
$("#g-sensors").addEventListener("change", (e) => {
  const ch = Number(e.target.value);
  e.target.checked ? shown.add(ch) : shown.delete(ch);
  makeCharts();
});
let resizeT;
addEventListener("resize", () => { clearTimeout(resizeT); resizeT = setTimeout(() => { if (page() === "graphs") makeCharts(); if (page() === "overview") renderOverview(); }, 150); });

// ---------------------------------------------------------------- Sensors
function renderSensors() {
  const s = status;
  const healthyCab = s.sensors.filter((x) => x.role === "cabinet" && x.state === "healthy").length;
  $("#s-sub").textContent = `${healthyCab} of 4 cabinet sensors healthy · at least 3 are needed for control`;
  const stateText = (x) => x.state === "healthy" ? "Healthy" : x.state === "manual" ? "Excluded by hand" : x.state === "not_read" ? "Not read yet" : `Excluded: ${x.reason}`;
  $("#s-table tbody").innerHTML = s.sensors.map((x) => {
    const manual = x.state === "manual";
    return `<tr>
      <td class="ch">ch${x.ch}</td>
      <td>${esc(x.name)}<div class="pos">probe ${esc(x.probe)} · ${x.role}</div></td>
      <td><span class="state ${x.state}">${esc(stateText(x))}</span></td>
      <td class="n">${x.t === null ? "--" : fmt(x.t, 2) + " °C"}</td>
      <td class="n">${x.rh === null ? "--" : fmt(x.rh, 1) + " %"}</td>
      <td class="n dim">${x.rh_raw === null ? "--" : fmt(x.rh_raw, 1) + " %"}</td>
      <td class="n dim">${ago(x.age_s)} ago</td>
      <td class="n">${x.crc_hour} / ${x.timeouts_hour}</td>
      <td class="n"><button class="btn small" data-ch="${x.ch}" data-ex="${manual ? "false" : "true"}"${s.update.updating ? " disabled" : ""}>${manual ? "Bring back" : "Exclude"}</button></td>
    </tr>`;
  }).join("");
}
$("#s-table").addEventListener("click", async (e) => {
  const b = e.target.closest("button[data-ch]");
  if (!b) return;
  const ch = Number(b.dataset.ch), ex = b.dataset.ex === "true";
  const x = status.sensors[ch];
  if (ex) {
    const left = status.sensors.filter((y) => y.role === "cabinet" && y.state === "healthy" && y.ch !== ch).length;
    const fault = x.role === "cabinet" && left < 3 || x.role === "chamber";
    const q = `Exclude ch${ch} ${x.name} by hand? It stays excluded until you bring it back.` +
      (fault ? `\n\nThat leaves ${x.role === "chamber" ? "no chamber sensor" : `${left} of 4 cabinet sensors`}: the cabinet goes into Fault and the loads are held.` : "");
    if (!confirm(q)) return;
  }
  b.disabled = true;
  try {
    const r = await post("/api/sensors/exclude", { channel: ch, excluded: ex });
    $("#s-note").textContent = `ch${r.ch} ${r.name} (probe ${r.probe}): ${r.excluded ? "excluded by hand" : "brought back; it rejoins after its good reads"}.`;
    await loadStatus();
  } catch (err) {
    $("#s-note").textContent = err.body?.message || "The board didn't answer; nothing changed.";
    b.disabled = false;
  }
});

// ---------------------------------------------------------------- Events
let evRows = [], evOlder = null, evNewest = null;
const TYPE_COLOUR = { compressor: "cool", harvest: "cool", heater: "heat", humidifier: "humid", humidify: "humid", strip: "dry",
  sensor: "warn", i2c: "warn", crc: "warn", timeout: "warn", sd: "warn", "12v": "warn", wifi: "warn", heap: "warn", fan: "warn", ineffective: "warn",
  settings: "acc", lcd: "acc", door: "acc" };
function evRow(e, freshRow) {
  const time = e.uk.split(" ").pop();
  return `<li${freshRow ? ' class="fresh-row"' : ""} data-type="${esc(e.type)}"><time class="${e.trusted ? "" : "untrusted"}" title="${esc(e.utc)}${e.trusted ? "" : " (clock not trusted)"}">${esc(time)}</time><span class="type t-${TYPE_COLOUR[e.type] || ""}">${esc(e.type)}</span><span class="ev"><span class="sentence">${esc(e.sentence || e.detail)}</span><small class="logged">${esc(e.detail)}</small></span></li>`;
}
function evList(rows, newCount) {
  let day = "", out = "";
  rows.forEach((e, i) => {
    const d = e.uk.split(" ").slice(0, 3).join(" ");
    if (d !== day) { day = d; out += `<li class="day">${esc(d)}</li>`; }
    out += evRow(e, i < newCount);
  });
  return out;
}
function renderEvents(newCount = 0) {
  const t = $("#e-type").value;
  const types = [...new Set(evRows.map((e) => e.type))].sort();
  $("#e-type").innerHTML = `<option value="">all types</option>` + types.map((x) => `<option${x === t ? " selected" : ""}>${esc(x)}</option>`).join("");
  const rows = evRows.filter((e) => !t || e.type === t);
  $("#e-list").innerHTML = rows.length ? evList(rows, newCount) : `<li class="empty">No events${t ? ` of type ${esc(t)} in what is loaded` : ""}.</li>`;
  $("#e-older").hidden = !evOlder;
}
async function loadEvents() {
  const r = await api("/api/events");
  const ram = r.source === "ram";
  $("#e-ram").hidden = !ram;
  $("#e-ram").textContent = ram ? "SD card unavailable: these are the recent events kept in the board's memory only." : "";
  evRows = r.events; evOlder = r.older; evNewest = r.newest;
  renderEvents();
}
async function newEvents() {
  if (!evNewest) return loadEvents();
  const r = await api(`/api/events?after=${encodeURIComponent(evNewest)}`);
  if (r.source === "ram") return loadEvents();
  evNewest = r.newest || evNewest;
  if (r.events.length) { evRows = r.events.concat(evRows); renderEvents(r.events.length); }
}
$("#e-type").addEventListener("change", () => renderEvents());
$("#e-older").addEventListener("click", async () => {
  const b = $("#e-older");
  b.disabled = true;
  try {
    const r = await api(`/api/events?before=${encodeURIComponent(evOlder)}`);
    evRows = evRows.concat(r.events); evOlder = r.older;
    renderEvents();
  } catch (e) { boardTrouble(e); }
  b.disabled = false;
});
$("#e-door").addEventListener("submit", async (e) => {
  e.preventDefault();
  const m = $("#e-door-msg");
  const noteText = $("#e-note").value.trim();
  try {
    await post("/api/door-test", noteText ? { note: noteText } : {});
    m.className = "msg good"; m.textContent = "Door test logged.";
    $("#e-note").value = "";
    newEvents();
  } catch (err) { m.className = "msg bad"; m.textContent = err.body?.message || "The board didn't answer; nothing was logged."; }
});

// ---------------------------------------------------------------- Settings (never refreshed while open)
let draft = null;
// the user settings' values (each an object with its value; ion_minutes_per_day_max is a plain number)
const userDraft = () => Object.fromEntries(Object.entries(settings.user).filter(([, v]) => v && typeof v === "object").map(([k, v]) => [k, v.value]));
function renderSettings() {
  const u = settings.user;
  for (const el of $$(".stepper")) {
    const f = el.dataset.field, d = u[f];
    const dec = f === "target_temp_c" ? 1 : 0;
    const unit = d.unit ? ` ${d.unit}` : "";
    $("[data-k=hint]", el).textContent = f.startsWith("ion_") ? `${d.min}–${d.max}${unit}` : `${fmt(d.min, dec)}–${fmt(d.max, dec)}${unit} in steps of ${fmt(d.step, dec)}`;
    const out = $("[data-k=v]", el);
    out.textContent = `${fmt(draft[f], dec)}${unit}`;
    out.classList.toggle("changed", draft[f] !== d.value);
    $("[data-d='-1']", el).disabled = draft[f] <= d.min;
    $("[data-d='1']", el).disabled = draft[f] >= d.max;
  }
  for (const cb of $$(".switch input")) cb.checked = draft[cb.dataset.field];
  // 5.5 / 4.6c: runs x minutes at most 10 a day (the board refuses more: ion_too_much)
  const perDay = draft.ion_runs_per_day * draft.ion_minutes_per_run, maxDay = u.ion_minutes_per_day_max ?? 10;
  const tooMuch = perDay > maxDay;
  const total = $("#st-ion-total");
  total.className = `ion-total${tooMuch ? " bad" : ""}`;
  total.textContent = perDay === 0 ? "No runs: the ion generator won't run."
    : tooMuch ? `${draft.ion_runs_per_day} runs of ${draft.ion_minutes_per_run} min is ${perDay} min a day: at most ${maxDay} minutes a day in total.`
    : `${draft.ion_runs_per_day} ${draft.ion_runs_per_day === 1 ? "run" : "runs"} of ${draft.ion_minutes_per_run} min: ${perDay} min a day, ${draft.ion_runs_per_day === 1 ? "at 00:00 UTC" : `every ${24 / draft.ion_runs_per_day} hours from 00:00 UTC`}.`;

  const changed = Object.keys(draft).some((k) => draft[k] !== u[k].value);
  const updating = status?.update.updating;
  $("#st-save").disabled = !changed || updating || tooMuch;
  $("#st-cancel").disabled = !changed;
  if (updating) { $("#st-msg").className = "msg"; $("#st-msg").textContent = "An update is running: settings can be changed when it has finished."; }
  $("#st-tuning").innerHTML = settings.tuning.map((t) => `<div><dt>${esc(t.label)}</dt><dd>${typeof t.value === "boolean" ? (t.value ? "On" : "Off") : `${t.value}${t.unit ? " " + esc(t.unit) : ""}`}</dd></div>`).join("");
}
async function settingsPage() {
  await loadSettings();
  draft = userDraft();
  $("#st-msg").textContent = "";
  renderSettings();
}
$("#st-form").addEventListener("click", (e) => {
  const b = e.target.closest("button[data-d]");
  if (!b || !draft) return;
  const f = b.closest(".stepper").dataset.field, d = settings.user[f];
  draft[f] = Math.min(d.max, Math.max(d.min, Math.round((draft[f] + Number(b.dataset.d) * d.step) / d.step) * d.step));
  renderSettings();
});
// switches show their state at once; they slide only once the form is touched by hand
["pointerdown", "keydown"].forEach((t) => $("#st-form").addEventListener(t, () => $("#st-form").classList.add("ready"), { once: true }));
$("#st-form").addEventListener("change", (e) => { if (e.target.dataset.field && draft) { draft[e.target.dataset.field] = e.target.checked; renderSettings(); } });
$("#st-cancel").addEventListener("click", () => { draft = userDraft(); $("#st-msg").textContent = ""; renderSettings(); });
$("#st-form").addEventListener("submit", async (e) => {
  e.preventDefault();
  const changes = Object.fromEntries(Object.keys(draft).filter((k) => draft[k] !== settings.user[k].value).map((k) => [k, draft[k]]));
  const m = $("#st-msg");
  try {
    settings = await post("/api/settings", changes);
    draft = userDraft();
    m.className = "msg good"; m.textContent = "Saved. The board uses the new settings from its next pass.";
    renderSettings();
    loadStatus();
  } catch (err) { m.className = "msg bad"; m.textContent = err.body?.message || "The board didn't answer; nothing changed."; }
});

// ---------------------------------------------------------------- Logs
async function logsPage() {
  try {
    const r = await api("/api/logs");
    $("#l-table").closest(".table-wrap").hidden = false;
    $("#l-note").className = "note";
    $("#l-note").textContent = "Downloads run at the card's own pace: a month of readings takes a few minutes. One download at a time.";
    $("#l-card").textContent = `SD card: ${bytes(r.card.free_bytes)} free of ${bytes(r.card.total_bytes)}`;
    $("#l-table tbody").innerHTML = r.files.length ? r.files.map((f) => `<tr><td>${esc(f.name)}${f.schema > 1 ? "" : ` <span class="dim">(schema 1)</span>`}</td><td>${f.kind}</td><td>${f.month ?? "every hour"}</td><td class="n">${bytes(f.bytes)}</td><td class="n"><a class="btn small" href="/api/logs/file?path=${encodeURIComponent(f.path)}" download="${esc(f.name)}">${icon("download")}Download</a></td></tr>`).join("")
      : `<tr><td colspan="5" class="dim">No log files on the card yet.</td></tr>`;
  } catch (e) {
    if (e.status === 503 && e.body?.error === "card_unavailable") {
      $("#l-card").textContent = "";
      $("#l-table").closest(".table-wrap").hidden = true;
      $("#l-note").className = "board-note inline";
      $("#l-note").textContent = "The SD card isn't available just now, so there is nothing to download. The board keeps logging into its memory and writes it all to the card when the card is back; the files appear here then.";
      return;
    } else throw e;
  }
}

// ---------------------------------------------------------------- Update
let upd = null, updAt = 0, updFile = null, updOk = false;
const UPDATE_EVERY = 5000;   // the Update page follows an update, a check or a rollback as it happens
let updAwaitGood = null;     // the version just installed, until it is marked good
// judgeImage(): web/image_check.js, the board's own rules (proven equal to the C++ by test_image_check)
function windowLeft() { return upd && upd.window_open ? Math.max(0, upd.window_s - (Date.now() - updAt) / 1000) : 0; }
function verifyLeft() { return upd && upd.pending_verify ? Math.max(0, (upd.verify_s || 0) - (Date.now() - updAt) / 1000) : 0; }
function renderUpdateWindow() {
  // whichever reading is newer: the status (every 30 s) or the Update page's own (every 5 s)
  if (status && (!upd || statusAt > updAt)) { upd = { ...(upd || {}), window_open: status.update.window_open, window_s: status.update.window_s, updating: status.update.updating, pct: status.update.pct, compressor_running: status.loads.compressor.on }; updAt = statusAt; }
  if (!upd) return;
  const left = windowLeft(), open = left > 0, checking = !!upd.pending_verify;
  const w = $("#u-window");
  w.classList.toggle("open", open || upd.updating);
  if (upd.updating) {
    w.innerHTML = upd.phase === "restarting"
      ? `${icon("system_update_alt")}<div><b>Installed: restarting</b><span>Written and checked. Every load is off; the board restarts in a moment, then the boot hold applies.</span></div>`
      : `${icon("system_update_alt")}<div><b>Updating</b><span>Writing the new firmware a piece at a time. Every load is off until it has finished.</span></div><span class="count">${upd.pct} %</span>`;
  } else if (checking) {
    w.innerHTML = `${icon("system_update_alt")}<div><b>Checking the new firmware</b><span>It is marked good once it has run 60 s with the settings check passed, the watchdog toggling and every sensor read once. A reset before then rolls back to the one before.</span></div><span class="count">${mss(verifyLeft())}</span>`;
  } else {
    w.innerHTML = open
      ? `${icon("system_update_alt")}<div><b>Update allowed</b><span>${upd.compressor_running ? "The compressor is running: an update never cuts a cooling run short, so it waits until it has stopped." : "Allow update was pressed on the cabinet screen. One update can be sent."}</span></div><span class="count">${mss(left)}</span>`
      : `${icon("system_update_alt")}<div><b>Updates are closed</b><span>Press <b>Allow update</b> in Settings on the cabinet's screen: it opens a 5-minute window.</span></div>`;
  }
  $("#u-send").disabled = !(open && updOk && !upd.updating && !upd.compressor_running && !checking);
  if (open && upd.compressor_running && updOk) { $("#u-check").className = "msg"; $("#u-check").textContent = "The compressor is running: an update never cuts a cooling run short. Wait for it to stop."; }
}
setInterval(() => { if (page() === "update") renderUpdateWindow(); }, 1000);
async function updatePage() {
  upd = await api("/api/update");
  updAt = Date.now();
  if (updAwaitGood && !upd.pending_verify && !upd.updating) {
    const m = $("#u-check");
    m.className = upd.last.result === "rolled_back" ? "msg bad" : "msg good";
    m.textContent = upd.last.result === "rolled_back"
      ? `${updAwaitGood} didn't start properly and was rolled back: running ${upd.build.version}.`
      : `${upd.build.version} started properly and is marked good.`;
    updAwaitGood = null;
  }
  const lastText = { none: "none yet", installed: "installed", refused: "refused", abandoned: "abandoned", rolled_back: "rolled back", failed: "failed" };
  $("#u-facts").innerHTML = [
    ["Running", `${esc(upd.build.version)} · ${upd.build.board} · ${upd.build.profile}${upd.build.test ? " · test" : ""}`],
    ["Started properly", upd.pending_verify ? "not yet: a reset now would roll back" : "yes"],
    ["Last update", `${lastText[upd.last.result] || upd.last.result}${upd.last.detail ? ` · ${esc(upd.last.detail)}` : ""}${upd.last.utc_s ? ` · ${ukWhen(upd.last.utc_s)}` : ""}`],
    ["Largest image", bytes(upd.slot_bytes)],
  ].map(([k, v]) => `<div><dt>${k}</dt><dd>${v}</dd></div>`).join("");
  renderUpdateWindow();
}
$("#u-file").addEventListener("change", async (e) => {
  updFile = e.target.files[0] || null;
  updOk = false;
  const m = $("#u-check");
  $("#u-file-name").textContent = updFile ? `${updFile.name} · ${bytes(updFile.size)}` : "Choose firmware.bin";
  if (updFile) {
    const head = new Uint8Array(await updFile.slice(0, 352).arrayBuffer());
    const { ok, text, version } = judgeImage(head);
    updOk = ok;
    if (ok && version.includes("-ROLLBACK-TEST")) {
      // B7 test 6's rollback test image: accepted, but said plainly (spec 5.2)
      m.className = "msg bad";
      m.textContent = `This is the ROLLBACK TEST image (${version}): it will roll back by itself. Every load stays off, and about 20 s after it starts the board resets and goes back to the firmware before it. B7 test 6 only.`;
    } else {
      m.className = ok ? "msg good" : "msg bad";
      m.textContent = text + (ok ? " The board checks it again before writing anything." : " Nothing will be sent.");
    }
  } else m.textContent = "";
  renderUpdateWindow();
});
$("#u-form").addEventListener("submit", (e) => {
  e.preventDefault();
  if (!updFile || !updOk) return;
  const m = $("#u-check"), bar = $("#u-progress");
  bar.hidden = false;
  $("#u-send").disabled = true;
  const x = new XMLHttpRequest();
  x.open("POST", "/api/update");
  x.setRequestHeader("Content-Type", "application/octet-stream");
  x.upload.onprogress = (p) => { if (p.lengthComputable) $("div", bar).style.width = `${(p.loaded / p.total) * 100}%`; m.className = "msg"; m.textContent = `Sending: ${Math.round((p.loaded / p.total) * 100)} %`; };
  x.onload = () => {
    let r = null;
    try { r = JSON.parse(x.responseText); } catch (_) { /* not JSON */ }
    if (x.status === 200 && r?.installed) {
      m.className = "msg good";
      m.textContent = `Installed ${r.version}. The board is restarting; this page will show it when it answers.`;
      const want = r.version, sentAt = Date.now();
      const poll = setInterval(async () => {
        try {
          const i = await api("/api/info");
          // restarted: its uptime began after the reply came (the version alone can't tell: it may be the same)
          if (i.uptime_s * 1000 >= Date.now() - sentAt) return;
          clearInterval(poll);
          m.textContent = i.build.version === want
            ? `Back on ${i.build.version}, being checked: it is marked good after 60 s of running. The boot hold applies, as after every restart.`
            : `Running ${i.build.version}: the new firmware didn't start properly and was rolled back.`;
          if (i.build.version === want) updAwaitGood = want;
          updatePage();
        } catch (_) { /* restarting */ }
      }, 3000);
    } else { m.className = "msg bad"; m.textContent = r?.message || `Refused (HTTP ${x.status}). Nothing was written.`; bar.hidden = true; renderUpdateWindow(); }
  };
  x.onerror = () => { m.className = "msg bad"; m.textContent = "The upload stopped. Nothing was installed; the board discards a half-sent image."; bar.hidden = true; };
  x.send(updFile);
});

// ---------------------------------------------------------------- pages
const PAGES = ["overview", "graphs", "sensors", "events", "settings", "logs", "update"];
function page() { const p = location.hash.slice(1); return PAGES.includes(p) ? p : "overview"; }
function show() {
  const p = page();
  for (const id of PAGES) $(`#page-${id}`).hidden = id !== p;
  $$(".tabs a").forEach((a) => (a.dataset.page === p ? a.setAttribute("aria-current", "page") : a.removeAttribute("aria-current")));
  for (const name of [...jobs.keys()]) if (name !== "status") stopJob(name);
  if (p === "overview") { $("#page-overview").classList.add("loading"); if (!settings) job("settings-once", async () => { await loadSettings(); renderOverview(); }, 0); if (status) renderOverview(); }
  if (p === "sensors" && status) renderSensors();
  if (p === "graphs") graphsPage();
  if (p === "events") { job("events", loadEvents, 0); job("events-new", newEvents, STATUS_EVERY); }
  if (p === "settings") job("settings-page", settingsPage, 0);
  if (p === "logs") job("logs", logsPage, 0);
  if (p === "update") job("update", updatePage, UPDATE_EVERY);
  document.title = p === "overview" ? "openHumidor" : `${p[0].toUpperCase()}${p.slice(1)} · openHumidor`;
}
addEventListener("hashchange", show);
job("status", loadStatus, STATUS_EVERY);
show();
