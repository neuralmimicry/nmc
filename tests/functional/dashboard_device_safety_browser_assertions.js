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

    async function runNetworkOverviewCase() {
        await waitFor("exact Tracey inventory match", () => (
            document.getElementById("networkInventorySummary")?.textContent.includes("1 hosts · 1 agents")
        ));
        assert(document.getElementById("networkInventoryCount").textContent.includes("2 of 2 entries"), "the combined overview duplicated Tracey as a separate device");
        assert(document.getElementById("networkUnlinkedTraceyDetails").hidden, "an exact Tracey match was also listed as unlinked");

        const traceyRow = document.querySelector('#networkInventoryRows button[data-network-agent-id="tracey-spirit"]');
        assert(traceyRow, "the matched Tracey health signal was not included in the device table");
        assert(traceyRow.parentElement.textContent.includes("healthy"), "the matched Tracey status was not shown beside its device");

        const hostNode = document.querySelector('#networkInventoryGraph [data-network-entity="device:spirit"]');
        assert(hostNode, "the verified host is missing from the graphical inventory");
        assert(hostNode.getAttribute("aria-label").includes("Tracey agent: healthy"), "the graph node omitted its exact-match Tracey health overlay");
        assert(document.querySelector("#networkInventoryGraph .network-edge-manages"), "the registered BMC-to-host relationship is missing from the graph");
        hostNode.dispatchEvent(new MouseEvent("click", { bubbles: true }));
        await waitFor("graph device drilldown", () => !document.getElementById("networkDeviceModal").hidden);

        const body = document.getElementById("networkDeviceModalBody");
        assert(body.textContent.includes("Tracey health on this host"), "the device drilldown omitted its linked Tracey evidence");
        assert(body.querySelector('button[data-network-operation="dhcp"]'), "matching DHCP evidence is unavailable in the host drilldown");
        assert(body.querySelector('button[data-network-operation="diagnostics"]'), "mapped BMC diagnostics are unavailable in the host drilldown");
        assert(body.querySelector('button[data-network-agent-id="tracey-spirit"]'), "the device drilldown has no Tracey-specific details link");

        body.querySelector('button[data-network-operation="dhcp"]').click();
        await waitFor("device-specific DHCP evidence", () => body.textContent.includes("192.168.1.2"));
        const dhcpRequest = state.requests.find((request) => request.path === "/devices/dhcp");
        assert(dhcpRequest?.method === "GET", "device DHCP evidence did not use a read-only GET");

        body.querySelector('button[data-network-operation="diagnostics"]').click();
        await waitFor("mapped controller diagnostics", () => state.requests.some((request) => request.path === "/devices/controllers/diagnostics"));
        const diagnosticsRequest = state.requests.find((request) => request.path === "/devices/controllers/diagnostics");
        assert(diagnosticsRequest.method === "GET", "mapped controller diagnostics did not use a read-only GET");
        assertNoControllerMutations();
    }

    async function runAmbiguousTraceyIdentityCase() {
        await waitFor("ambiguous Tracey identity evidence", () => (
            !document.getElementById("networkUnlinkedTraceyDetails").hidden
            && document.getElementById("networkUnlinkedTraceyCount").textContent === "1"
        ));
        assert(document.getElementById("networkInventoryCount").textContent.includes("3 of 3 entries"), "an ambiguous Tracey agent created or removed a device entry");
        assert(document.getElementById("networkInventorySummary").textContent.includes("0 hosts · 0 agents"), "an ambiguous agent was falsely attached to a host");
        assert(document.getElementById("networkUnlinkedTraceyRows").textContent.includes("ambiguous exact identity"), "the ambiguity reason was not made visible to the operator");
        assert(!document.querySelector('#networkInventoryRows button[data-network-agent-id="tracey-spirit"]'), "ambiguous Tracey evidence was duplicated into a device row");
        assertNoControllerMutations();
    }

    async function run() {
        assert(state, "the browser fixture bootstrap did not run");
        if (state.mode === "stale-inventory") await runStaleInventoryCase();
        else if (state.mode === "inventory-unavailable") await runInventoryUnavailableCase();
        else if (state.mode === "tracey-unavailable") await runTraceyUnavailableCase();
        else if (state.mode === "network-overview") await runNetworkOverviewCase();
        else if (state.mode === "network-overview-ambiguous") await runAmbiguousTraceyIdentityCase();
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
