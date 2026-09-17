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
  await page.locator("#action").selectOption("economy");
  await page.locator("#add").click();
  assert.equal(
    JSON.parse(await page.locator("#draft").inputValue())[0].action,
    "economy",
  );
  await page.locator("#turn").click();
  await page.waitForFunction(
    () => document.querySelector("#date").textContent === "February 2026",
  );
  assert.match(
    await page.locator("#journal").innerText(),
    /PT completed economy/,
  );
  await page.locator("#instruction").fill("Invest in army");
  await page.locator("#propose").click();
  await page.waitForFunction(() =>
    document.querySelector("#draft").value.includes("army"),
  );
  assert.equal(await page.locator("#date").innerText(), "February 2026");
  await page.locator("#draft").fill('[{"action":"navy"}]');
  await page.locator("#turn").click();
  await page.waitForFunction(
    () => document.querySelector("#date").textContent === "March 2026",
  );
  await page.reload();
  try {
    await page.waitForFunction(
      () => document.querySelector("#date").textContent === "March 2026",
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
  assert.ok((await page.locator("path.leaflet-interactive").count()) > 0);
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

  fs.mkdirSync("tests/artifacts", { recursive: true });
  await page.screenshot({
    path: "tests/artifacts/browser.png",
    fullPage: true,
  });
  assert.deepEqual(errors, []);
  assert.deepEqual(external, []);
  console.log(
    "Browser passed: selection, structured orders, reviewed proposals, progress, events, reload; no external requests or JS errors.",
  );
} finally {
  await browser.close();
}
