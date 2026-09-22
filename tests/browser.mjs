import { chromium } from "@playwright/test";
import assert from "node:assert/strict";
import fs from "node:fs";
const base = process.env.LOCALHISTORIA_TEST_URL;
if (!base || !base.startsWith("http://127.0.0.1:"))
  throw Error("Set LOCALHISTORIA_TEST_URL to a disposable test server");
const initial = await (await fetch(base + "/api/v1/campaign")).json();
assert.equal(
  initial.state,
  null,
  "Browser test requires an empty disposable save",
);
// Precise, collectable heap readings: the size check measures what the page retains, not pending garbage.
const browser = await chromium.launch({ headless: true, args: ["--js-flags=--expose-gc", "--enable-precise-memory-info"] });
try {
  const page = await browser.newPage({
    viewport: { width: 1440, height: 1050 },
  });
  const errors = [],
    external = [];
  page.on("pageerror", (e) => {
    errors.push(e.message);
    console.error("PAGE ERROR", e.message);
  });
  page.on("console", (m) => {
    if (m.type() === "error") console.error("CONSOLE", m.text());
  });
  page.on("response", (r) => {
    if (r.status() >= 400) console.error("HTTP", r.status(), r.url());
  });
  await page.route("**/*", (route) => {
    if (
      !route
        .request()
        .url()
        .startsWith(base + "/")
    ) {
      external.push(route.request().url());
      return route.abort();
    }
    return route.continue();
  });
  await page.route("**/flags/fr.svg", (r) =>
    r.fulfill({ status: 404, body: "" }),
  );
  await page.goto(base);
  await page.waitForSelector("#countries button");
  await page.locator("#search").fill("Portugal");
  await page.getByRole("button", { name: "Portugal", exact: true }).click();
  await page.waitForFunction(
    () => document.querySelector("#country-name").textContent === "Portugal",
  );
  page.on("dialog", () => {
    throw Error("Unexpected replacement confirmation");
  });
  await page.locator("#new-game").click();
  await page.locator("#controls").waitFor({ state: "visible" });
  // National status in real currency, the Wars & treaties menu and its map mode.
  await page.waitForFunction(() => document.querySelector("#stats").innerText.includes("€"));
  assert.match(await page.locator("#stats").innerText(), /GDP[\s\S]*US\$[\s\S]*population[\s\S]*stability/i);
  // The political system, next to stability, and in the country list.
  assert.match(await page.locator("#stats").innerText(), /Semi-presidential republic[\s\S]*government · since 1976/i);
  assert.match(await page.getByRole("button", { name: "Portugal", exact: true }).innerText(), /Portugal[\s\S]*Semi-presidential republic/);
  assert.match(await page.locator("#stats").innerText(), /28,000[\s\S]*Active troops[\s\S]*uncommitted[\s\S]*210,000[\s\S]*Reserve[\s\S]*Warships[\s\S]*Combat aircraft/);
  await page.evaluate(() => { const e = Object.values(state.entities).find(e => e.kind === "army" && e.actor === "UA"); map.setView([e.latitude, e.longitude], 7); });
  await page.waitForFunction(() => entityLayers.getLayers().some(l => l.getPopup()?.getContent()?.textContent.includes("90,000 troops")));
  await page.evaluate(() => { entityLayers.getLayers().find(l => l.getPopup()?.getContent()?.textContent.includes("90,000 troops")).openPopup(); });
  assert.match(await page.locator(".leaflet-popup-content").innerText(), /Ukrainian .* Army · 90,000 troops/);
  await page.evaluate(() => { map.closePopup(); });
  await page.locator("#diplomacy-open").click();
  await page.locator("#diplomacy").waitFor({ state: "visible" });
  assert.match(await page.locator("#diplomacy-list").innerText(), /Russo-Ukrainian War[\s\S]*Since February 2022/);
  await page.getByRole("tab", { name: "Treaties & alliances" }).click();
  assert.match(await page.locator("#diplomacy-list").innerText(), /North Atlantic Treaty Organization/);
  await page.locator("#diplomacy-mine").check();
  assert.doesNotMatch(await page.locator("#diplomacy-list").innerText(), /Collective Security Treaty Organization/);
  await page.getByRole("tab", { name: "Active wars" }).click();
  await page.locator("#diplomacy-mine").uncheck();
  await page.locator("#diplomacy-list .agreement", { hasText: "Russo-Ukrainian War" }).locator(".show-on-map").click();
  await page.waitForFunction(() => mapMode === "diplomacy" && !document.querySelector("#diplomacy").open);
  assert.match(await page.locator(".map-legend").innerText(), /Russo-Ukrainian War[\s\S]*War front/);
  assert.equal(await page.evaluate(() => { let fill; layers.eachLayer((l) => { if (l.feature.properties.name === "Donetsk Oblast") fill = l.options.fillColor; }); return fill; }), "#f28c38");
  await page.locator(".map-legend button").click();
  // Stability & unrest: a calm region takes its controller's stability band, and the legend names
  // both scales.
  await page.getByRole("button", { name: "Stability & unrest" }).click();
  await page.waitForFunction(() => mapMode === "unrest");
  assert.match(await page.locator(".map-legend").innerText(), /Region in open rebellion[\s\S]*Nation collapsing/);
  // Occupied Donetsk is calm enough to show its controller's band, which is Russia's.
  assert.equal(await page.evaluate(() => { let fill; layers.eachLayer((l) => { if (l.feature.properties.name === "Donetsk Oblast") fill = l.options.fillColor; }); return fill; }), "#6d8f76");
  await page.getByRole("button", { name: "Political" }).click();
  await page.waitForFunction(() => mapMode === "political" && document.querySelector(".map-legend").hidden);
  // Display geometry is small enough to keep the page light and restyles fast (Chromium reports heap size).
  const heap = await page.evaluate(() => { window.gc?.(); return performance.memory?.usedJSHeapSize; });
  console.log("JS heap after campaign load:", heap ? `${Math.round(heap / 1e6)} MB` : "unavailable");
  if (heap !== undefined) assert.ok(heap < 300e6, `JS heap ${heap}`);
  const switchMs = await page.evaluate(() => { const t = performance.now(); setMapMode("diplomacy"); const d = performance.now() - t; setMapMode("political"); return d; });
  console.log("Map mode switch:", Math.round(switchMs), "ms");
  assert.ok(switchMs < 150, `map mode switch took ${switchMs} ms`);
  // Countries mode: regions painted seamlessly in their own colour, national borders meshed from current ownership.
  await page.getByRole("button", { name: "Countries" }).click();
  assert.deepEqual(await page.evaluate(() => {
    let seams = 0, portugal;
    layers.eachLayer((l) => { if (l.options.color !== l.options.fillColor || l.options.fillOpacity !== 1) seams++; if (l.feature.properties.owner === "PT") portugal = l.options.fillColor; });
    const lines = countryBorders.getLayers().reduce((n, g) => n + g.getLayers().length, 0);
    return { mode: mapMode, seams, portugal, shown: map.hasLayer(countryBorders), lines: lines > 0 };
  }), { mode: "countries", seams: 0, portugal: "#c5a96e", shown: true, lines: true });
  await page.getByRole("button", { name: "Political" }).click();
  assert.equal(await page.evaluate(() => map.hasLayer(countryBorders)), false);
  // Overlay toggles in the map-mode bar replace Leaflet's layer chooser.
  assert.equal(await page.locator(".leaflet-control-layers").count(), 0);
  await page.locator(".map-modes [data-layer=entities]").click();
  assert.deepEqual(await page.evaluate(() => [map.hasLayer(entityLayers), document.querySelector(".map-modes [data-layer=entities]").getAttribute("aria-pressed")]), [false, "false"]);
  await page.locator(".map-modes [data-layer=entities]").click();
  assert.equal(await page.evaluate(() => map.hasLayer(entityLayers)), true);
  // Settings: the model endpoint lives in a dialog; re-saving keeps the mock endpoint for later months.
  await page.locator("#settings-open").click();
  await page.locator("#settings").waitFor({ state: "visible" });
  const endpoint = await page.locator("#llama-endpoint").inputValue();
  await page.locator("#llama-endpoint").fill(endpoint);
  await page.locator("#save-endpoint").click();
  await page.waitForFunction(() => document.querySelector("#status").textContent === "Model endpoint updated.");
  assert.equal((await (await fetch(base + "/api/v1/campaign")).json()).llama_endpoint, endpoint);
  await page.locator("#settings-close").click();
  await page.locator("#action-0").fill("Invest in the national railway network");
  await page.locator("#add-action").click();
  await page.locator("#action-1").fill("Open trade talks with Spain");
  assert.equal(await page.locator(".action-card").count(), 2);
  await page.reload();
  await page.waitForFunction(
    () => document.querySelector("#action-1")?.value === "Open trade talks with Spain",
  );
  await page.waitForFunction(() => layers?.getLayers().length > 3000);
  await page.evaluate(() => { window.firstRegionLayer = layers; });
  await page.locator("#turn").click();
  await page.waitForFunction(
    () => document.querySelector("#date").textContent === "February 2026" && !busy,
  );
  // A new month updates the existing region layer in place instead of rebuilding it.
  assert.equal(await page.evaluate(() => layers === window.firstRegionLayer), true);
  await page
    .locator("#journal details.events")
    .first()
    .evaluate((d) => (d.open = true));
  assert.equal(await page.locator("#journal article").first().locator("details.decided summary").innerText(), "How the month was decided");
  assert.match(await page.locator("#journal h3").first().innerText(), /^January 2026 played · led to February 2026$/);
  assert.match(
    await page.locator("#journal").innerText(),
    /Invest in the national railway network/,
  );
  assert.equal(await page.locator("#action-0").inputValue(), "");
  await page.locator("#turn").click();
  await page.waitForFunction(
    () => document.querySelector("#date").textContent === "March 2026" && !busy,
  );
  await page.reload();
  try {
    await page.waitForFunction(
      () => document.querySelector("#date").textContent === "March 2026" && !busy,
    );
  } catch (e) {
    console.error("STATUS", await page.locator("#status").innerText());
    console.error("ERRORS", errors);
    throw e;
  }
  // Events menu: the player's events by default, category filters, and markers on the map.
  await page.locator("#events-open").click();
  await page.locator("#events").waitFor({ state: "visible" });
  await page.waitForFunction(() => document.querySelectorAll("#events-list .event-card").length > 0);
  assert.match(await page.locator("#events-list").innerText(), /Invest in the national railway network[\s\S]*/);
  assert.ok((await page.locator("#events-list .event-card", { hasText: "on the map" }).count()) > 0);
  assert.match(await page.locator("#events-list").innerText(), /major/i);
  assert.doesNotMatch(await page.locator("#events-list").innerText(), /Open trade talks with Spain/);
  await page.getByRole("tab", { name: "All events" }).click();
  await page.waitForFunction(() => document.querySelector("#events [data-tab=all]").getAttribute("aria-selected") === "true" && !document.querySelector("#events-major-label").hidden);
  await page.locator("#events-major").uncheck();
  await page.waitForFunction(() => document.querySelector("#events-list").innerText.includes("Open trade talks with Spain"));
  const playerCards = await page.locator("#events-list .event-card").count();
  await page.locator("#events-scope").selectOption("all");
  await page.waitForFunction((n) => document.querySelectorAll("#events-list .event-card").length >= n, playerCards);
  await page.locator("#events-categories .chip", { hasText: "Other nations" }).click();
  await page.waitForFunction(() => document.querySelector("#events-categories .chip[aria-pressed=true]") && !document.querySelector("#events-list").innerText.includes("railway"));
  assert.ok((await page.locator("#events-list .event-card").count()) > 0);
  await page.locator("#events-categories .chip", { hasText: "Other nations" }).click();
  await page.locator("#events-search").fill("railway");
  await page.waitForFunction(() => document.querySelectorAll("#events-list .event-card").length === 1);
  await page.locator("#events-list .event-card").first().locator(".show-on-map").click();
  await page.waitForFunction(() => !document.querySelector("#events").open && eventLayers.getLayers().length > 0 && document.querySelector(".event-popup"));
  assert.match(await page.locator(".event-popup").first().innerText(), /railway/);
  await page.locator(".event-popup button").first().click();
  await page.waitForFunction(() => document.querySelector("#events").open && document.querySelector("#events-list .event-card.flash"));
  await page.locator("#events-close").click();
  await page.locator("#search").fill("Spain");
  await page.getByRole("button", { name: "Spain", exact: true }).click();
  await page.waitForFunction(
    () => document.querySelector("#country-name").textContent === "Spain",
  );
  assert.ok((await page.locator("canvas.leaflet-zoom-animated").count()) > 0);
  await page.waitForTimeout(500);
  const point = await page.evaluate(() => {
    const p = map.latLngToContainerPoint([39.5, -8]);
    const r = document.getElementById("map").getBoundingClientRect();
    return { x: r.left + p.x, y: r.top + p.y };
  });
  await page.mouse.click(point.x, point.y);
  await page.waitForFunction(
    () => document.querySelector("#country-name").textContent === "Portugal",
  );
  const oldZoom = await page.evaluate(() => map.getZoom());
  await page.locator(".leaflet-control-zoom-in").click();
  await page.waitForTimeout(500);
  assert.ok((await page.evaluate(() => map.getZoom())) > oldZoom);

  await page.locator("#search").fill("France");
  await page.getByRole("button", { name: "France", exact: true }).click();
  await page.waitForFunction(
    () => document.querySelector("#country-name").textContent === "France",
  );
  await page.waitForFunction(() => document.querySelector("#flag").hidden);
  await page.locator("#search").fill("Antarctica");
  await page.getByRole("button", { name: "Antarctica", exact: true }).click();
  await page.waitForFunction(
    () => document.querySelector("#country-name").textContent === "Antarctica",
  );
  assert.equal(await page.locator("#cities").innerText(), "");
  await page.locator("#search").fill("Portugal");
  await page.getByRole("button", { name: "Portugal", exact: true }).click();
  await page.waitForFunction(
    () => document.querySelector("#country-name").textContent === "Portugal",
  );
  await page.waitForTimeout(500);

  const rid = await page.evaluate(() => Object.values(state.entities).find(e=>e.actor==="PT").region);
  await page.locator("#action-0").fill("Cede the region to Spain after building a port: transfer-and-build-fixture");
  await page.evaluate((id) => { let target; layers.eachLayer((l) => { if (l.feature.id === id) target = l; }); target.fire("click"); }, rid);
  // The earlier map click on Portugal already pinned a region to the action; remove it with its chip.
  await page.waitForFunction(() => document.querySelectorAll(".action-card .chip").length === 2);
  await page.locator(".action-card .chip").first().click();
  await page.waitForFunction(() => document.querySelectorAll(".action-card .chip").length === 1);
  await page.locator("#turn").click();
  await page.waitForFunction(()=>document.querySelector("#date").textContent==="April 2026" && !busy);
  await page.waitForFunction(id=>state.territories[id].owner==="ES",rid);
  const changed = await page.locator("#journal .changes").first().innerText();
  assert.match(changed, /Fixture port \(port\) construction started/);
  assert.match(changed, /: Portugal → Spain/);
  assert.equal(await page.evaluate(id=>{let owner;layers.eachLayer(l=>{if(l.feature.id===id)owner=l.feature.properties.owner;});return owner;},rid),"ES");
  await page.evaluate(()=>{const e=state.entities["e3-fixture-port"];map.setView([e.latitude,e.longitude],7);});
  await page.waitForFunction(()=>entityLayers.getLayers().some(l=>l.getPopup()?.getContent()?.textContent.includes("Fixture port")));
  await page.evaluate(()=>{entityLayers.getLayers().find(l=>l.getPopup()?.getContent()?.textContent.includes("Fixture port")).openPopup();});
  assert.match(await page.locator(".leaflet-popup-content").innerText(),/Fixture port · port · Portugal · under construction until/);
  await page.reload();
  await page.waitForFunction(id=>state?.territories[id]?.owner==="ES"&&state.entities["e3-fixture-port"],rid);
  assert.equal(await page.evaluate(()=>state.entities["e3-fixture-port"].origin_event),"t3-fixture");

  const regional = await page.evaluate(() => Object.keys(state.regional_politics)[0]);
  assert.ok(regional);
  await page.evaluate(async id => { await select(state.territories[id].controller); }, regional);
  assert.match(await page.locator("#regional-politics").innerText(), /pending: Request a regional assembly/);
  await page.locator("#action-0").fill("Adopt the Atlantic Republic constitution: identity-fixture");
  await page.locator("#turn").click();
  await page.waitForFunction(() => state?.actors?.PT?.flag_revision === 1 && !busy);
  await page.locator("#search").fill("Atlantic Republic");
  await page.getByRole("button", { name: "Atlantic Republic", exact: true }).click();
  await page.waitForFunction(() => document.querySelector("#country-name").textContent === "Atlantic Republic");
  await page.waitForFunction(() => document.querySelector("#playing-as-name").textContent === "Atlantic Republic");
  const firstFlag = await page.locator("#flag").getAttribute("src");
  assert.match(firstFlag, /revision=1/);
  await page.waitForFunction(() => document.querySelector("#flag").naturalWidth > 0);
  const firstSvg = await (await fetch(base + firstFlag)).text();
  await page.locator("#action-0").fill("Explicitly refound the constitution: refound identity-fixture");
  await page.locator("#turn").click();
  await page.waitForFunction(() => state?.actors?.PT?.flag_revision === 2 && !busy);
  await page.waitForFunction(() => document.querySelector("#flag").getAttribute("src").includes("revision=2"));
  const secondFlag = await page.locator("#flag").getAttribute("src");
  assert.match(secondFlag, /revision=2/);
  assert.notEqual(await (await fetch(base + secondFlag)).text(), firstSvg);
  assert.equal(await (await fetch(base + firstFlag)).text(), firstSvg);
  assert.ok(await page.locator('#journal img[src*="revision=1"]').count());
  await page.reload();
  await page.waitForFunction(() => state?.actors?.PT?.flag_revision === 2 && !busy);
  await page.waitForFunction(() => document.querySelector("#playing-as-name").textContent === "Atlantic Republic");

  // An event whose every operation was dropped is marked as not having happened.
  const before = await page.evaluate(() => state.turn);
  await page.locator("#action-0").fill("Strike deep: unreachable-fixture");
  await page.locator("#turn").click();
  await page.waitForFunction(t => state?.turn === t + 1 && !busy, before);
  await page.waitForFunction(() => [...document.querySelectorAll("#journal p")].some(p => p.textContent.startsWith("Failed · Strike deep")));
  await page.locator("#events-open").click();
  await page.locator("#events").waitFor({ state: "visible" });
  // A failed event changed nothing, so it is not major.
  await page.getByRole("tab", { name: "All events" }).click();
  await page.waitForFunction(() => !document.querySelector("#events-major-label").hidden);
  await page.locator("#events-major").uncheck();
  await page.locator("#events-search").fill("Strike deep");
  await page.waitForFunction(() => [...document.querySelectorAll("#events-list .event-card")].some(c => c.textContent.includes("Strike deep") && c.textContent.includes("did not happen")));

  await page.locator("#events-close").click();
  fs.mkdirSync("tests/artifacts", { recursive: true });
  await page.screenshot({
    path: "tests/artifacts/browser.png",
    fullPage: true,
  });
  // Phone width: one header row, and the month's actions come before the journal.
  await page.setViewportSize({ width: 390, height: 844 });
  await page.reload();
  await page.waitForFunction(() => state && !busy && document.querySelector("#controls:not([hidden])"));
  await page.screenshot({ path: "tests/artifacts/phone.png", fullPage: true });
  assert.ok((await page.locator("header").boundingBox()).height < 70);
  const turnTop = await page.evaluate(() => document.querySelector("#turn").getBoundingClientRect().top + scrollY);
  const journalTop = await page.evaluate(() => document.querySelector(".journal").getBoundingClientRect().top + scrollY);
  assert.ok(turnTop < 2000 && turnTop < journalTop, `End month at ${turnTop}px, journal at ${journalTop}px`);
  await page.locator("#search").fill("a");
  assert.equal(await page.locator("#countries .country").count(), 8);
  await page.locator("#search").fill("");
  assert.deepEqual(errors, []);
  assert.deepEqual(external, []);
  console.log(
    "Browser passed: selection, free-text action cards, draft persistence, map targets, progress, events, reload; no external requests or JS errors.",
  );
} finally {
  await browser.close();
}
