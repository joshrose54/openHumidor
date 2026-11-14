/* openHumidor dashboard (spec 5.5, docs/api.md API version 1). Plain JavaScript, no framework.
 *
 * docs/api.md API version 5.
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

const API_VERSION = 5;
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
  // a no-break space keeps each figure with its unit ("2\u00a0s" never splits across lines)
  const u = (n, w) => `${n}\u00a0${w}`;
  if (s < 60) return u(Math.round(s), "s");
  if (s < 3600) return u(Math.round(s / 60), "min");
  if (s < 86400) { const h = Math.floor(s / 3600), m = Math.round((s % 3600) / 60); return m ? `${u(h, "h")} ${u(m, "min")}` : u(h, "h"); }
  const d = Math.floor(s / 86400), h = Math.round((s % 86400) / 3600);
  return h ? `${u(d, "d")} ${u(h, "h")}` : u(d, "d");   // rule 8: a zero second part dropped ("31 d")
}
// Rule 8: no space before °C, % or V; a (non-breaking) space before a word unit ("5\u00a0min")
const unitAfter = (u) => (!u ? "" : u === "°C" || u === "%" || u === "V" ? u : `\u00a0${u}`);
const cap = (t) => (t ? t[0].toUpperCase() + t.slice(1) : t);
const minus = (n) => String(n).replace("-", "\u2212");
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
    throw new ApiError(0, { message: "This page is out of date. Reload it." });   // the board's API version differs
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
    note.textContent = "Board busy. Trying again.";
  } else {
    note.textContent = statusAt ? `Board not answering. Figures from ${status?.time?.uk ?? "--:--"}; trying again.` : "Board not answering yet. Trying again.";
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
  f.textContent = s < 5 ? "Updated just now" : `Updated ${ago(s)} ago`;
  f.classList.toggle("stale", s > 75);
}, 1000);

// ---------------------------------------------------------------- header
function renderHeader() {
  const s = status;
  $("#hdr-time").textContent = s.time.uk;
  $("#hdr-modes").innerHTML = s.modes.map((m) => {
    const d = MODES[m] || MODES.standby;
    const left = m === "starting" ? ` ${mss(s.boot_hold_s)}` : m === "updating" ? ` ${s.update.pct}%` : "";
    return `<span class="mode c-${d.c}">${icon(d.icon)}<span class="mode-word">${d.label}${left}</span></span>`;
  }).join("");
  const tag = s.build.profile === "bench" ? (s.build.test ? "BENCH TEST" : "BENCH") : s.build.test ? "TEST" : "";
  $("#hdr-tag").textContent = tag;
  $("#hdr-tag").hidden = !tag;
}

// ---------------------------------------------------------------- Overview: the bands
// Spec 5.5 rule 1: each fact once. The header says the mode, the bands where the reading sits,
// each load's row what holds it; there are no "why" sentences beside the readings.
const tune = (name) => settings?.tuning.find((t) => t.name === name)?.value;
const guardOf = (load) => status.guards.find((g) => g.load === load);
// "Waiting · changeover 9:33"; a load switched off in Settings says only that
const heldText = (g) => g.reason === "disabled" ? "Off in Settings" : `Waiting · ${word("guards", g.reason)}${g.left_s !== null ? ` ${mss(g.left_s)}` : ""}`;
const offInSettings = (field) => settings && settings.user[field]?.value === false;

function band(el, { lo, hi, zones, ticks, value, set, label, unit }) {
  const span = hi - lo;
  const pos = (v) => Math.min(100, Math.max(0, ((v - lo) / span) * 100));
  const narrow = matchMedia("(max-width: 760px)").matches;
  const here = value === null ? -1 : zones.findIndex((z) => value >= z.from && value <= z.to);
  el.classList.toggle("unknown", value === null);
  el.setAttribute("aria-label", `${label}: ${value === null ? "no reading" : fmt(value, unit === "%" ? 0 : 1) + unit}${here >= 0 ? `, ${zones[here].label.toLowerCase()}` : ""}`);
  const prev = el.dataset.prev;
  el.innerHTML =
    // each zone placed from its values alone, so its edges sit exactly on their ticks
    `<div class="track">${zones.map((z, i) => { const a = pos(Math.max(z.from, lo)), b = pos(Math.min(z.to, hi)); return `<div class="z ${z.cls}${i === here ? " here" : ""}" style="left:${a}%;width:${b - a}%"><span class="zl">${esc(narrow ? z.short : z.label)}</span></div>`; }).join("")}</div>` +
    `<div class="setline" style="left:${pos(set)}%"></div>` +
    ticks.map((t) => `<span class="tick${t.set ? " set" : ""}" style="left:${pos(t.v)}%">${esc(t.text)}</span>`).join("") +
    (value === null ? "" : `<div class="mark" style="left:${prev ?? pos(value)}%"><span>${value < lo ? "◂ " : ""}${fmt(value, unit === "%" ? 0 : 1)}${value > hi ? " ▸" : ""}</span></div>`);
  // each zone's word clear of the marker and the target line: centred if it can be, otherwise in the
  // widest free stretch of its zone. Where none is wide enough the word moves: the reading's zone
  // into the marker's caption ("68 · Drying"), so the bar still names it; another zone's is hidden.
  const W = $(".track", el).clientWidth;
  if (W > 0) {
    const across = [pos(set), ...(value === null ? [] : [pos(value)])].map((p) => (p / 100) * W);
    $$(".z", el).forEach((zd) => {
      const sp = $(".zl", zd), zl = zd.offsetLeft, zw = zd.offsetWidth, sw = sp.offsetWidth, pad = 10, clear = 9;
      let free = [[pad, zw - pad]];
      for (const x0 of across) {
        const x = x0 - zl;
        free = free.flatMap(([a, b]) => (x + clear <= a || x - clear >= b ? [[a, b]] : [[a, x - clear], [x + clear, b]])).filter(([a, b]) => b - a >= sw);
      }
      if (!free.length) {
        sp.style.visibility = "hidden";
        const cap = zd.classList.contains("here") && $(".mark span", el);
        if (cap) {
          cap.textContent += ` · ${sp.textContent}`;
          // kept inside the bar at either end
          const mx = (pos(value) / 100) * W, half = cap.offsetWidth / 2;
          if (mx + half > W) cap.style.transform = `translateX(calc(-100% + ${W - mx}px))`;
          else if (mx - half < 0) cap.style.transform = `translateX(${-mx}px)`;
        }
        return;
      }
      const mid = zw / 2;
      const seg = free.find(([a, b]) => mid - sw / 2 >= a && mid + sw / 2 <= b) || free.sort((p, q) => (q[1] - q[0]) - (p[1] - p[0]))[0];
      const c = Math.min(Math.max(mid, seg[0] + sw / 2), seg[1] - sw / 2);
      sp.style.left = `${c - sw / 2}px`;
      sp.style.transform = "translateY(-50%)";
    });
  }
  // the ticks below the bar (the marker's caption is above it): an edge number that would touch the
  // target's label slides outward, its tick mark staying on its value, and never past the bar's ends
  const tks = $$(".tick", el), tgt = tks.find((t) => t.classList.contains("set")), EW = el.clientWidth;
  if (tgt && EW > 0) {
    const tc = tgt.offsetLeft, tw = tgt.offsetWidth, gap = 8;
    for (const t of tks) {
      if (t === tgt) continue;
      const c = t.offsetLeft, w = t.offsetWidth, need = (tw + w) / 2 + gap;
      if (Math.abs(c - tc) >= need) continue;
      const to = Math.min(Math.max(c < tc ? tc - need : tc + need, w / 2), EW - w / 2);
      t.style.setProperty("--dx", `${to - c}px`);
    }
  }
  if (value !== null) {   // one authored moment: the marker moves along its band to the new reading
    const m = $(".mark", el), to = pos(value);
    el.dataset.prev = String(to);
    if (prev !== undefined && Number(prev) !== to) requestAnimationFrame(() => requestAnimationFrame(() => { m.style.left = `${to}%`; }));
  }
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
  // under each band: the spread, from the ends as shown (so the figures agree); the sensors once
  const r1 = (v) => Math.round(v * 10) / 10;
  $("[data-k=spread]", T).textContent = c.t === null ? (c.healthy < 3 ? `${c.healthy} of 4 sensors healthy` : "No fresh reading")
    : `Spread ${fmt(r1(c.t_max) - r1(c.t_min))}°C (${fmt(c.t_min)}–${fmt(c.t_max)}) · ${c.healthy} of 4 sensors · read ${ago(c.age_s)} ago`;
  const rhs = s.sensors.filter((x) => x.role === "cabinet" && x.state === "healthy" && x.rh !== null).map((x) => Math.round(x.rh));
  $("[data-k=spread]", H).textContent = c.rh === null || rhs.length < 2 ? ""
    : `Spread ${Math.max(...rhs) - Math.min(...rhs)}% (${Math.min(...rhs)}–${Math.max(...rhs)}) · read ${ago(c.age_s)} ago`;
  // the bands: edges from the setpoints and the tuning values (5.5: read only)
  const st = s.setpoints.t, hyst = tune("cooling_hysteresis_c") ?? 0.5, db = tune("heating_deadband_c") ?? 1.5;
  band($("[data-k=band]", T), {
    label: "Temperature", unit: "°C", value: c.t, set: st,
    lo: st - db - 1.5, hi: st + hyst + 1.5,
    zones: [
      { from: -99, to: st - db, cls: "z-heat", label: "Heating", short: "Heating" },
      { from: st - db, to: st + hyst, cls: "z-dead", label: "Holding", short: "Holding" },
      { from: st + hyst, to: 99, cls: "z-cool", label: "Cooling", short: "Cooling" },
    ].map((z, i, a) => ({ ...z, from: i === 0 ? st - db - 1.5 : z.from, to: i === a.length - 1 ? st + hyst + 1.5 : z.to })),
    ticks: [{ v: st - db, text: fmt(st - db) }, { v: st, text: `${fmt(st)} target`, set: true }, { v: st + hyst, text: fmt(st + hyst) }],
  });
  const sr = s.setpoints.rh, b = tune("humidify_band_pts") ?? 2, off = tune("rh_strip_offset_pct") ?? 5;
  const rlo = sr - b - 4, rhi = sr + off + 3;
  band($("[data-k=band]", H), {
    label: "Humidity", unit: "%", value: c.rh, set: sr, lo: rlo, hi: rhi,
    zones: [
      { from: rlo, to: sr - b, cls: "z-humid", label: "Humidifying", short: "Humidifying" },
      { from: sr - b, to: sr + off, cls: "z-dead", label: "Holding", short: "Holding" },
      { from: sr + off, to: rhi, cls: "z-dry", label: "Drying", short: "Drying" },
    ],
    ticks: [{ v: sr - b, text: fmt(sr - b, 0) }, { v: sr, text: `${fmt(sr, 0)} target`, set: true }, { v: sr + off, text: fmt(sr + off, 0) }],
  });
  // the Allow update window (Starting and Updating are the header's): the pill opens Update
  const lines = [];
  if (s.update.window_open) lines.push(`<a class="pill acc" href="#update">${icon("system_update_alt")}<span>Update allowed · ${mss(s.update.window_s)}</span></a>`);
  $("#ov-lines").innerHTML = lines.join("");
  // alerts: every one, most serious first (the LCD's list)
  $("#ov-alerts").innerHTML = s.alerts.length
    ? s.alerts.map((a) => `<li class="${a.fault ? "fault" : ""}">${icon("warning")}<span>${esc(a.title)}</span><span class="when">${esc(a.since)}</span></li>`).join("")
    : `<li class="none">No active alerts</li>`;
  // loads, as running, and what holds them
  const L = s.loads, rows = [];
  const row = (name, ic, cls, on, text, held) => rows.push(`<div class="${on ? "on" : ""}${held ? " held" : ""}"><dt class="${on ? cls : ""}">${icon(ic)}${name}</dt><dd>${text}</dd></div>`);
  const gc = guardOf("compressor"), gh = guardOf("heater"), gu = guardOf("humidifier");
  row("Compressor", L.compressor.stripping ? "humidity_low" : "ac_unit", L.compressor.stripping ? "c-dry" : "c-cool", L.compressor.on,
      L.compressor.on ? `On for ${ago(L.compressor.for_s)}` : gc ? esc(heldText(gc)) : L.compressor.stopped_ago_s !== null ? `Off for ${ago(L.compressor.stopped_ago_s)}` : "Off", !L.compressor.on && gc);
  row("Heater", "local_fire_department", "c-heat", L.heater.on, L.heater.on ? `On · ${L.heater.duty_pct}%` : offInSettings("heater_enable") ? "Off in Settings" : gh ? esc(heldText(gh)) : "Off", !L.heater.on && gh);
  row("Humidifier", "water_drop", "c-humid", L.humidifier.on, L.humidifier.on ? "On" : offInSettings("humidifier_enable") ? "Off in Settings" : gu ? esc(heldText(gu)) : "Off", !L.humidifier.on && gu);
  row("Plenum fan", "air", "", L.fan.rpm > 0, `${L.fan.pwm}% · ${num(L.fan.rpm)}\u00a0rpm`, L.fan.pwm >= 20 && L.fan.rpm === 0);
  row("Power to", "settings", "", false, cap(L.interlock));   // the K1 relay: "Power to · Cooling"
  if (L.blowers) {   // Rev Q (4.6b): the mode decides the duty; full speed in boost or a fault
    const B = L.blowers;
    // the spread is said once, under the bands; a fault once, by the header and the alerts
    const text = B.mode === "idle" || B.mode === "updating" ? `Idle · ${B.pwm}%`
      : B.mode === "full" ? `Full speed${B.why && B.why !== "fault" ? ` · ${esc(word("blower_why", B.why))}` : ""}`
      : `Boost${B.idle_in_s !== null ? ` · idle in ${mss(B.idle_in_s)}` : ""}`;
    row("Blowers", "air", "", true, text, B.mode === "full" && B.why === "fault");
  }
  if (L.ion) {       // Rev Q (4.6c): short runs from the clock; times are UK local from the board
    const I = L.ion;
    // a skipped run is its own event (Events); the clock alert says when the time isn't trusted
    const text = I.state === "running" ? `On · ${mss(I.left_s)} left`
      : I.state === "waiting" ? (I.next ? `Next run ${esc(I.next)}` : `Next run in ${ago(I.next_in_s)}`)
      : I.state === "held" ? `Waiting · ${esc(word("ion_why", I.why))}`
      : I.state === "off" ? "Off in Settings"
      : I.state === "no_runs" ? "No runs set"
      : "Off";
    row("Ion generator", "air", "", I.on, text, I.state === "held");
  }
  $("#ov-loads").innerHTML = rows.join("");
  // the last 7 days
  const w = s.week, wk = (label, v) => `<div><dt>${label}</dt><dd>${v === null ? "Counting…" : num(v)}</dd></div>`;
  $("#ov-week").innerHTML = wk("Changeovers", w.crossovers) + wk("Drying runs", w.strips) + wk("Humidifier runs", w.humid_runs) +
    `<div><dt>Disturbances</dt><dd>${w.disturbances === null ? (w.ready ? "Off" : "Counting…") : num(w.disturbances)}</dd></div>`;
  // the Board section (5.5)
  const brd = s.board, v12 = (v) => (v === null ? "--" : `${fmt(v, 1)}V`);
  const rows2 = [
    ["Firmware", esc(brd.version)],
    ["Running for", ago(brd.uptime_s)],
    ["Last restart", esc(word("restarts", brd.restart_reason) || "--")],
    ["12V supply", brd.v12_now === null ? "--" : `${v12(brd.v12_now)} · lowest today ${v12(brd.v12_low_today)}`],
    ["WiFi signal", `${brd.wifi_rssi >= -60 ? "Good" : brd.wifi_rssi >= -70 ? "Fair" : "Weak"} · ${minus(brd.wifi_rssi)}\u00a0dBm`],
    ["Free memory", `${Math.round(brd.heap_free_bytes / 1024)}\u00a0KB`],
    ["SD card", brd.card_free_bytes === null ? "Not available" : `${bytes(brd.card_free_bytes)} free of ${bytes(brd.card_total_bytes)}`],
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
const AXIS_W = () => (chartWidth() < 500 ? 76 : 84);   // the y axis, the same on all three charts: room for "Compressor"

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
    axes: [timeAxis(), axis({ size: AXIS_W(), incrs: [0.25, 0.5, 1, 2, 5], values: (u, v) => v.map((x) => x.toFixed(1)) })],
  }, tData, $("#g-temp")));
  const rSeries = [{}, { label: "cabinet", stroke: COLOURS.humid, width: 2, points: { show: false } }, { label: "set", stroke: COLOURS.muted, width: 1, dash: [5, 5], points: { show: false } }];
  const rData = [d.t, d.cab_rh, setRh];
  for (let ch = 0; ch < 6; ch++) if (shown.has(ch)) { rSeries.push({ label: `ch${ch}`, stroke: SENSOR_COLOURS[ch], width: 1.25, points: { show: false } }); rData.push(d[`ch${ch}_rh`]); }
  charts.push(new uPlot({
    width: w, height: phone ? 170 : 220, tzDate: UK, cursor: { sync, points: { show: false } }, legend: { show: false },
    hooks: { drawClear: [zones([[-99, sr === null ? null : sr - hb, "rgba(70,194,174,0.11)"], [sr === null ? null : sr + off, 999, "rgba(224,184,80,0.10)"]])] },
    series: rSeries, axes: [timeAxis(), axis({ size: AXIS_W(), incrs: [1, 2, 5, 10], values: (u, v) => v.map((x) => x.toFixed(0)) })],
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
    axes: [timeAxis(), axis({ size: AXIS_W(), splits: () => [0.36, 1.36, 2.36], values: () => ["Humidifier", "Heater", "Compressor"], grid: { show: false } })],
  }, lData, $("#g-loads")));
}

function renderGraphSummary() {
  const d = gData, s = status;
  const pct = (v) => (v === null ? "--" : `${Math.round(v * 100)}%`);
  const items = [["Compressor on", pct(d.duty.compressor)], ["Heater on", pct(d.duty.heater)], ["Humidifier on", pct(d.duty.humidifier)]];
  if (s && (gRange === "week" || gRange === "month")) {
    items.push(["Changeovers, 7 days", s.week.crossovers ?? "…"], ["Drying runs, 7 days", s.week.strips ?? "…"], ["Humidifier runs, 7 days", s.week.humid_runs ?? "…"]);
  }
  $("#g-sum").innerHTML = items.map(([k, v]) => `<div><dt>${k}</dt><dd>${v}</dd></div>`).join("");
  const empty = d.t.length === 0;
  $("#g-empty").hidden = !empty;
  $$("#page-graphs .chart, #g-sensors, #g-sum").forEach((el) => (el.hidden = empty));
  $("#g-empty").textContent = "Nothing logged in this range yet.";
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
// the positions in channel order, by their names from the sensor table (web/words.js)
$("#g-sensors").insertAdjacentHTML("beforeend", Object.values(WORDS.positions)
  .map((n, ch) => `<label style="color:${SENSOR_COLOURS[ch]}"><input type="checkbox" value="${ch}"><span style="color:var(--text)">${esc(n)}</span></label>`).join(""));
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
  $("#s-sub").textContent = `${healthyCab} of 4 cabinet sensors healthy · 3 needed`;
  const stateText = (x) => x.state === "healthy" ? "Healthy" : x.state === "manual" ? "Excluded by hand" : x.state === "not_read" ? "Not read yet" : `Excluded: ${word("exclusions", x.reason)}`;
  $("#s-table tbody").innerHTML = s.sensors.map((x) => {
    const manual = x.state === "manual";
    return `<tr>
      <td class="ch">${x.ch}</td>
      <td>${esc(word("positions", x.name))}<div class="pos">Probe ${esc(x.probe)}</div></td>
      <td><span class="state ${x.state}">${esc(stateText(x))}</span></td>
      <td class="n">${x.t === null ? "--" : fmt(x.t, 2) + "°C"}</td>
      <td class="n">${x.rh === null ? "--" : fmt(x.rh, 1) + "%"}</td>
      <td class="n dim">${x.rh_raw === null ? "--" : fmt(x.rh_raw, 1) + "%"}</td>
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
    const q = `Exclude ${word("positions", x.name)}? It stays out until you bring it back.` +
      (fault ? `\n\n${x.role === "chamber" ? "No chamber sensor left" : `Only ${left} cabinet ${left === 1 ? "sensor" : "sensors"} left`}: this causes a Fault and stops every load.` : "");
    if (!confirm(q)) return;
  }
  b.disabled = true;
  try {
    const r = await post("/api/sensors/exclude", { channel: ch, excluded: ex });
    $("#s-note").textContent = r.excluded ? `${word("positions", r.name)} excluded.` : `${word("positions", r.name)} back: in use after 10 good reads.`;
    await loadStatus();
  } catch (err) {
    $("#s-note").textContent = err.body?.message || "No answer from the board. Nothing changed; try again.";
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
  // the sentence names its subject: the type is its dot (its name for screen readers); the logged
  // text shows on hover or tap (5.5)
  return `<li${freshRow ? ' class="fresh-row"' : ""} data-type="${esc(e.type)}" tabindex="0"><time class="${e.trusted ? "" : "untrusted"}" title="${esc(e.utc)}${e.trusted ? "" : " (clock not set)"}">${esc(time)}</time><span class="type t-${TYPE_COLOUR[e.type] || ""}"><span class="sr">${esc(word("event_types", e.type))}</span></span><span class="ev"><span class="sentence">${esc(e.sentence || e.detail)}</span><small class="logged">${esc(e.detail)}</small></span></li>`;
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
  // by name: one name may cover two types (Clock: rtc and ntp; Memory: heap and psram)
  const names = [...new Set(evRows.map((e) => word("event_types", e.type)))].sort();
  $("#e-type").innerHTML = `<option value="">All</option>` + names.map((x) => `<option${x === t ? " selected" : ""}>${esc(x)}</option>`).join("");
  const rows = evRows.filter((e) => !t || word("event_types", e.type) === t);
  $("#e-list").innerHTML = rows.length ? evList(rows, newCount) : `<li class="empty">${t ? `No ${esc(t)} events loaded` : "No events"}</li>`;
  $("#e-older").hidden = !evOlder;
}
async function loadEvents() {
  const r = await api("/api/events");
  const ram = r.source === "ram";
  $("#e-ram").hidden = !ram;
  $("#e-ram").textContent = ram ? "SD card not available: recent events only." : "";
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
// a tap shows the row's logged text (a hover does on a computer)
$("#e-list").addEventListener("click", (e) => { const li = e.target.closest("li[data-type]"); if (li) li.classList.toggle("open"); });
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
    m.className = "msg good"; m.textContent = "Door test logged";
    $("#e-note").value = "";
    newEvents();
  } catch (err) { m.className = "msg bad"; m.textContent = err.body?.message || "No answer from the board. Nothing logged; try again."; }
});

// ---------------------------------------------------------------- Settings (never refreshed while open)
let draft = null;
// the user settings' values (each an object with its value; ion_minutes_per_day_max is a plain number)
const userDraft = () => Object.fromEntries(Object.entries(settings.user).filter(([, v]) => v && typeof v === "object").map(([k, v]) => [k, v.value]));
// The ion generator's runs: fixed UTC slots from 00:00, 24/N hours apart (spec 4.6c), shown in UK
// time like every other time (rule 8): "00:00 and 12:00", or "01:00 and 13:00" in summer.
function ionTimes(runs) {
  const now = new Date(), day = Date.UTC(now.getUTCFullYear(), now.getUTCMonth(), now.getUTCDate());
  const t = Array.from({ length: runs }, (_, k) => ukTime.format(new Date(day + (k * 24 / runs) * 3600e3))).sort();
  return t.length < 2 ? t.join("") : `${t.slice(0, -1).join(", ")} and ${t[t.length - 1]}`;
}
function renderSettings() {
  const u = settings.user;
  for (const el of $$(".stepper")) {
    const f = el.dataset.field, d = u[f];
    const dec = f === "target_temp_c" ? 1 : 0;
    const unit = unitAfter(d.unit);   // the − and + move one step: the hint gives the range only
    $("[data-k=hint]", el).textContent = `${fmt(d.min, 0)}–${fmt(d.max, 0)}${unit}`;
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
  total.textContent = perDay === 0 ? "No runs"
    : tooMuch ? `${perDay}\u00a0min a day. The limit is ${maxDay}\u00a0min.`     // the board's own words (ion_too_much)
    : `${perDay}\u00a0min a day, at ${ionTimes(draft.ion_runs_per_day)}`;

  const changed = Object.keys(draft).some((k) => draft[k] !== u[k].value);
  const updating = status?.update.updating;
  $("#st-save").disabled = !changed || updating || tooMuch;
  $("#st-cancel").disabled = !changed;
  if (updating) { $("#st-msg").className = "msg"; $("#st-msg").textContent = "Updating. Save when it has finished."; }
  $("#st-tuning").innerHTML = settings.tuning.map((t) => `<div><dt>${esc(t.label)}</dt><dd>${typeof t.value === "boolean" ? (t.value ? "On" : "Off") : `${t.unit === "°C" ? fmt(t.value, 1) : t.value}${esc(unitAfter(t.unit))}`}</dd></div>`).join("");
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
    m.className = "msg good"; m.textContent = "Saved";
    renderSettings();
    loadStatus();
  } catch (err) { m.className = "msg bad"; m.textContent = err.body?.message || "No answer from the board. Nothing changed; try again."; }
});

// ---------------------------------------------------------------- Logs
// "2026-11" -> "Nov 2026"
const monthName = (ym) => { const [y, m] = String(ym).split("-").map(Number); return m >= 1 && m <= 12 ? `${["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"][m - 1]} ${y}` : esc(ym); };
async function logsPage() {
  try {
    const r = await api("/api/logs");
    $("#l-table").closest(".table-wrap").hidden = false;
    $("#l-note").className = "note";
    $("#l-note").textContent = "A month of readings takes a few minutes. One download at a time.";
    $("#l-card").textContent = `SD card: ${bytes(r.card.free_bytes)} free of ${bytes(r.card.total_bytes)}`;
    $("#l-table tbody").innerHTML = r.files.length ? r.files.map((f) => `<tr><td>${esc(f.name)}${f.schema > 1 ? "" : ` <span class="dim">(old format)</span>`}</td><td>${esc(cap(f.kind))}</td><td>${f.month ? monthName(f.month) : "All"}</td><td class="n">${bytes(f.bytes)}</td><td class="n"><a class="btn small" href="/api/logs/file?path=${encodeURIComponent(f.path)}" download="${esc(f.name)}">${icon("download")}Download</a></td></tr>`).join("")
      : `<tr><td colspan="5" class="dim">No log files yet</td></tr>`;
  } catch (e) {
    if (e.status === 503 && e.body?.error === "card_unavailable") {
      $("#l-card").textContent = "";
      $("#l-table").closest(".table-wrap").hidden = true;
      $("#l-note").className = "board-note inline";
      $("#l-note").textContent = "SD card not available. Logs are kept in memory until it's back.";
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
      ? `${icon("system_update_alt")}<div><b>Restarting</b></div>`
      : `${icon("system_update_alt")}<div><b>Updating</b></div><span class="count">${upd.pct}%</span>`;
  } else if (checking) {
    w.innerHTML = `${icon("system_update_alt")}<div><b>Checking new firmware</b><span>A restart now goes back to the old one.</span></div><span class="count">${mss(verifyLeft())}</span>`;
  } else {
    w.innerHTML = open
      ? `${icon("system_update_alt")}<div><b>Update allowed</b>${upd.compressor_running ? "<span>Waiting · compressor on</span>" : ""}</div><span class="count">${mss(left)}</span>`
      : `${icon("system_update_alt")}<div><b>Updates closed</b><span>Press <b>Allow update</b> in Settings on the cabinet screen.</span></div>`;
  }
  $("#u-send").disabled = !(open && updOk && !upd.updating && !upd.compressor_running && !checking);
}
setInterval(() => { if (page() === "update") renderUpdateWindow(); }, 1000);
async function updatePage() {
  upd = await api("/api/update");
  updAt = Date.now();
  if (updAwaitGood && !upd.pending_verify && !upd.updating) {
    const m = $("#u-check");
    m.className = upd.last.result === "rolled_back" ? "msg bad" : "msg good";
    m.textContent = upd.last.result === "rolled_back"
      ? `${updAwaitGood} didn't start. Back on ${upd.build.version}.`
      : `Update done: ${upd.build.version}`;
    updAwaitGood = null;
  }
  const lastText = { none: "None yet", installed: "Installed", refused: "Refused", abandoned: "Stopped", rolled_back: "Rolled back", failed: "Failed" };
  const b = upd.build, buildName = b.profile === "bench" ? (b.test ? "Bench test build" : "Bench build") : b.test ? "Test build" : "Cabinet build";
  $("#u-facts").innerHTML = [
    ["Running", `${esc(b.version)} · ${buildName}`],
    ["Last update", `${lastText[upd.last.result] || esc(upd.last.result)}${upd.last.detail ? ` · ${esc(upd.last.detail)}` : ""}${upd.last.utc_s ? ` · ${ukWhen(upd.last.utc_s)}` : ""}`],
    ["Size limit", bytes(upd.slot_bytes)],
  ].map(([k, v]) => `<div><dt>${k}</dt><dd>${v}</dd></div>`).join("");
  renderUpdateWindow();
}
$("#u-file").addEventListener("change", async (e) => {
  updFile = e.target.files[0] || null;
  updOk = false;
  const m = $("#u-check");
  $("#u-file-name").textContent = updFile ? `${updFile.name} · ${bytes(updFile.size)}` : "Choose file";
  if (updFile) {
    const head = new Uint8Array(await updFile.slice(0, 352).arrayBuffer());
    const { ok, text, version } = judgeImage(head);
    updOk = ok;
    if (ok && version.includes("-ROLLBACK-TEST")) {
      // B7 test 6's rollback test image: accepted, but said plainly (spec 5.2)
      m.className = "msg bad";
      m.textContent = "Rollback test image: about 20 s after it starts, the board goes back to the firmware it has now. For B7 test 6 only.";
    } else {
      m.className = ok ? "msg good" : "msg bad";
      m.textContent = text;   // the board checks it again; a refused file leaves Send off
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
  x.upload.onprogress = (p) => { if (p.lengthComputable) $("div", bar).style.width = `${(p.loaded / p.total) * 100}%`; m.className = "msg"; m.textContent = `Sending ${Math.round((p.loaded / p.total) * 100)}%`; };
  x.onload = () => {
    let r = null;
    try { r = JSON.parse(x.responseText); } catch (_) { /* not JSON */ }
    if (x.status === 200 && r?.installed) {
      m.className = "msg good";
      m.textContent = `Installed ${r.version}. Restarting…`;
      const want = r.version, sentAt = Date.now();
      const poll = setInterval(async () => {
        try {
          const i = await api("/api/info");
          // restarted: its uptime began after the reply came (the version alone can't tell: it may be the same)
          if (i.uptime_s * 1000 >= Date.now() - sentAt) return;
          clearInterval(poll);
          m.textContent = i.build.version === want
            ? `Running ${i.build.version}`
            : `The new firmware didn't start. Back on ${i.build.version}.`;
          if (i.build.version === want) updAwaitGood = want;
          updatePage();
        } catch (_) { /* restarting */ }
      }, 3000);
    } else { m.className = "msg bad"; m.textContent = r?.message || "Refused. Nothing changed."; bar.hidden = true; renderUpdateWindow(); }
  };
  x.onerror = () => { m.className = "msg bad"; m.textContent = "Upload stopped. Nothing changed; try again."; bar.hidden = true; };
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
