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
const browser = await chromium.launch({ headless: true });
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
  await page.getByRole("button", { name: "Political" }).click();
  await page.waitForFunction(() => mapMode === "political" && document.querySelector(".map-legend").hidden);
  await page.locator("#action-0").fill("Invest in the national railway network");
  await page.locator("#add-action").click();
  await page.locator("#action-1").fill("Open trade talks with Spain");
  assert.equal(await page.locator(".action-card").count(), 2);
  await page.reload();
  await page.waitForFunction(
    () => document.querySelector("#action-1")?.value === "Open trade talks with Spain",
  );
  await page.locator("#turn").click();
  await page.waitForFunction(
    () => document.querySelector("#date").textContent === "February 2026" && !busy,
  );
  await page
    .locator("#journal details")
    .first()
    .evaluate((d) => (d.open = true));
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
  assert.equal(await page.evaluate(id=>{let owner;layers.eachLayer(l=>{if(l.feature.id===id)owner=l.feature.properties.owner;});return owner;},rid),"ES");
  await page.evaluate(()=>{const e=state.entities["e3-fixture-port"];map.setView([e.latitude,e.longitude],7);});
  await page.waitForFunction(()=>entityLayers.getLayers().some(l=>l.getPopup()?.getContent()?.textContent.includes("Fixture port")));
  await page.evaluate(()=>{entityLayers.getLayers().find(l=>l.getPopup()?.getContent()?.textContent.includes("Fixture port")).openPopup();});
  assert.match(await page.locator(".leaflet-popup-content").innerText(),/Fixture port.*under_construction.*origin t3-fixture/);
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

  fs.mkdirSync("tests/artifacts", { recursive: true });
  await page.screenshot({
    path: "tests/artifacts/browser.png",
    fullPage: true,
  });
  assert.deepEqual(errors, []);
  assert.deepEqual(external, []);
  console.log(
    "Browser passed: selection, free-text action cards, draft persistence, map targets, progress, events, reload; no external requests or JS errors.",
  );
} finally {
  await browser.close();
}
