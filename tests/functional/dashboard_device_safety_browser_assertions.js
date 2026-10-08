(() => {
    "use strict";

    const state = window.__nmcDashboardBrowserTest;
    const prefix = "NMC-DASHBOARD-BROWSER-RESULT:";

    function assert(condition, message) {
        if (!condition) throw new Error(message);
    }

    async function waitFor(description, predicate, timeoutMs = 8000) {
        const deadline = Date.now() + timeoutMs;
        while (Date.now() < deadline) {
            if (predicate()) return;
            await new Promise((resolve) => window.setTimeout(resolve, 25));
        }
        throw new Error(`Timed out waiting for ${description}`);
    }

    function assertNoControllerMutations() {
        const mutations = state.requests.filter((request) => (
            request.path === "/devices/controllers/actions"
            || (request.path.startsWith("/devices/controllers/") && request.method !== "GET")
        ));
        assert(mutations.length === 0, `controller mutation requests were observed: ${JSON.stringify(mutations)}`);
        assert(!document.querySelector('[data-network-operation="action"]'), "the device panel rendered a controller-action control");
    }

    async function runStaleInventoryCase() {
        await waitFor("stale inventory warning", () => (
            document.getElementById("networkInventoryStatus")?.textContent.includes("inventory is stale")
        ));
        assert(document.getElementById("networkInventoryCount").textContent.includes("2 of 2 entries"), "stale inventory entries were hidden instead of visibly marked");

        const rowButton = [...document.querySelectorAll("#networkInventoryRows button[data-network-entity]")]
            .find((button) => button.getAttribute("data-network-entity") === "controller:spirit-bmc");
        assert(rowButton, "the verified controller row was not rendered");
        rowButton.click();
        await waitFor("stale controller drilldown", () => !document.getElementById("networkDeviceModal").hidden);

        const body = document.getElementById("networkDeviceModalBody").textContent;
        assert(body.includes("Inventory is stale"), "the drilldown omitted the stale-inventory policy warning");
        assert(body.includes("does not synthesise or submit controller preflight"), "the controller detail did not state that dashboard preflight is unavailable");
        assertNoControllerMutations();

        const diagnostics = document.querySelector('#networkDeviceModalBody button[data-network-operation="diagnostics"]');
        assert(diagnostics, "the read-only diagnostics operation was not available");
        diagnostics.click();
        await waitFor("read-only controller diagnostics", () => state.requests.some((request) => request.path === "/devices/controllers/diagnostics"));
        const diagnosticRequest = state.requests.find((request) => request.path === "/devices/controllers/diagnostics");
        assert(diagnosticRequest.method === "GET", "controller diagnostics did not use a read-only GET");
        assertNoControllerMutations();
    }

    async function runInventoryUnavailableCase() {
        await waitFor("inventory unavailable state", () => (
            document.getElementById("networkInventorySummary")?.textContent.includes("Continuum inventory is unavailable")
        ));
        assert(document.getElementById("networkInventoryStatus").textContent.includes("HTTP 503"), "the inventory error status was hidden");
        assert(document.getElementById("networkInventoryCount").textContent === "0 entries", "unavailable inventory retained actionable entity rows");
        assert(document.getElementById("networkInventoryRows").textContent.includes("No inventory data is available"), "the table did not show its empty unavailable state");
        assertNoControllerMutations();
    }

    async function runTraceyUnavailableCase() {
        await waitFor("Tracey unavailable state", () => (
            document.getElementById("networkInventoryStatus")?.textContent.includes("no empty-match conclusion was made")
        ));
        assert(document.getElementById("networkInventoryCount").textContent.includes("2 of 2 entries"), "Tracey outage hid the independent Continuum inventory");

        const rowButton = [...document.querySelectorAll("#networkInventoryRows button[data-network-entity]")]
            .find((button) => button.getAttribute("data-network-entity") === "device:spirit");
        assert(rowButton, "the host row was not rendered during the Tracey outage");
        rowButton.click();
        await waitFor("host drilldown during Tracey outage", () => !document.getElementById("networkDeviceModal").hidden);

        const body = document.getElementById("networkDeviceModalBody").textContent;
        assert(body.includes("Tracey data is unavailable or hidden by service access; identity matching was not evaluated."), "the drilldown misreported unavailable Tracey data as an empty match");
        assert(!body.includes("No Tracey agent matched this device by exact registered identity."), "the drilldown made a false no-match claim during an API outage");
        assertNoControllerMutations();
    }

    async function run() {
        assert(state, "the browser fixture bootstrap did not run");
        if (state.mode === "stale-inventory") await runStaleInventoryCase();
        else if (state.mode === "inventory-unavailable") await runInventoryUnavailableCase();
        else if (state.mode === "tracey-unavailable") await runTraceyUnavailableCase();
        else throw new Error(`unknown browser fixture: ${state.mode}`);
        assert(state.errors.length === 0, `uncaught dashboard browser errors: ${state.errors.join("; ")}`);
        return `${prefix}PASS:${state.mode}`;
    }

    void run().then((result) => {
        document.documentElement.setAttribute("data-nmc-dashboard-browser-result", result);
        document.title = result;
    }).catch((error) => {
        const result = `${prefix}FAIL:${state?.mode || "unknown"}:${String(error && error.stack || error)}`;
        document.documentElement.setAttribute("data-nmc-dashboard-browser-result", result);
        document.title = result;
    });
})();
