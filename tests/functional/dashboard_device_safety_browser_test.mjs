#!/usr/bin/env node
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { accessSync, constants, existsSync, readFileSync } from "node:fs";
import { createServer } from "node:http";
import { tmpdir, homedir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, "../..");
const docsRoot = path.join(repositoryRoot, "nmc_server/src/docs");
const monitoringPrefix = "/services/health/monitoring";
const resultPattern = /^NMC-DASHBOARD-BROWSER-RESULT:(?:PASS|FAIL):/;

function isExecutable(filePath) {
    try {
        accessSync(filePath, constants.X_OK);
        return true;
    } catch (_error) {
        return false;
    }
}

function fromPath(command) {
    for (const directory of (process.env.PATH || "").split(path.delimiter)) {
        const candidate = path.join(directory, command);
        if (isExecutable(candidate)) return candidate;
    }
    return "";
}

function executable(override, command, candidates = []) {
    const choices = [override, ...candidates, fromPath(command)].filter(Boolean);
    return choices.find((candidate) => isExecutable(candidate)) || "";
}

function contentType(filePath) {
    if (filePath.endsWith(".html")) return "text/html; charset=utf-8";
    if (filePath.endsWith(".js")) return "text/javascript; charset=utf-8";
    if (filePath.endsWith(".css")) return "text/css; charset=utf-8";
    if (filePath.endsWith(".svg")) return "image/svg+xml";
    if (filePath.endsWith(".json")) return "application/json; charset=utf-8";
    return "application/octet-stream";
}

async function serveFile(response, filePath) {
    try {
        const body = await readFileSync(filePath);
        response.writeHead(200, { "Content-Type": contentType(filePath), "Cache-Control": "no-store" });
        response.end(body);
    } catch (_error) {
        response.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" });
        response.end("Not found");
    }
}

const server = createServer(async (request, response) => {
    const requestUrl = new URL(request.url || "/", `http://${request.headers.host || "127.0.0.1"}`);
    if (requestUrl.pathname === monitoringPrefix || requestUrl.pathname === `${monitoringPrefix}/`) {
        let html = readFileSync(path.join(docsRoot, "index.html"), "utf8");
        const dashboardScript = `<script src="${monitoringPrefix}/dashboard.js"></script>`;
        const testScripts = [
            `<script src="${monitoringPrefix}/__test/dashboard_device_safety_browser_bootstrap.js"></script>`,
            dashboardScript,
            `<script src="${monitoringPrefix}/__test/dashboard_device_safety_browser_assertions.js"></script>`
        ].join("\n");
        assert(html.includes(dashboardScript), "the built-in dashboard script tag has changed; update the browser harness injection point");
        html = html.replace(dashboardScript, testScripts);
        response.writeHead(200, { "Content-Type": "text/html; charset=utf-8", "Cache-Control": "no-store" });
        response.end(html);
        return;
    }

    if (requestUrl.pathname.startsWith(`${monitoringPrefix}/__test/`)) {
        const filename = path.basename(requestUrl.pathname);
        await serveFile(response, path.join(scriptDirectory, filename));
        return;
    }

    if (!requestUrl.pathname.startsWith(`${monitoringPrefix}/`)) {
        response.writeHead(404);
        response.end();
        return;
    }

    const relativePath = decodeURIComponent(requestUrl.pathname.slice(monitoringPrefix.length)).replace(/^\/+/, "") || "index.html";
    const filePath = path.resolve(docsRoot, relativePath);
    if (filePath !== docsRoot && !filePath.startsWith(`${docsRoot}${path.sep}`)) {
        response.writeHead(400);
        response.end("Invalid asset path");
        return;
    }
    await serveFile(response, filePath);
});

async function unusedPort() {
    const listener = createServer();
    await new Promise((resolve, reject) => {
        listener.once("error", reject);
        listener.listen(0, "127.0.0.1", resolve);
    });
    const address = listener.address();
    assert(address && typeof address === "object", "could not allocate a local WebDriver port");
    await new Promise((resolve) => listener.close(resolve));
    return address.port;
}

async function webdriverRequest(baseUrl, route, method = "GET", body) {
    const response = await fetch(`${baseUrl}${route}`, {
        method,
        headers: body === undefined ? undefined : { "Content-Type": "application/json" },
        body: body === undefined ? undefined : JSON.stringify(body),
        signal: AbortSignal.timeout(60000)
    });
    const responseText = await response.text();
    let result = null;
    if (responseText) {
        try {
            result = JSON.parse(responseText);
        } catch (_error) {
            throw new Error(`WebDriver returned non-JSON for ${method} ${route}: ${responseText.slice(0, 500)}`);
        }
    }
    if (!response.ok) throw new Error(`WebDriver ${method} ${route} failed with HTTP ${response.status}: ${JSON.stringify(result)}`);
    return result;
}

async function waitForWebDriver(baseUrl, driver) {
    const deadline = Date.now() + 15000;
    while (Date.now() < deadline) {
        if (driver.exitCode !== null) throw new Error(`geckodriver exited with status ${driver.exitCode}`);
        try {
            const response = await fetch(`${baseUrl}/status`, { signal: AbortSignal.timeout(1000) });
            if (response.ok) return;
        } catch (_error) {
            // geckodriver has not bound its local port yet.
        }
        await new Promise((resolve) => setTimeout(resolve, 100));
    }
    throw new Error("geckodriver did not become ready within 15 seconds");
}

async function createBrowserSession(driverUrl, browser) {
    const created = await webdriverRequest(driverUrl, "/session", "POST", {
        capabilities: {
            alwaysMatch: {
                browserName: "firefox",
                "moz:firefoxOptions": { binary: browser, args: ["-headless"] }
            }
        }
    });
    const sessionId = created?.value?.sessionId;
    assert(sessionId, `geckodriver did not return a session id: ${JSON.stringify(created)}`);
    return sessionId;
}

async function runBrowserCase(driverUrl, sessionId, baseUrl, testCase) {
    const sessionPrefix = `/session/${sessionId}`;
    await webdriverRequest(driverUrl, `${sessionPrefix}/url`, "POST", { url: `${baseUrl}?browser-test=${testCase}` });
    const deadline = Date.now() + 20000;
    let title = "";
    while (Date.now() < deadline) {
        const result = await webdriverRequest(driverUrl, `${sessionPrefix}/execute/sync`, "POST", {
            script: "return document.title;",
            args: []
        });
        title = result?.value || "";
        if (resultPattern.test(title)) break;
        await new Promise((resolve) => setTimeout(resolve, 50));
    }

    const expected = `NMC-DASHBOARD-BROWSER-RESULT:PASS:${testCase}`;
    if (title !== expected) {
        const diagnostic = await webdriverRequest(driverUrl, `${sessionPrefix}/execute/sync`, "POST", {
            script: "return document.documentElement.outerHTML.slice(-4000);",
            args: []
        }).catch((error) => ({ value: String(error) }));
        throw new Error(`${testCase}: ${title || "browser assertion timed out"}; page tail: ${diagnostic?.value || "unavailable"}`);
    }
    process.stdout.write(`[dashboard-browser] PASS ${testCase}\n`);
}

const browser = executable(process.env.FIREFOX_BIN, "firefox", [
    "/snap/firefox/current/usr/lib/firefox/firefox",
    "/usr/bin/firefox",
    "/usr/bin/firefox-esr"
]);
const webDriver = executable(process.env.WEBDRIVER_BIN, "geckodriver", [
    "/usr/bin/geckodriver",
    "/snap/firefox/current/usr/lib/firefox/geckodriver",
    "/snap/bin/geckodriver"
]);
assert(browser, "Firefox was not found; set FIREFOX_BIN to a Firefox executable");
assert(webDriver, "geckodriver was not found; set WEBDRIVER_BIN to a geckodriver executable");

const defaultSnapProfileRoot = path.join(homedir(), "snap/firefox/common");
const profileRoot = process.env.NMC_BROWSER_PROFILE_ROOT
    || (existsSync(defaultSnapProfileRoot) ? defaultSnapProfileRoot : tmpdir());
assert(existsSync(profileRoot), `WebDriver profile root does not exist: ${profileRoot}`);
accessSync(profileRoot, constants.W_OK);

await new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", resolve);
});
const address = server.address();
assert(address && typeof address === "object", "the local dashboard fixture server did not bind");
const dashboardUrl = `http://127.0.0.1:${address.port}${monitoringPrefix}`;

const driverPort = await unusedPort();
const driverUrl = `http://127.0.0.1:${driverPort}`;
const driver = spawn(webDriver, [
    "--host", "127.0.0.1",
    "--port", String(driverPort),
    "--profile-root", profileRoot,
    "--binary", browser,
    "--log", "fatal"
], { stdio: "ignore" });
driver.on("error", (error) => {
    process.stderr.write(`[dashboard-browser] unable to start geckodriver: ${error.message}\n`);
});

try {
    await waitForWebDriver(driverUrl, driver);
    const sessionId = await createBrowserSession(driverUrl, browser);
    try {
        for (const testCase of [
            "stale-inventory",
            "inventory-unavailable",
            "tracey-unavailable",
            "network-overview",
            "network-overview-ambiguous"
        ]) {
            await runBrowserCase(driverUrl, sessionId, dashboardUrl, testCase);
        }
    } finally {
        await webdriverRequest(driverUrl, `/session/${sessionId}`, "DELETE").catch(() => {});
    }
} finally {
    await new Promise((resolve) => server.close(resolve));
    driver.kill("SIGTERM");
    await new Promise((resolve) => {
        if (driver.exitCode !== null) resolve();
        else {
            driver.once("exit", resolve);
            setTimeout(resolve, 3000).unref();
        }
    });
}
