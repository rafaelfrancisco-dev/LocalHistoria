"use strict";
const $ = (id) => document.getElementById(id);
let countries = [],
  state = null,
  selected = null,
  layers,
  selectedLayer = null,
  busy = false;
const map = L.map("map", { worldCopyJump: false, minZoom: 1, preferCanvas: true }).setView(
  [22, 8],
  2,
);
map.attributionControl.setPrefix(false);
map.attributionControl.addAttribution('Supplied outlines · <a href="https://www.geoboundaries.org/">geoBoundaries gbOpen</a> (modified, CC BY 4.0) · fictional sandbox');
async function api(path, body) {
  const r = await fetch(
    "/api/v1/" + path,
    body
      ? {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(body),
        }
      : {},
  );
  const j = await r.json();
  if (!r.ok) throw Error(j.error?.message || "Request failed");
  return j;
}
function status(s) {
  $("status").textContent = s;
}
function protect(fn) {
  return async () => {
    try {
      await fn();
    } catch (e) {
      status(e.message);
    }
  };
}
function month(turn) {
  return new Date(Date.UTC(2026, turn, 1)).toLocaleDateString("en", {
    month: "long",
    year: "numeric",
    timeZone: "UTC",
  });
}
function flag(img, code) {
  img.hidden = false;
  img.src = "/flags/" + code.toLowerCase() + ".svg";
  img.onerror = () => {
    img.hidden = true;
  };
}
function list() {
  const q = $("search").value.toLowerCase();
  $("countries").replaceChildren();
  for (const c of countries.filter((c) =>
    (c.name + " " + c.code).toLowerCase().includes(q),
  )) {
    const b = document.createElement("button");
    b.className = "country" + (selected === c.code ? " selected" : "");
    b.setAttribute("aria-pressed", selected === c.code);
    b.setAttribute("aria-label", c.name);
    const img = document.createElement("img");
    img.alt = "";
    flag(img, c.code);
    b.append(img, document.createTextNode(c.name));
    if (state?.player === c.code) {
      const you = document.createElement("small");
      you.className = "you";
      you.textContent = "YOU";
      b.append(you);
    }
    b.onclick = protect(() => select(c.code, true));
    $("countries").append(b);
  }
}
function styleFor(f) {
  const owner = f.properties.owner ?? f.properties.code;
  const isSelected = owner === selected;
  const isPlayer = state && owner === state.player;
  return {
    color: isPlayer && !isSelected ? "#dcbb7b" : "#a0b5b2",
    weight: isSelected || isPlayer ? 2 : 0.6,
    dashArray: f.properties.controller && f.properties.controller !== owner ? "3 3" : null,
    fillColor: isSelected
      ? "#d9b875"
      : state?.actors?.[owner]?.color || (f.properties.playable
        ? "#537d83"
        : "#39464d"),
    fillOpacity: isSelected ? 0.9 : 0.65,
  };
}
function restyleMap() {
  layers?.eachLayer((l) => l.setStyle(styleFor(l.feature)));
}
function renderStats() {
  $("stats").replaceChildren();
  const c = state?.countries[selected];
  if (!c) return;
  for (const key of [
    "treasury",
    "income",
    "stability",
    "army",
    "navy",
    "posture",
    "priority",
  ]) {
    const cell = document.createElement("div");
    cell.className = "stat";
    const strong = document.createElement("strong");
    strong.textContent = c[key];
    const label = document.createElement("span");
    label.textContent = key;
    cell.append(strong, label);
    $("stats").append(cell);
  }
  const rel = state.relations[[state.player, selected].sort().join(":")] || 0;
  $("country-description").textContent =
    selected === state.player
      ? "Your country · fictional statistics"
      : `Fictional statistics · relations with you: ${rel}`;
}
async function select(code, zoom = false) {
  selected = code;
  list();
  restyleMap();
  layers?.eachLayer((l) => {
    if ((l.feature.properties.owner ?? l.feature.properties.code) === code) selectedLayer = l;
  });
  if (code !== state?.player) $("target").value = code;
  if (zoom && selectedLayer)
    map.fitBounds(selectedLayer.getBounds(), { maxZoom: 5 });
  const detail = await api("country?code=" + encodeURIComponent(code));
  if (selected !== code) return;
  $("country-name").textContent = detail.name;
  flag($("flag"), code);
  $("country-description").textContent =
    "Sandbox actor · all statistics are fictional";
  $("new-game").hidden = !/^[A-Z]{2}$/.test(code);
  $("new-game").textContent = state
    ? "Start new campaign as " + detail.name
    : "Play as " + detail.name;
  $("refresh").hidden = false;
  $("cities").replaceChildren();
  for (const c of detail.cities) {
    const p = document.createElement("p");
    p.textContent =
      c.name +
      (c.population
        ? " · city population " + c.population.toLocaleString()
        : " · population unavailable");
    $("cities").append(p);
  }
  const b = detail.background;
  $("source").hidden = !b?.available;
  $("background-notice").textContent = b?.available
    ? `${b.attribution}. Revision ${b.revision}, retrieved ${new Date(b.retrieved_at * 1000).toLocaleString()}. ${b.notice}`
    : "No cached reference. Optional download requires network access.";
  $("background").textContent = b?.available
    ? b.excerpt + "\n\nHistory: " + (b.history_excerpt || "Unavailable")
    : b?.reason || "";
  if (b?.available) {
    $("source").href = b.article_url;
    $("source").textContent = b.title;
  }
  renderStats();
}
async function load() {
  const j = await api("campaign");
  state = j.state;
  if (state) {
    for (const [code, actor] of Object.entries(state.actors || {}))
      if (!countries.some(c => c.code === code)) countries.push({code, name:actor.name});
    await refreshCampaignMap();
  }
  $("llama-endpoint").value = j.llama_endpoint;
  $("date").textContent = month(state?.turn || 0);
  $("controls").hidden = !state;
  $("playing-as").hidden = !state;
  if (state) {
    flag($("playing-as-flag"), state.player);
    $("playing-as-name").textContent =
      countries.find((c) => c.code === state.player)?.name || state.player;
    $("player-label").textContent =
      "Playing as " +
      countries.find((c) => c.code === state.player)?.name +
      " · treasury " +
      state.countries[state.player].treasury;
    $("diplomacy").replaceChildren();
    for (const offer of state.offers) {
      if (offer.includes(state.player)) {
        const p = document.createElement("p");
        p.textContent = "Pending alliance: " + offer.replace(":", " → ");
        $("diplomacy").append(p);
      }
    }
    for (const alliance of state.alliances) {
      if (alliance.includes(state.player)) {
        const p = document.createElement("p");
        p.textContent = "Alliance: " + alliance.replace(":", " ↔ ");
        $("diplomacy").append(p);
      }
    }
    $("journal").replaceChildren();
    for (const entry of j.journal) {
      const box = document.createElement("article");
      box.className = "entry";
      const h = document.createElement("h3");
      h.textContent = month(entry.turn - 1);
      const p = document.createElement("p");
      p.textContent = entry.narration;
      const label = document.createElement("small");
      label.textContent = entry.narration_fallback
        ? "Factual event summary"
        : "AI narration · authoritative events below";
      box.append(h, label, p);
      const details = document.createElement("details");
      const summary = document.createElement("summary");
      summary.textContent = "Authoritative events";
      details.append(summary);
      for (const event of entry.events) {
        const line = document.createElement("p");
        line.textContent = event.text;
        details.append(line);
      }
      box.append(details);
      $("journal").append(box);
    }
    if (!j.journal.length)
      $("journal").textContent = "Your story starts in January 2026.";
    renderStats();
  }
  list();
  restyleMap();
  return j;
}
function setBusy(value) {
  busy = value;
  for (const id of ["turn", "new-game", "propose", "add"])
    $(id).disabled = value;
}
async function poll(id) {
  for (let i = 0; i < 1200; i++) {
    const j = await api("jobs?id=" + encodeURIComponent(id));
    if (j.status === "complete") return j;
    if (j.status !== "running") throw Error(j.error || j.status);
    status("Working… " + id + " · the game remains responsive");
    await new Promise((r) => setTimeout(r, 500));
  }
  throw Error("Job still running. Reload to resume monitoring.");
}
$("search").oninput = list;
$("save-endpoint").onclick = protect(async () => {
  await api("model-config", { llama_endpoint: $("llama-endpoint").value });
  status("Model endpoint updated.");
});
$("new-game").onclick = protect(async () => {
  if (state && !confirm("Replace the active campaign and its saved journal?"))
    return;
  await api("campaign", { country: selected, confirm_replace: !!state });
  $("draft").value = "[]";
  await load();
  status("Campaign started. Draft your first monthly orders.");
});
$("action").onchange = () => {
  const a = $("action").value;
  const vals =
    a === "posture"
      ? ["defensive", "neutral", "assertive"]
      : a === "priority"
        ? ["balanced", "prosperity", "welfare"]
        : [];
  $("value").replaceChildren(...vals.map((v) => new Option(v, v)));
  $("value").disabled = !vals.length;
  $("target").disabled = !["improve", "offer", "accept", "reject"].includes(a);
};
$("action").onchange();
$("add").onclick = protect(async () => {
  const draft = JSON.parse($("draft").value);
  if (!Array.isArray(draft)) throw Error("Draft must be an array");
  const action = $("action").value,
    order = { action };
  if (!$("target").disabled) order.target = $("target").value;
  if (!$("value").disabled) order.value = $("value").value;
  if (["construction","movement","conflict","territorial_diplomacy"].includes(action)) {
    order.target=$("world-target").value;order.value=$("world-description").value;
  }
  draft.push(order);
  $("draft").value = JSON.stringify(draft, null, 2);
});
$("turn").onclick = protect(async () => {
  if (busy) return;
  setBusy(true);
  try {
    const pending = {
      campaign_id: state.campaign_id,
      expected_turn: state.turn,
      idempotency_key: crypto.randomUUID(),
      orders: JSON.parse($("draft").value),
    };
    sessionStorage.setItem("pendingTurn", JSON.stringify(pending));
    const job = await api("turns", pending);
    const result = await poll(job.id);
    sessionStorage.removeItem("pendingTurn");
    $("draft").value = "[]";
    await load();
    status("Month complete · autosaved.");
  } finally {
    setBusy(false);
  }
});
$("propose").onclick = protect(async () => {
  setBusy(true);
  try {
    const job = await api("proposals", {
      campaign_id: state.campaign_id,
      expected_turn: state.turn,
      text: $("instruction").value,
    });
    const result = await poll(job.id);
    if (
      result.campaign_id !== state.campaign_id ||
      result.expected_turn !== state.turn
    )
      throw Error("Proposal expired");
    $("draft").value = JSON.stringify(result.orders, null, 2);
    status(
      result.message +
        " · Review the draft, then explicitly advance the month.",
    );
  } finally {
    setBusy(false);
  }
});
$("refresh").onclick = protect(async () => {
  $("refresh").disabled = true;
  try {
    const job = await api("history", { code: selected });
    const result = await poll(job.id);
    await select(selected);
    status(
      result.result.available
        ? "Reference cached with source attribution."
        : "Reference unavailable: " + result.result.reason,
    );
  } finally {
    $("refresh").disabled = false;
  }
});
const entityLayers = L.layerGroup().addTo(map);
const mapControls = L.control.layers({}, {Entities:entityLayers}).addTo(map);
let entityRequest = 0;
async function refreshEntities() {
  if (!state) return;
  const request = ++entityRequest, b=map.getBounds();
  const entities=await api(`entities?west=${b.getWest()}&east=${b.getEast()}&south=${b.getSouth()}&north=${b.getNorth()}&zoom=${map.getZoom()}`);
  if (request !== entityRequest) return;
  entityLayers.clearLayers();
  for (const e of entities) {
    const label=document.createElement("div");
    label.textContent=`${e.name} · ${e.kind} · ${e.actor} · ${e.status} · ${e.id} · region ${e.region} · origin ${e.origin_event}`;
    L.circleMarker([e.latitude,e.longitude],{radius:5,color:state.actors[e.actor]?.color||"#fff"}).bindPopup(label).addTo(entityLayers);
  }
}
map.on("moveend",protect(refreshEntities));
let regionGeometry=null;
async function refreshCampaignMap() {
  const snapshot=await api("map");
  if (!regionGeometry) regionGeometry=await (await fetch(snapshot.geometry_url)).json();
  const geo={type:"FeatureCollection",features:regionGeometry.features.map(f=>({...f,properties:{...snapshot.territories[f.id],id:f.id}}))};
  if (layers) {mapControls.removeLayer(layers);map.removeLayer(layers);}
  layers=L.geoJSON(geo,{style:styleFor,onEachFeature:(f,l)=>{
    const p=f.properties,label=document.createElement("span");
    label.textContent=`${p.name} · ${p.id} · owner ${p.owner || "unresolved"} · controller ${p.controller || "unresolved"}`;
    l.bindTooltip(label);
    l.on("click",protect(async()=>{$("world-target").value=p.id;if(p.owner) await select(p.owner);status(label.textContent);}));
  }}).addTo(map);
  mapControls.addOverlay(layers,"Territories and occupation");
  await refreshEntities();
}
const prepare=document.createElement("button");prepare.id="prepare";prepare.textContent="Prepare campaign references";
$("controls").prepend(prepare);
prepare.onclick=protect(async()=>{
  prepare.disabled=true;
  try { const job=await api("prepare",{});const result=await poll(job.id);
    const rows=Object.entries(result.coverage||{});status(rows.map(([a,r])=>`${a}: ${r.available?"cached":r.reason||"unavailable"}`).join(" · "));
  } finally {prepare.disabled=false;}
});
protect(async () => {
  countries = await api("countries");
  list();
  $("target").replaceChildren(
    ...countries.map((c) => new Option(c.name + " (" + c.code + ")", c.code)),
  );
  const existing = await api("campaign");
  const geo = existing.state ? {type:"FeatureCollection",features:[]} : await (await fetch("/world.geojson")).json();
  layers = L.geoJSON(geo, {
    style: styleFor,
    onEachFeature: (f, l) => {
      const label = document.createElement("span");
      label.textContent =
        f.properties.name +
        (f.properties.playable ? "" : " · non-playable territory");
      l.bindTooltip(label);
      l.on(
        "click",
        protect(async () => {
          if (f.properties.playable) await select(f.properties.code);
          else
            status(
              f.properties.name +
                " has no resolved country code and remains non-playable.",
            );
        }),
      );
    },
  }).addTo(map);
  const j = await load();
  if (state) {
    await select(state.player);
    status("Campaign resumed · " + month(state.turn) + " · autosaved.");
  }
  const pending = JSON.parse(sessionStorage.getItem("pendingTurn") || "null");
  let job = j.active_jobs[0];
  if (pending && pending.campaign_id === state?.campaign_id) {
    try {
      job = await api("turns", pending);
    } catch (e) {
      status(e.message);
      sessionStorage.removeItem("pendingTurn");
    }
  }
  if (job) {
    setBusy(true);
    try {
      await poll(job.id);
      sessionStorage.removeItem("pendingTurn");
      await load();
      status("Turn recovered and autosaved.");
    } finally {
      setBusy(false);
    }
  }
})();
