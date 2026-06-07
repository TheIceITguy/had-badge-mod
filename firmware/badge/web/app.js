"use strict";
const $ = (id) => document.getElementById(id);
let SCHEMA = {};

async function getJSON(url) { return (await fetch(url)).json(); }
async function postJSON(url, obj) {
  return (await fetch(url, {method: "POST", headers: {"Content-Type": "application/json"},
                           body: JSON.stringify(obj)})).json();
}

function inputFor(key, s) {
  if (s.type === "bool") {
    const cb = document.createElement("input");
    cb.type = "checkbox"; cb.checked = !!s.value; cb.dataset.key = key; cb.dataset.type = "bool";
    return cb;
  }
  if (s.type === "enum") {
    const sel = document.createElement("select");
    sel.dataset.key = key; sel.dataset.type = "enum";
    (s.choices || []).forEach((c) => {
      const o = document.createElement("option"); o.value = c; o.textContent = c;
      if (c === s.value) o.selected = true; sel.appendChild(o);
    });
    return sel;
  }
  const inp = document.createElement("input");
  inp.dataset.key = key; inp.dataset.type = s.type; inp.dataset.secret = s.secret ? "1" : "";
  inp.type = s.secret ? "password" : "text";
  inp.value = s.secret ? "" : (s.value == null ? "" : s.value);
  if (s.secret) inp.placeholder = "(unchanged)";
  return inp;
}

function renderSettings(schema) {
  SCHEMA = schema;
  const root = $("settings"); root.innerHTML = "";
  const groups = {};
  for (const [key, s] of Object.entries(schema)) (groups[s.group] = groups[s.group] || []).push([key, s]);
  for (const [group, items] of Object.entries(groups)) {
    const gt = document.createElement("div"); gt.className = "group-title"; gt.textContent = group;
    root.appendChild(gt);
    for (const [key, s] of items) {
      const row = document.createElement("div"); row.className = "field";
      const lab = document.createElement("label"); lab.textContent = s.label; lab.title = s.help || "";
      row.appendChild(lab); row.appendChild(inputFor(key, s)); root.appendChild(row);
    }
  }
}

function collect(filterKeys) {
  const out = {};
  document.querySelectorAll("#settings [data-key]").forEach((el) => {
    const key = el.dataset.key;
    if (filterKeys && !filterKeys(key)) return;
    if (el.dataset.type === "bool") out[key] = el.checked;
    else if (el.dataset.secret === "1" && el.value === "") return; // unchanged secret
    else out[key] = el.value;
  });
  return out;
}

async function loadStatus() {
  const st = await getJSON("/api/status");
  $("devname").textContent = st.name || "Badge";
  $("version").textContent = st.project + " v" + st.version;
  $("repo").href = st.repo; $("repo").textContent = st.repo;
  const net = st.net ? (st.net.active || "?") : "?";
  const ip = st.wifi ? (st.wifi.ip || "-") : "-";
  const bat = st.battery && st.battery.pct != null ? st.battery.pct + "%" : "n/a";
  $("status").textContent = `node ${st.node} · stack ${net} · ip ${ip} · batt ${bat}`;
}

async function loadNodes() {
  const tb = $("nodes").querySelector("tbody"); tb.innerHTML = "";
  for (const n of await getJSON("/api/nodes")) {
    const tr = document.createElement("tr");
    tr.innerHTML = `<td>${n.num}</td><td>${n.name}</td><td>${n.lat ?? ""}</td>` +
                   `<td>${n.lon ?? ""}</td><td>${n.snr ?? ""}</td>`;
    tb.appendChild(tr);
  }
}

$("save").onclick = async () => {
  const r = await postJSON("/api/settings", collect());
  $("savemsg").textContent = r.ok ? "saved" : ("errors: " + (r.errors || []).join(", "));
  loadStatus();
};
$("applywifi").onclick = async () => {
  const r = await postJSON("/api/wifi", collect((k) => k.startsWith("wifi_")));
  $("savemsg").textContent = r.ok ? "wifi applied" : "wifi error";
  setTimeout(loadStatus, 1500);
};
$("refreshnodes").onclick = loadNodes;
$("reboot").onclick = async () => { await fetch("/api/reboot", {method: "POST"}); $("upmsg").textContent = "rebooting…"; };
$("upload").onclick = async () => {
  const f = $("upfile").files[0]; const path = $("uppath").value.trim();
  if (!f || !path) { $("upmsg").textContent = "pick a file and path"; return; }
  const body = await f.arrayBuffer();
  const r = await (await fetch("/api/upload?path=" + encodeURIComponent(path),
                               {method: "POST", body})).json();
  $("upmsg").textContent = r.ok ? ("uploaded " + r.path) : ("error: " + (r.error || "?"));
};

(async () => { await loadStatus(); renderSettings(await getJSON("/api/settings")); loadNodes(); })();
