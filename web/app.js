"use strict";
const $ = (id) => document.getElementById(id);
let countries = [],
  state = null,
  nationalStatus = {},
  mapMode = "political",
  highlight = null,
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
  const actor = state?.actors?.[code];
  // Campaign-created actors only have generated flags, which need the loaded campaign.
  img.hidden = !actor && !/^[A-Z]{2}$/.test(code);
  if (img.hidden) return;
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
// Wars & treaties map mode: relations of the selected country, war fronts and occupations.
const INTENSITY = { low: 1, medium: 2, high: 3 };
const RELATION_COLORS = { self: "#d9b875", enemy: "#a8413a", ally: "#4f86c6", partner: "#3f9d92", war: "#6e3b36", front: "#f28c38", neutral: "#2b3a41" };
const ALLIANCE_KINDS = new Set(["alliance", "defence_pact", "union"]);
let diplomacyCache = null;
function diplomacyIndex() {
  if (diplomacyCache) return diplomacyCache;
  const index = { front: {}, atWar: new Set(), relation: {}, members: null, memberColor: null };
  for (const c of Object.values(state?.conflicts || {})) {
    if (c.status !== "open" || c.intensity === "none") continue;
    c.actors.forEach((a) => index.atWar.add(a));
    for (const r of c.affected_regions || []) index.front[r] = Math.max(index.front[r] || 0, INTENSITY[c.intensity] || 1);
  }
  const relate = (a, kind) => {
    const order = ["partner", "ally", "enemy"];
    if (a !== selected && order.indexOf(kind) > order.indexOf(index.relation[a])) index.relation[a] = kind;
  };
  if (selected) {
    for (const c of Object.values(state?.commitments || {}))
      if (c.status === "open" && c.actors.includes(selected)) c.actors.forEach((a) => relate(a, ALLIANCE_KINDS.has(c.kind) ? "ally" : "partner"));
    for (const c of Object.values(state?.conflicts || {}))
      if (c.status === "open" && c.actors.includes(selected)) c.actors.forEach((a) => relate(a, "enemy"));
  }
  if (highlight) {
    const item = state?.[highlight.kind === "conflict" ? "conflicts" : "commitments"]?.[highlight.id];
    if (item) {
      index.members = new Set(item.actors);
      index.memberColor = highlight.kind === "conflict" ? RELATION_COLORS.enemy : ALLIANCE_KINDS.has(item.kind) ? RELATION_COLORS.ally : RELATION_COLORS.partner;
    }
  }
  return (diplomacyCache = index);
}
function diplomacyStyle(f) {
  const p = f.properties, owner = p.owner ?? p.code, index = diplomacyIndex();
  const occupied = p.controller && owner && p.controller !== owner;
  let fillColor = RELATION_COLORS.neutral, fillOpacity = 0.55;
  if (index.members) {
    if (index.members.has(owner)) { fillColor = index.memberColor; fillOpacity = 0.75; }
  } else if (owner === selected) { fillColor = RELATION_COLORS.self; fillOpacity = 0.8; }
  else if (index.relation[owner]) { fillColor = RELATION_COLORS[index.relation[owner]]; fillOpacity = 0.7; }
  else if (index.atWar.has(owner)) { fillColor = RELATION_COLORS.war; fillOpacity = 0.6; }
  if (index.front[p.id]) { fillColor = RELATION_COLORS.front; fillOpacity = 0.55 + 0.13 * index.front[p.id]; }
  return {
    color: occupied ? state?.actors?.[p.controller]?.color || "#fff" : "#3d4e55",
    weight: occupied ? 2 : 0.5,
    dashArray: occupied ? "4 3" : null,
    fillColor,
    fillOpacity,
  };
}
function styleFor(f) {
  if (mapMode === "diplomacy" && state) return diplomacyStyle(f);
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
  diplomacyCache = null;
  layers?.eachLayer((l) => l.setStyle(styleFor(l.feature)));
  renderLegend();
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
  const national = nationalStatus[selected];
  if (national?.gdp) {
    const [local, dollars] = national.gdp.split(" (");
    const wars = national.wars.map((w) => `${w.name} (${w.intensity})`).join(" · ");
    for (const [label, value, detail, wide] of [
      ["GDP" + (national.estimated ? " · estimate" : ""), local, dollars ? dollars.slice(0, -1) : "", true],
      ["population", national.population_text, national.displaced ? `${national.displaced.toLocaleString()} displaced` : ""],
      ["GDP per capita", national.gdp_per_capita, ""],
      ["stability", national.stability.replace(/ \(.*/, ""), national.stability.match(/\((.*)\)/)?.[1] || ""],
      ["economy", national.economic_trend.replace(/ \(.*/, ""), national.economic_trend.match(/\((.*)\)/)?.[1] || ""],
      ["at war", national.wars.length ? wars : "At peace", "", true],
    ]) {
      const cell = document.createElement("div");
      cell.className = "stat" + (wide ? " wide" : "");
      const strong = document.createElement("strong");
      strong.textContent = value;
      const span = document.createElement("span");
      span.textContent = detail ? `${label} · ${detail}` : label;
      cell.append(strong, span);
      $("stats").append(cell);
    }
  }
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
  nationalStatus = j.national_status || {};
  if (state) {
    countries = Object.entries(state.actors || {}).map(([code, actor]) => ({code, name:actor.name})).sort((a,b) => a.name.localeCompare(b.name));
  }
  $("llama-endpoint").value = j.llama_endpoint;
  $("date").textContent = month(state?.turn || 0);
  $("controls").hidden = !state;
  $("playing-as").hidden = !state;
  $("diplomacy-open").hidden = !state;
  $("events-open").hidden = !state;
  if (state) {
    flag($("playing-as-flag"), state.player);
    $("playing-as-name").textContent =
      countries.find((c) => c.code === state.player)?.name || state.player;
    $("player-label").textContent =
      "Playing as " + (countries.find((c) => c.code === state.player)?.name || state.player);
    renderActions();
    renderProposals();
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
        line.textContent = (event.action?.startsWith("independent_") ? "Independent initiative · " : event.action === "war_toll" ? "War toll · " : "") + event.text;
        const structured = entry.structured_events?.find(e => e.id === event.target);
        if (structured?.regional_actor) line.prepend(`${state.territories[structured.regional_actor]?.name || structured.regional_actor} · `);
        flagThumbs(structured?.identity_changes, line);
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
  if ($("diplomacy").open) renderDiplomacy();
  if ($("events").open) protect(() => fetchEvents(false))();
  if (selected && state?.actors?.[selected]) { $("country-name").textContent = state.actors[selected].name; flag($("flag"), selected); }
  if (state) await refreshCampaignMap();
  return j;
}
function setBusy(value) {
  busy = value;
  for (const id of ["turn", "new-game", "add-action"])
    $(id).disabled = value;
}
function actorName(code) {
  return state?.actors?.[code]?.name || code;
}
// Renders one flushed model call as a single readable line, ahead of it being turned into map deltas.
function logText(r) {
  const a = r.answer || {};
  if (r.kind === "decide") {
    const actions = (a.actions || []).map((x) => x.text).filter(Boolean);
    return actorName(r.actor) + ": " + (actions.length ? actions.join(" · ") : "no action taken");
  }
  if (r.kind === "adjudicate") {
    const lines = [...(a.events || []), ...(a.world_events || [])]
      .map((e) => e.description ?? e.text)
      .filter(Boolean);
    return "Adjudicated the month: " + (lines.length ? lines.join(" ") : "no outcomes");
  }
  if (r.kind === "narrate") return "Journal written.";
  return "Model responded.";
}
function logLine(text) {
  const log = $("turn-log");
  const p = document.createElement("p");
  p.textContent = text;
  log.append(p);
  log.scrollTop = log.scrollHeight;
}
async function poll(id, withLog) {
  let since = 0;
  const pull = async () => {
    if (!withLog) return;
    for (const r of await api("inference?job=" + encodeURIComponent(id) + "&since=" + since)) {
      since = r.id;
      logLine(logText(r));
    }
  };
  if (withLog) {
    $("turn-log").hidden = false;
    $("turn-log").replaceChildren();
  }
  for (let i = 0; i < 1200; i++) {
    await pull();
    const j = await api("jobs?id=" + encodeURIComponent(id));
    if (j.status === "complete") {
      await pull();
      if (withLog) $("turn-log").hidden = true;
      return j;
    }
    if (j.status !== "running") {
      if (withLog) $("turn-log").hidden = true;
      throw Error(j.error || j.status);
    }
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
// Treaties AI nations offered last month: the player accepts or declines, and the answer takes effect as this month ends.
function readAnswers() {
  try {
    const saved = JSON.parse(sessionStorage.getItem("proposalAnswers") || "null");
    if (saved?.campaign_id === state?.campaign_id && saved.turn === state?.turn) return saved.answers || {};
  } catch {}
  return {};
}
function setAnswers(answers) {
  try {
    sessionStorage.setItem("proposalAnswers", JSON.stringify({ campaign_id: state?.campaign_id, turn: state?.turn, answers }));
  } catch {}
}
function pendingProposals() {
  return Object.entries(state?.proposals || {}).filter(([, p]) => p.status === "pending" && p.actors.includes(state.player));
}
function renderProposals() {
  const box = $("proposals");
  const offers = pendingProposals();
  box.hidden = !offers.length;
  box.replaceChildren();
  if (!offers.length) return;
  const h = document.createElement("h3");
  h.textContent = offers.length === 1 ? "A treaty is offered" : `${offers.length} treaties are offered`;
  box.append(h);
  const answers = readAnswers();
  for (const [id, p] of offers) {
    const card = document.createElement("article");
    card.className = "agreement proposal";
    const others = p.actors.filter((a) => a !== state.player && a !== p.from).map(actorName);
    const offered = document.createElement("p");
    offered.className = "meta";
    offered.textContent = `Offered by ${actorName(p.from)}` + (others.length ? `, with ${others.join(", ")}` : "");
    const terms = document.createElement("p");
    terms.textContent = p.text + (p.months ? ` Lasts ${p.months} month${p.months === 1 ? "" : "s"}.` : p.amends ? "" : " Open-ended.");
    const choice = document.createElement("div");
    choice.className = "chips";
    const note = document.createElement("small");
    const answer = (value) => () => {
      const next = readAnswers();
      if (next[id] === value) delete next[id];
      else next[id] = value;
      setAnswers(next);
      renderProposals();
    };
    const accept = button("Accept", "chip" + (answers[id] === true ? " chosen" : ""), answer(true));
    const decline = button("Decline", "chip" + (answers[id] === false ? " chosen" : ""), answer(false));
    accept.setAttribute("aria-pressed", String(answers[id] === true));
    decline.setAttribute("aria-pressed", String(answers[id] === false));
    note.textContent = id in answers ? "Takes effect as the month ends" : "Lapses if unanswered this month";
    choice.append(accept, decline, note);
    card.append(cardHead(p.amends ? `Amendment to the ${p.name}` : p.name, [[kindLabel(p.kind), ""]]), offered, terms);
    if (p.terms?.length) card.append(termList(p.terms));
    card.append(choice);
    box.append(card);
  }
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
    const offered = new Set(pendingProposals().map(([id]) => id));
    const responses = Object.entries(readAnswers()).filter(([id]) => offered.has(id)).map(([proposal, accept]) => ({ proposal, accept }));
    if (responses.length) pending.responses = responses;
    sessionStorage.setItem("pendingTurn", JSON.stringify(pending));
    const job = await api("turns", pending);
    const result = await poll(job.id, true);
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
    const status=e.status==="under_construction"?`under construction until ${month(e.completes_turn-1)}`:e.kind==="event_site"&&e.expires_turn!==undefined?`fades at the end of ${month(e.expires_turn-1)}`:e.status;
    label.textContent=`${e.name} · ${kindLabel(e.kind)} · ${actorName(e.actor)} · ${status} · ${state.territories[e.region]?.name||e.region}`;
    L.circleMarker([e.latitude,e.longitude],{radius:5,color:state.actors[e.actor]?.color||"#fff",dashArray:e.status==="under_construction"?"2 3":null}).bindPopup(label).addTo(entityLayers);
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
    l.bindTooltip(()=>{
      if (mapMode!=="diplomacy") return label;
      const lines=[p.name+(p.population?` · ${p.population.toLocaleString()} people`:"")+(p.displaced?` · ${p.displaced.toLocaleString()} displaced`:"")];
      if (p.controller&&p.owner&&p.controller!==p.owner) lines.push(`Occupied by ${state.actors[p.controller]?.name||p.controller}`);
      for (const c of Object.values(state.conflicts)) if (c.status==="open"&&(c.affected_regions||[]).includes(p.id)) lines.push(`Front: ${c.name} (${c.intensity})`);
      const treaties=Object.values(state.commitments).filter(c=>c.status==="open"&&c.actors.includes(p.owner)).map(c=>c.name);
      if (treaties.length) lines.push("Treaties: "+treaties.slice(0,4).join(", ")+(treaties.length>4?` +${treaties.length-4}`:""));
      const box=document.createElement("span");box.style.whiteSpace="pre-line";box.textContent=lines.join("\n");return box;
    });
    l.on("click",protect(async()=>{const added=state&&!$("controls").hidden&&addTarget(p.id);if(p.controller || p.owner) await select(p.controller || p.owner);status(added?`Added ${p.name} to action ${activeAction+1}`:label.textContent);}));
  }}).addTo(map);
  mapControls.addOverlay(layers,"Territories and occupation");
  await Promise.all([refreshEntities(), refreshEventMarkers()]);
}
// Map mode: political ownership, or wars, fronts, occupations and the selected country's treaties.
function readPreference(key, fallback) {
  try { return localStorage.getItem(key) || fallback; } catch { return fallback; }
}
function setMapMode(mode) {
  mapMode = mode === "diplomacy" ? "diplomacy" : "political";
  try { localStorage.setItem("mapMode", mapMode); } catch {}
  if (mapMode === "political") highlight = null;
  for (const b of modeControl.getContainer()?.querySelectorAll("button") || []) b.setAttribute("aria-pressed", b.dataset.mode === mapMode);
  restyleMap();
}
const modeControl = L.control({ position: "topleft" });
modeControl.onAdd = () => {
  const box = L.DomUtil.create("div", "map-modes leaflet-bar");
  for (const [mode, text] of [["political", "Political"], ["diplomacy", "Wars & treaties"]]) {
    const b = L.DomUtil.create("button", "", box);
    b.type = "button";
    b.dataset.mode = mode;
    b.textContent = text;
    b.setAttribute("aria-pressed", mode === mapMode);
    L.DomEvent.on(b, "click", (e) => { L.DomEvent.stop(e); setMapMode(mode); });
  }
  L.DomEvent.disableClickPropagation(box);
  return box;
};
modeControl.addTo(map);
const legend = L.control({ position: "bottomleft" });
legend.onAdd = () => L.DomUtil.create("div", "map-legend");
legend.addTo(map);
function renderLegend() {
  const box = legend.getContainer();
  if (!box) return;
  box.hidden = mapMode !== "diplomacy" || !state;
  box.replaceChildren();
  if (box.hidden) return;
  const index = diplomacyIndex();
  const title = document.createElement("strong");
  const item = highlight && state[highlight.kind === "conflict" ? "conflicts" : "commitments"][highlight.id];
  title.textContent = item ? item.name : `Relations of ${state.actors[selected]?.name || "the selected country"}`;
  box.append(title);
  const rows = item
    ? [[index.memberColor, highlight.kind === "conflict" ? "Parties" : "Members"], [RELATION_COLORS.front, "War front"]]
    : [[RELATION_COLORS.self, "Selected country"], [RELATION_COLORS.enemy, "At war with it"], [RELATION_COLORS.ally, "Allies (alliance, defence pact, union)"], [RELATION_COLORS.partner, "Other treaty partners"], [RELATION_COLORS.war, "Other nations at war"], [RELATION_COLORS.front, "War front (darker = more intense)"]];
  rows.push([null, "Dashed border in the occupier’s colour: occupied"]);
  for (const [color, text] of rows) {
    const row = document.createElement("div");
    const swatch = document.createElement("i");
    if (color) swatch.style.background = color; else swatch.className = "dashed";
    row.append(swatch, document.createTextNode(text));
    box.append(row);
  }
  if (item) {
    const clear = document.createElement("button");
    clear.type = "button";
    clear.textContent = "Clear highlight";
    clear.onclick = () => { highlight = null; restyleMap(); };
    box.append(clear);
  }
}
mapMode = readPreference("mapMode", "political") === "diplomacy" ? "diplomacy" : "political";
setMapMode(mapMode);

// Wars & treaties menu.
let diplomacyTab = "wars";
function kindLabel(kind) {
  return (kind || "other").replaceAll("_", " ");
}
// A treaty clause in words. Offered clauses name no previous owner yet; carried-out ones record it.
function termText(t) {
  const region = state.territories[t.region]?.name || t.region;
  const conflict = state.conflicts[t.conflict]?.name || t.conflict;
  if (t.action === "end_conflict") return `Ends the ${conflict}`;
  if (t.action === "cede") return `${t.from ? actorName(t.from) + " cedes" : "Cedes"} ${region} to ${actorName(t.to)}`;
  if (t.action === "occupy") return `${actorName(t.by)} holds ${region} while in force`;
  if (t.action === "withdraw") return `${t.by ? actorName(t.by) + " withdraws" : "Withdrawal"} from ${region}`;
  return kindLabel(t.action);
}
function termList(terms) {
  const list = document.createElement("ul");
  list.className = "terms";
  for (const t of terms || []) {
    const item = document.createElement("li");
    item.textContent = termText(t);
    list.append(item);
  }
  return list;
}
function zoomToRegions(ids) {
  const bounds = [];
  layers?.eachLayer((l) => { if (ids.includes(l.feature.properties.id)) bounds.push(l.getBounds()); });
  if (bounds.length) map.fitBounds(bounds.reduce((a, b) => a.extend(b), L.latLngBounds(bounds[0].getSouthWest(), bounds[0].getNorthEast())), { maxZoom: 6 });
}
function showOnMap(kind, id) {
  const item = state[kind === "conflict" ? "conflicts" : "commitments"][id];
  highlight = { kind, id };
  $("diplomacy").close();
  setMapMode("diplomacy");
  const regions = item.affected_regions?.length
    ? item.affected_regions
    : Object.entries(state.territories).filter(([, t]) => item.actors.includes(t.owner) && t.kind !== "residual").map(([rid]) => rid);
  zoomToRegions(regions);
}
function renderDiplomacy() {
  if (!state) return;
  const mine = $("diplomacy-mine").checked && selected;
  $("diplomacy-mine-label").textContent = `Only involving ${state.actors[selected]?.name || "the selected country"}`;
  for (const tab of document.querySelectorAll("#diplomacy [role=tab]")) tab.setAttribute("aria-selected", tab.dataset.tab === diplomacyTab);
  const items = [];
  for (const [kind, collection] of [["conflict", state.conflicts], ["commitment", state.commitments]])
    for (const [id, v] of Object.entries(collection || {})) {
      const open = v.status === "open";
      if (diplomacyTab === "wars" && !(kind === "conflict" && open)) continue;
      if (diplomacyTab === "treaties" && !(kind === "commitment" && open)) continue;
      if (diplomacyTab === "ended" && open) continue;
      if (mine && !v.actors.includes(selected)) continue;
      items.push({ kind, id, v });
    }
  items.sort((a, b) => (b.v.casualties || 0) - (a.v.casualties || 0) || (INTENSITY[b.v.intensity] || 0) - (INTENSITY[a.v.intensity] || 0) || (b.v.ended_turn ?? 0) - (a.v.ended_turn ?? 0) || String(a.v.name || a.id).localeCompare(String(b.v.name || b.id)));
  const list = $("diplomacy-list");
  list.replaceChildren();
  if (!items.length) {
    const p = document.createElement("p");
    p.className = "empty";
    p.textContent = diplomacyTab === "wars" ? "No active wars." : diplomacyTab === "treaties" ? "No treaties in force." : "Nothing has ended yet.";
    list.append(p);
  }
  for (const { kind, id, v } of items) {
    const card = document.createElement("article");
    card.className = "agreement " + kind;
    const head = cardHead(v.name || (kind === "conflict" ? "Unnamed conflict" : "Unnamed agreement"), [
      [kindLabel(v.kind), ""],
      kind === "conflict" && v.status === "open" ? [`${v.intensity || "unknown"} intensity`, "intensity-" + (v.intensity || "unknown")] : null,
    ]);
    const parties = partyChips(v.actors, "diplomacy", 10);
    if (v.actors.length > 10) parties.append(` +${v.actors.length - 10} more`);
    const meta = document.createElement("p");
    meta.className = "meta";
    const dates = [v.started_turn !== undefined ? `Since ${month(v.started_turn)}` : null, v.ended_turn !== undefined ? `ended ${month(v.ended_turn)}` : v.expires_turn !== undefined ? `until ${month(v.expires_turn)}` : null].filter(Boolean).join(", ");
    const toll = kind === "conflict" && (v.casualties || v.displaced) ? `${(v.casualties || 0).toLocaleString()} killed · ${(v.displaced || 0).toLocaleString()} displaced since 2026` + (v.last_month_casualties ? ` (${v.last_month_casualties.toLocaleString()} killed last month)` : "") : "";
    meta.textContent = [dates, toll].filter(Boolean).join(" · ");
    const text = document.createElement("p");
    text.textContent = v.text;
    card.append(head, parties, meta, text);
    if (v.terms?.length) card.append(termList(v.terms));
    const regions = document.createElement("div");
    regions.className = "chips";
    chipRow(regions, "Front:", (v.affected_regions || []).map((r) => [state.territories[r]?.name || r, () => { highlight = { kind, id }; $("diplomacy").close(); setMapMode("diplomacy"); zoomToRegions([r]); }]));
    if (regions.childElementCount) card.append(regions);
    card.append(button("Show on map", "show-on-map", () => showOnMap(kind, id)));
    list.append(card);
  }
}
// Card pieces shared by the Wars & treaties and Events menus.
function button(text, className, onclick) {
  const b = document.createElement("button");
  b.type = "button";
  b.className = className;
  b.textContent = text;
  b.onclick = onclick;
  return b;
}
// badges: [text, extra class] pairs; null entries are skipped.
function cardHead(title, badges) {
  const head = document.createElement("div");
  head.className = "agreement-head";
  const h = document.createElement("h3");
  h.textContent = title;
  const row = document.createElement("span");
  for (const [text, cls] of badges.filter(Boolean)) {
    const badge = document.createElement("span");
    badge.className = cls ? `badge ${cls}` : "badge";
    badge.textContent = text;
    row.append(badge);
  }
  head.append(h, row);
  return head;
}
function partyChips(actors, dialog, max) {
  const parties = document.createElement("div");
  parties.className = "parties";
  for (const a of actors.slice(0, max)) {
    const chip = button("", "party", protect(async () => { $(dialog).close(); await select(a, true); }));
    const img = document.createElement("img");
    img.alt = "";
    flag(img, a);
    chip.append(img, document.createTextNode(actorName(a)));
    parties.append(chip);
  }
  return parties;
}
// entries: [name, onclick] pairs; an empty row adds nothing.
function chipRow(container, label, entries) {
  if (!entries.length) return;
  const small = document.createElement("small");
  small.textContent = label;
  container.append(small, ...entries.map(([name, onclick]) => button(name, "chip", onclick)));
}
function flagThumbs(changes, into, separator) {
  for (const change of changes || []) {
    const img = document.createElement("img"); img.width = 48; img.alt = "Flag adopted in this event";
    flag(img, change.actor, change.revision);
    if (separator) into.append(separator);
    into.append(img);
  }
}
function wireDialog(id) {
  $(id + "-close").onclick = () => $(id).close();
  $(id).addEventListener("click", (e) => { if (e.target === $(id)) $(id).close(); });
}
$("diplomacy-open").onclick = () => { renderDiplomacy(); $("diplomacy").showModal(); };
wireDialog("diplomacy");
$("diplomacy-mine").onchange = renderDiplomacy;
for (const tab of document.querySelectorAll("#diplomacy [role=tab]")) tab.onclick = () => { diplomacyTab = tab.dataset.tab; renderDiplomacy(); };
// Events: markers for what is still on the map, and a menu of every event.
const EVENT_CATEGORIES = { player: "Your actions", nation: "Other nations", regional: "Regional", world: "World developments", war: "War tolls", construction: "Construction" };
const eventLayers = L.layerGroup().addTo(map);
mapControls.addOverlay(eventLayers, "Events");
let eventMarkers = new Map(), eventMarkerRequest = 0;
const locationKey = (p) => `${p.lat.toFixed(3)},${p.lon.toFixed(3)}`;
function eventPopup(events) {
  const box = document.createElement("div");
  box.className = "event-popup";
  for (const ev of events.slice(0, 8)) {
    const p = document.createElement("p");
    const small = document.createElement("small");
    small.textContent = `${month(ev.turn - 1)} · ${EVENT_CATEGORIES[ev.category] || ev.category} · ${actorName(ev.actor)}`;
    const text = document.createElement("span");
    text.textContent = ev.text.length > 240 ? ev.text.slice(0, 240) + "…" : ev.text;
    const open = document.createElement("button");
    open.type = "button";
    open.textContent = "Open in events";
    open.onclick = protect(() => openEvents(ev.id, ev.on_map ? "map" : "all"));
    p.append(small, text, document.createElement("br"), open);
    box.append(p);
  }
  if (events.length > 8) box.append(`+${events.length - 8} more in the Events menu`);
  return box;
}
async function refreshEventMarkers() {
  if (!state) return;
  const request = ++eventMarkerRequest;
  const { events } = await api("events?on_map=1");
  if (request !== eventMarkerRequest) return;
  eventLayers.clearLayers();
  const groups = new Map();
  for (const ev of events)
    for (const p of ev.location) {
      const key = locationKey(p);
      if (!groups.has(key)) groups.set(key, { p, events: [] });
      groups.get(key).events.push(ev);
    }
  eventMarkers = new Map();
  for (const [key, { p, events: here }] of groups) {
    const icon = L.divIcon({ className: "", iconSize: [20, 20], html: `<div class="event-marker" style="--marker:${state.actors[here[0].actor]?.color || "#dcbb7b"}"><span>${here.length > 1 ? here.length : ""}</span></div>` });
    const marker = L.marker([p.lat, p.lon], { icon, keyboard: true, title: `${here.length} event${here.length > 1 ? "s" : ""}` }).bindPopup(() => eventPopup(here), { maxWidth: 300 });
    marker.addTo(eventLayers);
    eventMarkers.set(key, { marker, events: here });
  }
}
let eventsTab = "map", eventsItems = [], eventsNext = null, eventsRequest = 0;
const eventsCategories = new Set();
async function fetchEvents(append) {
  if (!state) return;
  const params = new URLSearchParams({ limit: "50" });
  const scope = $("events-scope").value;
  const who = scope === "player" ? state.player : scope === "selected" ? selected : null;
  if (who && state.actors[who]) { params.set("scope", "actor"); params.set("actor", who); }
  // The map only ever shows major events; the full history can include routine outcomes.
  if (eventsTab === "map") params.set("on_map", "1");
  else if ($("events-major").checked) params.set("major", "1");
  if (eventsCategories.size) params.set("category", [...eventsCategories].join(","));
  if ($("events-search").value.trim()) params.set("q", $("events-search").value.trim());
  if (append && eventsNext !== null) params.set("before_turn", eventsNext);
  // Filters change faster than responses arrive; only the latest request may render.
  const request = ++eventsRequest;
  const j = await api("events?" + params);
  if (request !== eventsRequest) return;
  eventsItems = append ? eventsItems.concat(j.events) : j.events;
  eventsNext = j.more ? j.next_before_turn : null;
  renderEvents(append ? j.events : null);
}
// Category chips are built once; renders only update which are pressed.
const eventsCategoryChips = Object.entries(EVENT_CATEGORIES).map(([id, text]) =>
  [id, button(text, "chip", protect(async () => { eventsCategories.has(id) ? eventsCategories.delete(id) : eventsCategories.add(id); await fetchEvents(false); }))]);
$("events-categories").replaceChildren(...eventsCategoryChips.map(([, b]) => b));
// appended: cards to add below the list; otherwise the whole list is redrawn.
function renderEvents(appended) {
  for (const tab of document.querySelectorAll("#events [role=tab]")) tab.setAttribute("aria-selected", tab.dataset.tab === eventsTab);
  $("events-major-label").hidden = eventsTab === "map";
  $("events-scope").options[1].textContent = `Selected country (${state?.actors?.[selected]?.name || "none"})`;
  for (const [id, b] of eventsCategoryChips) b.setAttribute("aria-pressed", eventsCategories.has(id));
  const list = $("events-list");
  if (!appended) {
    list.replaceChildren();
    if (!eventsItems.length) {
      const p = document.createElement("p");
      p.className = "empty";
      p.textContent = eventsTab === "map" ? "No major events are on the map right now." : "No events match.";
      list.append(p);
    }
  }
  list.append(...(appended || eventsItems).map(eventCard));
  $("events-more").hidden = eventsNext === null;
}
function eventCard(ev) {
  const card = document.createElement("article");
  card.className = "agreement event-card";
  card.dataset.id = ev.id;
  const head = cardHead(month(ev.turn - 1), [ev.major ? ["major", "major"] : null, [EVENT_CATEGORIES[ev.category] || ev.category, ""], ev.on_map ? ["on the map", ""] : null, ev.breaches ? ["breached a treaty", "blocked"] : null, ev.blocked_by ? ["held back by a treaty", "blocked"] : null]);
  const parties = partyChips([...new Set([ev.actor, ...ev.actors])], "events", 8);
  if (ev.regional_actor) parties.append(`Regional initiative of ${state.territories[ev.regional_actor]?.name || ev.regional_actor}`);
  const text = document.createElement("p");
  text.className = "text";
  text.textContent = ev.text;
  flagThumbs(ev.identity_changes, text, " ");
  card.append(head, parties, text);
  if (ev.effects?.length) {
    const effects = document.createElement("p");
    const arrow = { severe_decline: "⇊", decline: "↓", growth: "↑", strong_growth: "⇈" };
    for (const x of ev.effects)
      for (const [label, value] of [["economy", x.economy], ["stability", x.stability]]) {
        if (!arrow[value]) continue;
        const badge = document.createElement("span");
        badge.className = `badge effect effect-${value}`;
        badge.textContent = `${actorName(x.actor)} ${label} ${arrow[value]}`;
        effects.append(badge);
      }
    card.append(effects);
  }
  const links = document.createElement("div");
  links.className = "chips";
  chipRow(links, "Where:", ev.regions.filter((r) => state.territories[r]).slice(0, 8).map((r) => [state.territories[r].name, () => { $("events").close(); zoomToRegions([r]); }]));
  chipRow(links, "Wars:", ev.conflicts.filter((c) => state.conflicts[c]).map((c) => [state.conflicts[c].name || c, () => { $("events").close(); showOnMap("conflict", c); }]));
  chipRow(links, "Caused by:", ev.causes.map((c) => ["earlier event", protect(() => focusEvent(c, true))]));
  if (links.childElementCount) card.append(links);
  if (ev.location.length) card.append(button("Show on map", "show-on-map", () => showEventOnMap(ev)));
  return card;
}
function showEventOnMap(ev) {
  $("events").close();
  if (!map.hasLayer(eventLayers)) map.addLayer(eventLayers);
  const points = ev.location.map((p) => [p.lat, p.lon]);
  if (points.length > 1) map.fitBounds(points, { maxZoom: 6, padding: [30, 30] });
  else map.setView(points[0], Math.max(map.getZoom(), 5));
  const found = eventMarkers.get(locationKey(ev.location[0]));
  if (found?.events.some((e) => e.id === ev.id)) found.marker.openPopup();
  else L.popup({ maxWidth: 300 }).setLatLng(points[0]).setContent(eventPopup([ev])).openOn(map);
}
function resetEventFilters() {
  $("events-scope").value = "all";
  $("events-search").value = "";
  $("events-major").checked = false;
  eventsCategories.clear();
}
// Scrolls to an event, loading older pages of all events when it is not listed yet.
async function focusEvent(id, older) {
  const card = () => [...$("events-list").querySelectorAll(".event-card")].find((c) => c.dataset.id === id);
  if (!card() && older) {
    eventsTab = "all";
    resetEventFilters();
    await fetchEvents(false);
    for (let pages = 0; !card() && eventsNext !== null && pages < 50; pages++) await fetchEvents(true);
  }
  const found = card();
  if (!found) return status("That event is no longer in the saved history.");
  found.scrollIntoView({ block: "center" });
  found.classList.add("flash");
  setTimeout(() => found.classList.remove("flash"), 1500);
}
async function openEvents(id, tab = eventsTab) {
  map.closePopup();
  eventsTab = tab;
  if (id) resetEventFilters();
  await fetchEvents(false);
  if (!$("events").open) $("events").showModal();
  if (id) await focusEvent(id, true);
}
$("events-open").onclick = protect(() => openEvents());
wireDialog("events");
$("events-scope").onchange = protect(() => fetchEvents(false));
$("events-major").onchange = protect(() => fetchEvents(false));
let eventsSearchTimer;
$("events-search").oninput = () => { clearTimeout(eventsSearchTimer); eventsSearchTimer = setTimeout(protect(() => fetchEvents(false)), 250); };
$("events-more").onclick = protect(() => fetchEvents(true));
for (const tab of document.querySelectorAll("#events [role=tab]")) tab.onclick = protect(async () => { eventsTab = tab.dataset.tab; await fetchEvents(false); });
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
      await poll(job.id, true);
      sessionStorage.removeItem("pendingTurn");
      await load();
      status("Turn recovered and autosaved.");
    } finally {
      setBusy(false);
    }
  }
  } finally { setBusy(false); }
})();
