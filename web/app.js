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
function flag(img, code, revision = state?.actors?.[code]?.flag_revision) {
  img.hidden = false;
  const actor = state?.actors?.[code];
  const generated = revision !== undefined || (actor && !/^[A-Z]{2}$/.test(code));
  img.src = generated
    ? `/api/v1/flag?campaign=${encodeURIComponent(state.campaign_id)}&actor=${encodeURIComponent(code)}&revision=${revision ?? "fallback"}`
    : "/flags/" + code.toLowerCase() + ".svg";
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
  if (!state?.actors?.[selected]) return;
  const territories = Object.values(state.territories);
  const entities = Object.values(state.entities).filter(
    (e) => e.actor === selected && e.status !== "destroyed",
  );
  const involved = (v) => v.status === "open" && v.actors.includes(selected);
  const count = (kind, statuses = ["active"]) =>
    entities.filter((e) => e.kind === kind && statuses.includes(e.status)).length;
  const building = entities.filter((e) => e.status === "under_construction").length;
  for (const [label, value] of [
    ["regions", territories.filter((t) => t.owner === selected).length],
    ["occupied by others", territories.filter((t) => t.owner === selected && t.controller !== selected).length],
    ["occupying", territories.filter((t) => t.controller === selected && t.owner !== selected).length],
    ["armies · fleets", count("army") + " · " + count("fleet")],
    ["under construction", building],
    ["conflicts · treaties", Object.values(state.conflicts).filter(involved).length + " · " + Object.values(state.commitments).filter(involved).length],
  ]) {
    const cell = document.createElement("div");
    cell.className = "stat";
    const strong = document.createElement("strong");
    strong.textContent = value;
    const span = document.createElement("span");
    span.textContent = label;
    cell.append(strong, span);
    $("stats").append(cell);
  }
  $("country-description").textContent =
    selected === state.player ? "Your nation" : "Another nation of this world";
  let politics = $("regional-politics");
  if (!politics) { politics = document.createElement("section"); politics.id = "regional-politics"; $("stats").after(politics); }
  politics.replaceChildren();
  for (const [id, p] of Object.entries(state.regional_politics || {})) {
    if (state.territories[id]?.controller !== selected) continue;
    const h = document.createElement("h3"); h.textContent = state.territories[id].name;
    const agenda = document.createElement("p"); agenda.textContent = `${p.agenda} · Relations: ${p.relations}`;
    politics.append(h, agenda);
    for (const d of p.demands) { const line = document.createElement("p"); line.textContent = `${d.status}: ${d.text} (${d.id})`; politics.append(line); }
  }
}
async function select(code, zoom = false) {
  selected = code;
  list();
  restyleMap();
  layers?.eachLayer((l) => {
    if ((l.feature.properties.owner ?? l.feature.properties.code) === code) selectedLayer = l;
  });
  if (zoom && selectedLayer)
    map.fitBounds(selectedLayer.getBounds(), { maxZoom: 5 });
  const detail = await api("country?code=" + encodeURIComponent(code));
  if (selected !== code) return;
  $("country-name").textContent = state?.actors?.[code]?.name || detail.name;
  flag($("flag"), code);
  $("country-description").textContent =
    "Sandbox actor";
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
    countries = Object.entries(state.actors || {}).map(([code, actor]) => ({code, name:actor.name})).sort((a,b) => a.name.localeCompare(b.name));
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
      "Playing as " + (countries.find((c) => c.code === state.player)?.name || state.player);
    renderActions();
    const expanded = new Set([...$("journal").querySelectorAll("article")].filter(e => e.querySelector("details")?.open).map(e => e.dataset.turn));
    $("journal").replaceChildren();
    for (const entry of j.journal) {
      const box = document.createElement("article");
      box.className = "entry";
      box.dataset.turn = String(entry.turn);
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
      details.open = expanded.has(String(entry.turn));
      const summary = document.createElement("summary");
      summary.textContent = "Authoritative events";
      details.append(summary);
      for (const event of entry.events) {
        const line = document.createElement("p");
        line.textContent = (event.action?.startsWith("independent_") ? "Independent initiative · " : "") + event.text;
        const structured = entry.structured_events?.find(e => e.id === event.target);
        if (structured?.regional_actor) line.prepend(`${state.territories[structured.regional_actor]?.name || structured.regional_actor} · `);
        for (const change of structured?.identity_changes || []) {
          const img = document.createElement("img"); img.width = 48; img.alt = "Flag adopted in this event";
          flag(img, change.actor, change.revision); line.append(img);
        }
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
  if (selected && state?.actors?.[selected]) { $("country-name").textContent = state.actors[selected].name; flag($("flag"), selected); }
  if (state) await refreshCampaignMap();
  return j;
}
function setBusy(value) {
  busy = value;
  for (const id of ["turn", "new-game", "add-action"])
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
  setDraft([]);
  await load();
  status("Campaign started. Write your first actions.");
});
// Free-text actions, Pax Historia style: no fixed action types, optional map targets.
let draft = [],
  activeAction = 0;
function readDraft() {
  try {
    const saved = JSON.parse(sessionStorage.getItem("actionDraft") || "null");
    if (saved?.campaign_id === state?.campaign_id && Array.isArray(saved.actions)) return saved.actions;
  } catch {}
  return [];
}
function setDraft(actions) {
  draft = actions.length ? actions : [{ text: "", targets: [] }];
  activeAction = Math.min(activeAction, draft.length - 1);
  try {
    sessionStorage.setItem("actionDraft", JSON.stringify({ campaign_id: state?.campaign_id, actions: draft }));
  } catch {}
}
function targetName(id) {
  return (
    state.territories[id]?.name ||
    state.entities[id]?.name ||
    state.actors[id]?.name ||
    id
  );
}
function renderActions() {
  if (!draft.length) setDraft(readDraft());
  $("actions").replaceChildren();
  draft.forEach((action, index) => {
    const card = document.createElement("div");
    card.className = "action-card" + (index === activeAction ? " active" : "");
    const label = document.createElement("label");
    label.htmlFor = "action-" + index;
    label.textContent = "Action " + (index + 1);
    const text = document.createElement("textarea");
    text.id = "action-" + index;
    text.rows = 3;
    text.maxLength = 500;
    text.placeholder = "What does your nation do this month?";
    text.value = action.text;
    text.oninput = () => {
      action.text = text.value;
      setDraft(draft);
    };
    text.onfocus = () => {
      if (activeAction === index) return;
      activeAction = index;
      for (const [n, c] of [...$("actions").children].entries()) c.classList.toggle("active", n === index);
    };
    const chips = document.createElement("div");
    chips.className = "chips";
    for (const id of action.targets) {
      const chip = document.createElement("button");
      chip.type = "button";
      chip.className = "chip";
      chip.textContent = targetName(id) + " ×";
      chip.setAttribute("aria-label", "Remove target " + targetName(id));
      chip.onclick = () => {
        action.targets = action.targets.filter((t) => t !== id);
        setDraft(draft);
        renderActions();
      };
      chips.append(chip);
    }
    const hint = document.createElement("small");
    hint.textContent = action.targets.length ? "" : "Click a map region to add a target (optional)";
    chips.append(hint);
    const remove = document.createElement("button");
    remove.type = "button";
    remove.className = "remove";
    remove.textContent = "Remove";
    remove.onclick = () => {
      draft.splice(index, 1);
      setDraft(draft);
      renderActions();
    };
    card.append(label, text, chips, remove);
    $("actions").append(card);
  });
  $("add-action").disabled = busy || draft.length >= 8;
}
function addTarget(id) {
  const action = draft[activeAction];
  if (!action || action.targets.includes(id)) return false;
  if (action.targets.length >= 4) throw Error("An action can have at most four targets");
  action.targets.push(id);
  setDraft(draft);
  renderActions();
  return true;
}
$("add-action").onclick = () => {
  if (draft.length >= 8) return;
  draft.push({ text: "", targets: [] });
  activeAction = draft.length - 1;
  setDraft(draft);
  renderActions();
  $("action-" + activeAction).focus();
};
$("turn").onclick = protect(async () => {
  if (busy) return;
  setBusy(true);
  try {
    const pending = {
      campaign_id: state.campaign_id,
      expected_turn: state.turn,
      idempotency_key: crypto.randomUUID(),
      orders: draft
        .filter((a) => a.text.trim())
        .map((a) => (a.targets.length ? { text: a.text.trim(), targets: a.targets } : { text: a.text.trim() })),
    };
    sessionStorage.setItem("pendingTurn", JSON.stringify(pending));
    const job = await api("turns", pending);
    const result = await poll(job.id);
    sessionStorage.removeItem("pendingTurn");
    setDraft([]);
    await load();
    status("Month complete · autosaved.");
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
    l.on("click",protect(async()=>{const added=state&&!$("controls").hidden&&addTarget(p.id);if(p.controller || p.owner) await select(p.controller || p.owner);status(added?`Added ${p.name} to action ${activeAction+1}`:label.textContent);}));
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
  setBusy(true);
  try {
  countries = await api("countries");
  list();
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
  } finally { setBusy(false); }
})();
