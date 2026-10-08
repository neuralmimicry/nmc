(() => {
    "use strict";

    const prefix = "/services/health/monitoring";
    const mode = new URLSearchParams(window.location.search).get("browser-test") || "";
    const requests = [];
    const errors = [];
    const identity = {
        user: "Continuum dashboard browser test",
        role: "user",
        authenticated: true,
        service_access: {
            continuum: { access_level: "observe", visible_access_level: "observe", public_access_level: "observe", visible: true },
            tracey: { access_level: "observe", visible_access_level: "observe", public_access_level: "observe", visible: true }
        }
    };

    localStorage.setItem("nmc_dashboard_token", "browser-test-token");
    localStorage.setItem("nmc_dashboard_identity", JSON.stringify(identity));

    const state = { mode, requests, errors };
    window.__nmcDashboardBrowserTest = state;
    window.addEventListener("error", (event) => errors.push(String(event.message || "window error")));
    window.addEventListener("unhandledrejection", (event) => errors.push(String(event.reason || "unhandled rejection")));

    const device = {
        id: "spirit",
        name: "spirit",
        kind: "host",
        source: "browser fixture",
        addresses: ["192.168.1.2"],
        mac_addresses: ["02:00:00:00:00:02"],
        depends_on: [],
        services: ["nmc-server"],
        last_seen_unix_ms: Date.now()
    };
    const controller = {
        id: "spirit-bmc",
        protocol: "redfish",
        vendor_profile: "hp_ilo",
        endpoint: "https://192.168.1.51",
        mac_address: "02:00:00:00:00:51",
        manages: ["spirit"],
        power_actions_enabled: true
    };

    function reply(payload, status = 200) {
        return new Response(JSON.stringify(payload), {
            status,
            headers: { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" }
        });
    }

    function inventory(stale) {
        return {
            data: {
                schema_version: 1,
                revision: stale ? "stale-browser-fixture" : "fresh-browser-fixture",
                generated_at_unix_ms: stale ? Date.now() - 60 * 60 * 1000 : Date.now(),
                stale,
                devices: [device],
                controllers: [controller]
            }
        };
    }

    const emptyLists = new Set([
        "/k8s/list", "/vcluster/list", "/openshift/clusters", "/openstack/clusters", "/proxmox/clusters",
        "/vm/list", "/openshift/resources", "/openstack/resources", "/proxmox/resources"
    ]);
    const originalFetch = window.fetch.bind(window);

    window.fetch = async (input, init = {}) => {
        const requestUrl = new URL(typeof input === "string" ? input : input.url, window.location.href);
        if (!requestUrl.pathname.startsWith(`${prefix}/`)) return originalFetch(input, init);

        const path = requestUrl.pathname.slice(prefix.length);
        const method = String(init.method || (typeof input === "object" ? input.method : "GET") || "GET").toUpperCase();
        requests.push({ path, search: requestUrl.search, method });

        if (path === "/devices/inventory") {
            if (mode === "inventory-unavailable") return reply({ message: "fixture inventory unavailable" }, 503);
            return reply(inventory(mode === "stale-inventory"));
        }
        if (path === "/tracey/agents") {
            if (mode === "tracey-unavailable") return reply({ message: "fixture Tracey unavailable" }, 503);
            return reply({ data: { agents: [], summary: {} } });
        }
        if (path === "/devices/controllers/diagnostics") {
            return reply({ data: { controller_id: "spirit-bmc", capabilities: ["GracefulRestart"], status: "diagnostics-only" } });
        }
        if (emptyLists.has(path)) return reply({ data: [] });
        if (path === "/tracey/ai-lab") return reply({ data: { summary: {}, reports: [] } });
        if (path === "/k8s/healthz") return reply({ data: { status: "healthy" } });
        if (path === "/connections/status") return reply({ data: { connected: false } });
        return reply({ data: {} });
    };
})();
