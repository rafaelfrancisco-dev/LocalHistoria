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
  busy = false,
  owners = new Set(),
  lastTurnLog = null;
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
// How a region held by force came to be held: the engine's control record, shown only while its
// holder still controls the region.
function controlText(p) {
  const c = state?.territories?.[p.id]?.control;
  if (!c || c.holder !== p.controller) return "";
  const name = (a) => state.actors[a]?.name || a;
  const since = Number.isInteger(c.since) ? ` since ${month(c.since)}` : "";
  const annexed = p.owner === c.holder;
  return `${annexed ? "Annexed" : "Held"} by ${name(c.holder)}${since}, taken from ${name(c.claimant)}` +
    (annexed ? ` · ${Math.floor(c.integration || 0)}% integrated` : "");
}
function usd(n) {
  return n >= 1e9 ? `US$${(n / 1e9).toFixed(1)} billion` : `US$${Math.round(n / 1e6).toLocaleString()} million`;
}
// What a nation's implementation allowance is paying for this month (engine-owned status, realism
// campaigns only): how much it used, work slowed for lack of funds, efforts under way and losses
// being replaced.
function implementationDetail(status) {
  const i = status.implementation;
  const parts = [`${usd(i.committed_usd)} used`];
  if (i.stalled) parts.push(`${i.stalled} underfunded`);
  const efforts = Object.keys(status.efforts || {}), orders = Object.keys(status.procurement || {});
  if (efforts.length) parts.push(efforts.join(" and ") + " under way");
  if (orders.length) parts.push("replacing " + orders.map((k) => (k === "personnel" ? "troops" : k)).join(", "));
  return parts.join(" · ");
}
// Where a region's displaced people are sheltering, from the engine's origin-host ledger. A balance
// carried from before the ledger existed has no recorded host and is said so.
function displacementText(p) {
  const d = state?.territories?.[p.id]?.displacement;
  if (!d) return "";
  const hosts = Object.entries(d.hosts || {}).sort((a, b) => b[1] - a[1]);
  const where = hosts.slice(0, 3).map(([id, n]) => `${n.toLocaleString()} in ${state.territories[id]?.name || id}`);
  if (hosts.length > 3) where.push(`${hosts.length - 3} more regions`);
  if (d.unknown) where.push(`${d.unknown.toLocaleString()} whereabouts unknown`);
  return where.length ? `Displaced: ${where.join(", ")}` : "";
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
const phone = matchMedia("(max-width: 600px)");
// With a campaign the list is grouped around the player; a search lists every match, and on a phone the list is a
// dropdown of the first eight.
function countryGroups(q) {
  const matches = countries.filter((c) => (c.name + " " + c.code).toLowerCase().includes(q));
  if (phone.matches) return [[null, document.activeElement === $("search") || q ? matches.slice(0, 8) : []]];
  if (!state || q) return [[null, matches]];
  const player = state.player;
  const engaged = new Set();
  for (const v of [...Object.values(state.conflicts || {}), ...Object.values(state.commitments || {})])
    if (v.status === "open" && v.actors.includes(player)) v.actors.forEach((a) => engaged.add(a));
  return [
    ["You", matches.filter((c) => c.code === player)],
    ["Neighbors", matches.filter((c) => c.neighbor && c.code !== player)],
    ["At war or in treaty with you", matches.filter((c) => engaged.has(c.code) && !c.neighbor && c.code !== player)],
    ["Everyone", matches.filter((c) => c.code !== player && (c.has_flag || owners.has(c.code)))],
  ];
}
function list() {
  const q = $("search").value.toLowerCase();
  $("countries").replaceChildren();
  for (const [title, entries] of countryGroups(q)) {
    if (!entries.length) continue;
    if (title) {
      const h = document.createElement("h3");
      h.className = "country-group";
      h.textContent = title;
      $("countries").append(h);
    }
    for (const c of entries) countryButton(c);
  }
}
function countryButton(c) {
  const b = document.createElement("button");
  b.className = "country" + (selected === c.code ? " selected" : "");
  b.setAttribute("aria-pressed", selected === c.code);
  b.setAttribute("aria-label", c.name);
  const img = document.createElement("img");
  img.alt = "";
  flag(img, c.code);
  // Flag, then a column with the name and the political system under it.
  const label = document.createElement("span");
  label.className = "label";
  const name = document.createElement("span");
  name.className = "name";
  name.textContent = c.name;
  label.append(name);
  if (state?.player === c.code) {
    const you = document.createElement("small");
    you.className = "you";
    you.textContent = "YOU";
    name.append(" ", you);
  }
  if (c.government) {
    const system = document.createElement("small");
    system.className = "government";
    system.textContent = c.government;
    label.append(system);
  }
  b.append(img, label);
  b.onclick = protect(async () => {
    if (phone.matches) {
      $("search").value = "";
      $("search").blur();
      list();
    }
    await select(c.code, true);
  });
  $("countries").append(b);
}
// Stability & unrest map mode. The bands mirror unrestDescription() and stabilityLabel() in C++;
// a region carries its own unrest, and everything calm falls back to its owner's stability.
const UNREST_BANDS = [[80, "in open rebellion", "#8c1d18"], [70, "in revolt", "#bb3b23"], [55, "unrest", "#d9762b"], [35, "restive", "#d8a83a"]];
const STABILITY_BANDS = [[85, "highly stable", "#2f7d5f"], [70, "very stable", "#4d8f6a"], [50, "stable", "#6d8f76"], [35, "fragile", "#8a8560"], [20, "unstable", "#8a6a4a"], [0, "collapsing", "#7d4a44"]];
const unrestBand = (u) => UNREST_BANDS.find(([floor]) => u >= floor)?.[1] || "calm";
function unrestStyle(f) {
  const p = f.properties, owner = p.owner ?? p.code, controller = p.controller || owner;
  const unrest = state?.territories?.[p.id]?.unrest || 0;
  const band = UNREST_BANDS.find(([floor]) => unrest >= floor);
  const stability = state?.actors?.[controller]?.status?.stability;
  const fillColor = band ? band[2] : typeof stability === "number" ? STABILITY_BANDS.find(([floor]) => stability >= floor)[2] : "#39464d";
  const occupied = p.controller && owner && p.controller !== owner;
  return {
    color: occupied ? state?.actors?.[p.controller]?.color || "#fff" : "#3d4e55",
    weight: occupied ? 2 : 0.5,
    dashArray: occupied ? "4 3" : null,
    fillColor,
    fillOpacity: band ? 0.85 : 0.6,
  };
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
    // A war's hostile pairs, when recorded, say who fights whom: a co-belligerent is not an enemy.
    for (const c of Object.values(state?.conflicts || {}))
      if (c.status === "open" && c.actors.includes(selected))
        (c.hostile_pairs ? c.hostile_pairs.filter((p) => p.actors.includes(selected)).flatMap((p) => p.actors) : c.actors).forEach((a) => relate(a, "enemy"));
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
  if (mapMode === "unrest" && state) return unrestStyle(f);
  const owner = f.properties.owner ?? f.properties.code;
  const isSelected = owner === selected;
  const isPlayer = state && owner === state.player;
  const fillColor = isSelected ? "#d9b875" : state?.actors?.[owner]?.color || (f.properties.playable ? "#537d83" : "#39464d");
  const fillOpacity = isSelected ? 0.9 : 0.65;
  // Countries mode: translucent fills leave anti-aliased seams along every subdivision edge, so each region is
  // painted opaque in its colour pre-blended over the map background and stroked in that same colour.
  if (mapMode === "countries" && state) {
    const flat = blend(fillColor, "#0d202b", fillOpacity);
    return { stroke: true, color: flat, weight: 1, opacity: 1, dashArray: null, fillColor: flat, fillOpacity: 1 };
  }
  return {
    color: isPlayer && !isSelected ? "#dcbb7b" : "#a0b5b2",
    weight: isSelected || isPlayer ? 2 : 0.6,
    dashArray: f.properties.controller && f.properties.controller !== owner ? "3 3" : null,
    fillColor,
    fillOpacity,
  };
}
function blend(color, background, alpha) {
  const rgb = (c) => [1, 3, 5].map((i) => parseInt(c.slice(i, i + 2), 16));
  const [a, b] = [rgb(color), rgb(background)];
  return "#" + a.map((v, i) => Math.round(v * alpha + b[i] * (1 - alpha)).toString(16).padStart(2, "0")).join("");
}
function restyleMap() {
  diplomacyCache = null;
  layers?.eachLayer((l) => l.setStyle(styleFor(l.feature)));
  renderCountryBorders();
  renderLegend();
}
// Two columns: a card left alone on its row stretches across both, so the
// grid never shows a half-empty row above a full-width card or at the end.
function fillStatRows() {
  const cells = [...$("stats").children];
  let started = false; // a card already sits in the first column of this row
  let previous = null;
  for (const cell of cells) {
    cell.classList.remove("stretch");
    if (cell.classList.contains("wide")) {
      if (started) previous.classList.add("stretch");
      started = false;
    } else {
      started = !started;
    }
    previous = cell;
  }
  if (started) previous.classList.add("stretch");
}
function renderStats() {
  $("stats").replaceChildren();
  if (!state?.actors?.[selected]) return;
  const territories = Object.values(state.territories);
  const entities = Object.values(state.entities).filter(
    (e) => e.actor === selected && e.status !== "destroyed",
  );
  const involved = (v) => v.status === "open" && v.actors.includes(selected);
  const building = entities.filter((e) => e.status === "under_construction").length;
  const national = nationalStatus[selected];
  const military = national?.military;
  const last = state.actors[selected].status || {};
  const signed = (n, text) => (n > 0 ? "+" : n < 0 ? "−" : "±") + text;
  const lastPopulation = typeof last.last_population_change === "number" ? `${signed(last.last_population_change, Math.abs(last.last_population_change).toLocaleString())} last month` : "";
  const lastGdp = typeof last.last_gdp_change === "number" ? `${signed(last.last_gdp_change, Math.abs(last.last_gdp_change).toFixed(2))}% last month` : "";
  if (national?.gdp) {
    const [local, dollars] = national.gdp.split(" (");
    const wars = national.wars.map((w) => `${w.name}${w.against?.length ? ` against ${w.against.join(", ")}` : ""}${w.ceasefire_with?.length ? `, ceasefire with ${w.ceasefire_with.join(", ")}` : ""} (${w.intensity})`).join(" · ");
    for (const [label, value, detail, wide] of [
      ["GDP" + (national.estimated ? " · estimate" : ""), local, [dollars ? dollars.slice(0, -1) : "", lastGdp].filter(Boolean).join(" · "), true],
      ["population", national.population_text, [lastPopulation, national.displaced ? `${national.displaced.toLocaleString()} displaced` : ""].filter(Boolean).join(" · ")],
      ["GDP per capita", national.gdp_per_capita, ""],
      ["stability", national.stability.replace(/ \(.*/, ""), national.stability.match(/\((.*)\)/)?.[1] || ""],
      ...(national.government ? [["government", national.government, national.government_since ? `since ${national.government_since}` : "", true]] : []),
      ...(national.unrest ? [["unrest", national.unrest.replace(/ in .*/, ""), `${national.unrest_score}/100 · ${national.unrest_region}`]] : []),
      ["economy", national.economic_trend.replace(/ \(.*/, ""), national.economic_trend.match(/\((.*)\)/)?.[1] || ""],
      ["at war", national.wars.length ? wars : "At peace", "", true],
      // On a phone the allowance folds into the construction count's tooltip, keeping the month's
      // actions near the top.
      ...(last.implementation && !phone.matches ? [["implementation", `${usd(last.implementation.allowance_usd)}/month`, implementationDetail(last)]] : []),
      ...(military ? [
        ["Active troops" + (military.estimated ? " · estimate" : ""), military.active.toLocaleString(), [`${military.uncommitted_personnel.toLocaleString()} uncommitted`, national.forces].filter(Boolean).join(" · ")],
        ["Reserve", military.reserve.toLocaleString(), "Available for mobilisation"],
        ["Warships", military.ships.toLocaleString(), `${military.uncommitted_ships.toLocaleString()} uncommitted`],
        ["Combat aircraft", military.aircraft.toLocaleString(), "National air support"],
      ] : []),
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
    ["under construction", building],
    ["conflicts", Object.values(state.conflicts).filter(involved).length],
    ["treaties", Object.values(state.commitments).filter(involved).length],
  ]) {
    const cell = document.createElement("div");
    cell.className = "stat";
    const strong = document.createElement("strong");
    strong.textContent = value;
    const span = document.createElement("span");
    span.textContent = label;
    if (label === "under construction" && last.implementation)
      cell.title = `Implementation allowance ${usd(last.implementation.allowance_usd)}/month · ${implementationDetail(last)}`;
    cell.append(strong, span);
    $("stats").append(cell);
  }
  fillStatRows();
  $("country-description").textContent =
    selected === state.player ? "Your nation" : "Another nation of this world";
  // Folded on a phone so the month's actions stay near the top.
  let politics = $("regional-politics");
  if (!politics) { politics = document.createElement("details"); politics.id = "regional-politics"; politics.open = !phone.matches; $("stats").after(politics); }
  const restive = Object.entries(state.territories).filter(([id, t]) => t.controller === selected && t.unrest >= 35);
  const merged = { ...Object.fromEntries(restive.map(([id]) => [id, state.regional_politics?.[id] || { agenda: "No standing agenda recorded", relations: "neutral", demands: [] }])), ...(state.regional_politics || {}) };
  const regions = Object.entries(merged).filter(([id]) => state.territories[id]?.controller === selected);
  politics.hidden = !regions.length;
  const heading = document.createElement("summary");
  heading.textContent = `Regional politics (${regions.length})`;
  politics.replaceChildren(heading);
  for (const [id, p] of regions) {
    const h = document.createElement("h3"); h.textContent = state.territories[id].name;
    const unrest = state.territories[id].unrest || 0;
    const agenda = document.createElement("p"); agenda.textContent = `${p.agenda} · Relations: ${p.relations}` + (unrest >= 35 ? ` · Unrest: ${unrestBand(unrest)} (${unrest.toFixed(0)}/100)` : "");
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
    countries = (await api("countries")).sort((a, b) => a.name.localeCompare(b.name));
    owners = new Set(Object.values(state.territories).map((t) => t.owner));
  }
  $("llama-endpoint").value = j.llama_endpoint;
  $("date").textContent = month(state?.turn || 0);
  $("controls").hidden = !state;
  $("playing-as").hidden = !state;
  $("diplomacy-open").hidden = !state;
  $("events-open").hidden = !state;
  $("prepare").hidden = !state;
  if (state) {
    flag($("playing-as-flag"), state.player);
    $("playing-as-name").textContent =
      countries.find((c) => c.code === state.player)?.name || state.player;
    $("player-label").textContent =
      "Playing as " + (countries.find((c) => c.code === state.player)?.name || state.player);
    renderActions();
    renderProposals();
    const expanded = new Set([...$("journal").querySelectorAll("article")].filter(e => e.querySelector("details.events")?.open).map(e => e.dataset.turn));
    $("journal").replaceChildren();
    for (const [index, entry] of j.journal.entries()) {
      const box = document.createElement("article");
      box.className = "entry";
      box.dataset.turn = String(entry.turn);
      // The month that was played, then the month it led to.
      const h = document.createElement("h3");
      h.textContent = month(entry.turn - 1);
      const played = document.createElement("small");
      played.className = "played";
      played.textContent = `played · led to ${month(entry.turn)}`;
      h.append(" ", played);
      box.append(h);
      if (entry.changes?.length) {
        const changes = document.createElement("ul");
        changes.className = "changes";
        changes.setAttribute("aria-label", "What changed this month");
        for (const c of entry.changes) {
          const item = document.createElement("li");
          item.textContent = c.summary;
          changes.append(item);
        }
        box.append(changes);
      }
      const p = document.createElement("p");
      p.textContent = entry.narration;
      const label = document.createElement("small");
      label.textContent = entry.narration_fallback
        ? "Factual event summary"
        : "AI narration · authoritative events below";
      box.append(label, p);
      const details = document.createElement("details");
      details.className = "events";
      details.open = expanded.has(String(entry.turn));
      const summary = document.createElement("summary");
      summary.textContent = "Authoritative events";
      details.append(summary);
      // The player's outcomes first, then other nations; war tolls are folded away.
      const tolls = entry.events.filter((e) => e.action === "war_toll");
      const others = entry.events.filter((e) => e.action !== "war_toll");
      const ordered = [...others.filter((e) => e.actor === state.player), ...others.filter((e) => e.actor !== state.player)];
      let tollBox = null;
      if (tolls.length) {
        tollBox = document.createElement("details");
        tollBox.className = "tolls";
        const s = document.createElement("summary");
        s.textContent = `War tolls (${tolls.length})`;
        tollBox.append(s);
      }
      for (const event of [...ordered, ...tolls]) {
        const line = document.createElement("p");
        line.textContent = (event.action?.startsWith("independent_") ? "Independent initiative · " : EVENT_PREFIX[event.action] || "") + event.text;
        const structured = entry.structured_events?.find(e => e.id === event.target);
        if (structured?.failed || structured?.partial) line.prepend(structured.failed ? "Failed · " : "Partial · ");
        if (structured?.regional_actor) line.prepend(`${state.territories[structured.regional_actor]?.name || structured.regional_actor} · `);
        flagThumbs(structured?.identity_changes, line);
        (event.action === "war_toll" ? tollBox : details).append(line);
      }
      if (tollBox) details.append(tollBox);
      box.append(details);
      if (index === 0 && lastTurnLog?.turn === entry.turn) {
        const decided = document.createElement("details");
        decided.className = "decided";
        const s = document.createElement("summary");
        s.textContent = "How the month was decided";
        decided.append(s, ...lastTurnLog.lines.map((text) => { const line = document.createElement("p"); line.textContent = text; return line; }));
        box.append(decided);
      }
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
  if (r.kind === "turn_timing") return r.status === "complete" ? "Month processing finished." : "Month processing failed.";
  if (r.status && !["success", "answered"].includes(r.status))
    return (r.actor ? actorName(r.actor) + ": " : "") + "Model attempt " + r.status.replaceAll("_", " ") + (r.error ? ": " + r.error : ".");
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
const PHASES = { chronicle: "Writing chronicles", decide: "Nations are deciding", adjudicate: "Adjudicating the month", narrate: "Writing the journal" };
let lastPhase = null;
function logLine(text) {
  const log = $("turn-log");
  const p = document.createElement("p");
  p.textContent = text;
  log.append(p);
  log.scrollTop = log.scrollHeight;
}
async function poll(id, withLog) {
  let since = 0, calls = 0, cancelling = false;
  const started = Date.now();
  const pull = async () => {
    if (!withLog) return;
    for (const r of await api("inference?job=" + encodeURIComponent(id) + "&since=" + since)) {
      since = r.id;
      calls++;
      logLine(logText(r));
      // Screen readers hear one line per phase, not every model call.
      if (PHASES[r.kind] && r.kind !== lastPhase) $("turn-phase").textContent = PHASES[r.kind];
      lastPhase = r.kind;
    }
  };
  if (withLog) {
    $("turn-log").hidden = false;
    $("turn-log").replaceChildren();
    lastPhase = null;
    lastTurnLog = null;
    // The server stops before its next model call; nothing changes unless the month already committed.
    $("cancel-turn").hidden = false;
    $("cancel-turn").disabled = false;
    $("cancel-turn").onclick = protect(async () => {
      $("cancel-turn").disabled = true;
      cancelling = true;
      status("Cancelling after the current model call…");
      sessionStorage.removeItem("pendingTurn");
      await api("turns/cancel", { job: String(id) });
    });
  }
  try {
    // The server's month budget is at most 30 minutes.
    for (let i = 0; i < 4000; i++) {
      await pull();
      const j = await api("jobs?id=" + encodeURIComponent(id));
      if (j.status === "complete") {
        await pull();
        if (withLog) {
          // Kept in the newest journal entry once the month is loaded.
          lastTurnLog = { turn: (state?.turn ?? 0) + 1, lines: [...$("turn-log").children].map((p) => p.textContent) };
          $("turn-log").hidden = true;
        }
        return j;
      }
      if (j.status !== "running") {
        if (withLog) $("turn-log").hidden = true;
        throw Error(j.error || j.status);
      }
      const seconds = Math.round((Date.now() - started) / 1000);
      if (!withLog) status("Working… " + id + " · the game remains responsive");
      else if (!cancelling)
        status(`${PHASES[lastPhase] || "Preparing the month"} · ${calls} model call${calls === 1 ? "" : "s"} · ${seconds} s`);
      await new Promise((r) => setTimeout(r, 500));
    }
    throw Error("Job still running. Reload to resume monitoring.");
  } finally {
    if (withLog) $("cancel-turn").hidden = true;
  }
}
$("search").oninput = list;
$("search").onfocus = () => { if (phone.matches) list(); };
$("search").onblur = () => { if (phone.matches) setTimeout(list, 200); };
phone.addEventListener("change", list);
$("save-endpoint").onclick = protect(async () => {
  await api("model-config", { llama_endpoint: $("llama-endpoint").value });
  status("Model endpoint updated.");
});
function readSkipNarration() {
  try { return localStorage.getItem("skipNarration") === "1"; } catch { return false; }
}
function renderSettings() {
  $("skip-narration").checked = readSkipNarration();
  let job = null;
  try { job = localStorage.getItem("lastJob"); } catch {}
  $("last-inference").hidden = !job;
  if (job) $("last-inference").href = "/api/v1/inference?job=" + encodeURIComponent(job);
}
$("settings-open").onclick = () => { renderSettings(); $("settings").showModal(); };
wireDialog("settings");
$("skip-narration").onchange = () => {
  try { localStorage.setItem("skipNarration", $("skip-narration").checked ? "1" : "0"); } catch {}
};
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
  return Object.entries(state?.proposals || {}).filter(([, p]) => p.status === "pending" && p.actors.includes(state.player) && !p.answers?.[state.player]);
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
    offered.textContent = `Offered by ${actorName(p.from)} · revision ${p.revision || 1}` + (others.length ? `, with ${others.join(", ")}` : "");
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
    const responses = Object.entries(readAnswers()).filter(([id]) => offered.has(id)).map(([proposal, accept]) => ({ proposal, accept, revision: state.proposals[proposal].revision || 1 }));
    if (responses.length) pending.responses = responses;
    if (readSkipNarration()) pending.narration = false;
    sessionStorage.setItem("pendingTurn", JSON.stringify(pending));
    const job = await api("turns", pending);
    try { localStorage.setItem("lastJob", job.id); } catch {}
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
let entityRequest = 0;
async function refreshEntities() {
  if (!state) return;
  const key=monthKey(), cities=map.getZoom()>=4, view=map.getBounds();
  if (entityFetch && entityFetch.key===key && entityFetch.cities===cities && !entityFetch.full && entityFetch.bounds.contains(view)) return;
  const request = ++entityRequest, b=view.pad(0.5);
  const entities=await api(`entities?west=${b.getWest()}&east=${b.getEast()}&south=${b.getSouth()}&north=${b.getNorth()}&zoom=${map.getZoom()}`);
  if (request !== entityRequest) return;
  entityFetch={key,cities,bounds:b,full:entities.length>=200};
  entityLayers.clearLayers();
  for (const e of entities) {
    const label=document.createElement("div");
    const status=e.status==="under_construction"?(e.paused?`construction halted: ${e.paused.remaining_months} month${e.paused.remaining_months===1?"":"s"} of work left, awaiting control of the region`:`under construction until ${month(e.completes_turn-1)}`):e.kind==="event_site"&&e.expires_turn!==undefined?`fades at the end of ${month(e.expires_turn-1)}`:e.status;
    label.textContent=`${e.name}${e.kind === "army" ? ` · ${(e.personnel ?? 10000).toLocaleString()} troops` : e.kind === "fleet" ? ` · ${(e.ships ?? 3).toLocaleString()} ships` : ""} · ${kindLabel(e.kind)} · ${actorName(e.actor)} · ${status} · ${state.territories[e.region]?.name||e.region}`;
    L.circleMarker([e.latitude,e.longitude],{radius:5,color:state.actors[e.actor]?.color||"#fff",dashArray:e.status==="under_construction"?"2 3":null}).bindPopup(label).addTo(entityLayers);
  }
}
// Pans are debounced, and a view inside the last (padded) fetch of the same month needs no request.
let moveTimer = null, entityFetch = null;
map.on("moveend", () => { clearTimeout(moveTimer); moveTimer = setTimeout(protect(refreshEntities), 300); });
const monthKey = () => state && `${state.campaign_id}:${state.turn}`;
// Region geometry is fetched and turned into layers once; later months only replace properties and restyle.
let regionLayer = null, regionTopology = null, regionById = new Map(), eventMarkerKey = null;
function regionLabel(p) {
  if (mapMode === "countries") {
    const owner = state.actors[p.owner]?.name || p.owner || "Unresolved territory";
    return p.controller && p.owner && p.controller !== p.owner ? `${owner} · ${p.name} occupied by ${state.actors[p.controller]?.name || p.controller}` : owner;
  }
  return `${p.name} · ${p.id} · owner ${p.owner || "unresolved"} · controller ${p.controller || "unresolved"}`;
}
async function refreshCampaignMap() {
  const snapshot=await api("map");
  if (!regionLayer) {
    regionTopology=await (await fetch(snapshot.geometry_url)).json();
    const geo=topojson.feature(regionTopology,regionTopology.objects.regions);
    for (const f of geo.features) f.properties={...snapshot.territories[f.id],id:f.id};
    regionLayer=L.geoJSON(geo,{style:styleFor,onEachFeature:(f,l)=>{
      // The layer holds its own LatLngs; only properties are read from the feature from now on.
      regionById.set(f.id,l);f.geometry=null;
      l.bindTooltip(()=>{
        const p=l.feature.properties,box=document.createElement("span");
        if (mapMode==="unrest") {
          const unrest=state?.territories?.[p.id]?.unrest||0, controller=p.controller||p.owner;
          const stability=state?.actors?.[controller]?.status?.stability;
          const lines=[p.name+(p.population?` · ${p.population.toLocaleString()} people`:"")];
          lines.push(`Unrest: ${unrestBand(unrest)}${unrest?` (${unrest.toFixed(0)}/100)`:""}`);
          const held=controlText(p);
          if (held) lines.push(held);
          if (typeof stability==="number") lines.push(`${state.actors[controller]?.name||controller}: ${STABILITY_BANDS.find(([floor])=>stability>=floor)[1]} (${stability.toFixed(0)}/100)`);
          box.style.whiteSpace="pre-line";box.textContent=lines.join("\n");return box;
        }
        if (mapMode!=="diplomacy") { box.textContent=regionLabel(p); return box; }
        const lines=[p.name+(p.population?` · ${p.population.toLocaleString()} people`:"")+(p.displaced?` · ${p.displaced.toLocaleString()} displaced`:"")];
        if (p.controller&&p.owner&&p.controller!==p.owner) lines.push(`Occupied by ${state.actors[p.controller]?.name||p.controller}`);
        const held=controlText(p);
        if (held) lines.push(held);
        const away=displacementText(p);
        if (away) lines.push(away);
        for (const c of Object.values(state.conflicts)) if (c.status==="open"&&(c.affected_regions||[]).includes(p.id)) lines.push(`Front: ${c.name} (${c.intensity})`);
        const treaties=Object.values(state.commitments).filter(c=>c.status==="open"&&c.actors.includes(p.owner)).map(c=>c.name);
        if (treaties.length) lines.push("Treaties: "+treaties.slice(0,4).join(", ")+(treaties.length>4?` +${treaties.length-4}`:""));
        box.style.whiteSpace="pre-line";box.textContent=lines.join("\n");return box;
      });
      l.on("click",protect(async()=>{const p=l.feature.properties,added=state&&!$("controls").hidden&&addTarget(p.id);if(p.controller || p.owner) await select(p.controller || p.owner);status(added?`Added ${p.name} to action ${activeAction+1}`:regionLabel(p));}));
    }});
  } else for (const [id,l] of regionById) l.feature.properties={...snapshot.territories[id],id};
  if (layers!==regionLayer) {
    if (layers) map.removeLayer(layers);
    layers=regionLayer.addTo(map);
  }
  restyleMap();
  await Promise.all([refreshEntities(), refreshEventMarkers()]);
}
// Countries mode draws no subdivisions: borders are meshed from the shared region arcs wherever the owner changes.
let countryBorders = null, countryBordersKey = null;
function renderCountryBorders() {
  const show = mapMode === "countries" && state && regionTopology && layers === regionLayer;
  if (!show) { if (countryBorders) map.removeLayer(countryBorders); return; }
  const key = `${monthKey()}:${state.player}`;
  if (key !== countryBordersKey) {
    if (countryBorders) map.removeLayer(countryBorders);
    const obj = regionTopology.objects.regions, prop = (g) => regionById.get(g.id).feature.properties;
    const owner = (g) => prop(g).owner || "", controller = (g) => prop(g).controller || owner(g);
    const border = (a, b) => a === b || owner(a) !== owner(b);
    const player = (a, b) => owner(a) === state.player || owner(b) === state.player;
    const line = (filter, style) => L.geoJSON(topojson.mesh(regionTopology, obj, filter), { interactive: false, style: () => style });
    const occupiers = new Set([...regionById.values()]
      .map(l => l.feature.properties)
      .filter(p => p.controller && p.controller !== p.owner)
      .map(p => p.controller));
    countryBorders = L.layerGroup([
      line((a, b) => border(a, b) && !player(a, b), { color: "#1f2a2f", weight: 1.2, opacity: 0.9 }),
      line((a, b) => border(a, b) && player(a, b), { color: "#dcbb7b", weight: 2 }),
      // Draw the occupied area's perimeter last, including coasts and national borders.
      ...[...occupiers].map(occupier => {
        const held = g => controller(g) === occupier && controller(g) !== owner(g);
        return line((a, b) => a === b ? held(a) : held(a) !== held(b), { color: state.actors[occupier]?.color || "#fff", weight: 2.5, dashArray: "4 3", opacity: 1 });
      }),
    ]);
    countryBordersKey = key;
  }
  countryBorders.addTo(map);
}
// Map mode: political ownership with regions, countries without subdivisions, or wars, fronts, occupations and treaties.
function readPreference(key, fallback) {
  try { return localStorage.getItem(key) || fallback; } catch { return fallback; }
}
const MAP_MODES = ["political", "countries", "diplomacy", "unrest"];
function setMapMode(mode) {
  mapMode = MAP_MODES.includes(mode) ? mode : "political";
  try { localStorage.setItem("mapMode", mapMode); } catch {}
  if (mapMode !== "diplomacy") highlight = null;
  for (const b of modeControl.getContainer()?.querySelectorAll("button[data-mode]") || []) b.setAttribute("aria-pressed", b.dataset.mode === mapMode);
  restyleMap();
}
const modeControl = L.control({ position: "topleft" });
modeControl.onAdd = () => {
  const box = L.DomUtil.create("div", "map-modes leaflet-bar");
  for (const [mode, text] of [["political", "Political"], ["countries", "Countries"], ["diplomacy", "Wars & treaties"], ["unrest", "Stability & unrest"]]) {
    const b = L.DomUtil.create("button", "", box);
    b.type = "button";
    b.dataset.mode = mode;
    b.textContent = text;
    b.setAttribute("aria-pressed", mode === mapMode);
    L.DomEvent.on(b, "click", (e) => { L.DomEvent.stop(e); setMapMode(mode); });
  }
  // Overlay toggles: unit and construction markers, and event markers.
  for (const [name, text] of [["entities", "Entities"], ["events", "Events"]]) {
    const b = L.DomUtil.create("button", "layer-toggle", box);
    b.type = "button";
    b.dataset.layer = name;
    b.textContent = text;
    b.setAttribute("aria-pressed", "true");
    L.DomEvent.on(b, "click", (e) => { L.DomEvent.stop(e); const layer = overlay(name); setLayerVisible(layer, !map.hasLayer(layer)); });
  }
  L.DomEvent.disableClickPropagation(box);
  return box;
};
modeControl.addTo(map);
const overlay = (name) => (name === "entities" ? entityLayers : eventLayers);
function setLayerVisible(layer, on) {
  if (on) map.addLayer(layer); else map.removeLayer(layer);
  const name = layer === entityLayers ? "entities" : "events";
  modeControl.getContainer().querySelector(`[data-layer=${name}]`).setAttribute("aria-pressed", String(on));
}
const legend = L.control({ position: "bottomleft" });
legend.onAdd = () => L.DomUtil.create("div", "map-legend");
legend.addTo(map);
function renderLegend() {
  const box = legend.getContainer();
  if (!box) return;
  box.hidden = (mapMode !== "diplomacy" && mapMode !== "unrest") || !state;
  box.replaceChildren();
  if (box.hidden) return;
  if (mapMode === "unrest") {
    const heading = document.createElement("strong");
    heading.textContent = "Stability & unrest";
    box.append(heading);
    const rows = [...UNREST_BANDS.map(([, text, color]) => [color, `Region ${text}`]), ...STABILITY_BANDS.map(([, text, color]) => [color, `Nation ${text}`]), [null, "Dashed border in the occupier’s colour: occupied"]];
    for (const [color, text] of rows) {
      const row = document.createElement("div");
      const swatch = document.createElement("i");
      if (color) swatch.style.background = color; else swatch.className = "dashed";
      row.append(swatch, document.createTextNode(text));
      box.append(row);
    }
    return;
  }
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
setMapMode(readPreference("mapMode", "political"));

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
  if (t.action === "grant_access") return `Military access for ${actorName(t.to)} on the other parties' land`;
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
    const sides = kind === "conflict" && v.status === "open" ? pairSummary(v) : "";
    if (sides) {
      const p = document.createElement("p");
      p.className = "meta";
      p.textContent = sides;
      card.append(p);
    }
    if (v.terms?.length) card.append(termList(v.terms));
    const regions = document.createElement("div");
    regions.className = "chips";
    chipRow(regions, "Front:", (v.affected_regions || []).map((r) => [state.territories[r]?.name || r, () => { highlight = { kind, id }; $("diplomacy").close(); setMapMode("diplomacy"); zoomToRegions([r]); }]));
    if (regions.childElementCount) card.append(regions);
    card.append(button("Show on map", "show-on-map", () => showOnMap(kind, id)));
    list.append(card);
  }
}
// Who fights whom in a war with more than two parties or a partial ceasefire: allies on one side are
// not enemies, and a ceasefire or peace between two parties pauses only their pair.
const PAUSING_KINDS = new Set(["ceasefire", "peace_treaty", "non_aggression_pact"]);
function pairSummary(c) {
  if (!c.hostile_pairs?.length) return "";
  const pauses = Object.values(state.commitments || {}).filter((k) => k.status === "open" && PAUSING_KINDS.has(k.kind));
  const label = (p) => p.actors.map(actorName).join("–");
  const fighting = [], truce = [];
  for (const p of c.hostile_pairs) {
    if (p.status !== "active") continue;
    (pauses.some((k) => p.actors.every((a) => k.actors.includes(a))) ? truce : fighting).push(label(p));
  }
  if (c.actors.length <= 2 && !truce.length) return "";
  return [fighting.length ? `Fighting: ${fighting.join(", ")}` : "", truce.length ? `Ceasefire: ${truce.join(", ")}` : ""].filter(Boolean).join(" · ");
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
const EVENT_PREFIX = { war_toll: "War toll · ", front_advance: "Front · ", unrest: "Unrest · ", revolt: "Rebellion · ", secession: "Independence · ", regime_change: "Government · " };
const EVENT_CATEGORIES = { player: "Your actions", nation: "Other nations", regional: "Regional", domestic: "Unrest & rebellion", world: "World developments", war: "War tolls", construction: "Construction" };
const eventLayers = L.layerGroup().addTo(map);
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
  if (!state || eventMarkerKey === monthKey()) return;
  const request = ++eventMarkerRequest, key = monthKey();
  const { events } = await api("events?on_map=1");
  if (request !== eventMarkerRequest) return;
  eventMarkerKey = key;
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
  const head = cardHead(month(ev.turn - 1), [ev.major ? ["major", "major"] : null, [EVENT_CATEGORIES[ev.category] || ev.category, ""], ev.on_map ? ["on the map", ""] : null, ev.breaches ? ["breached a treaty", "blocked"] : null, ev.blocked_by ? ["held back by a treaty", "blocked"] : null, ev.failed ? ["did not happen", "blocked"] : null, ev.partial ? ["partly happened", "blocked"] : null]);
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
  setLayerVisible(eventLayers, true);
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
const prepare=$("prepare");
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
