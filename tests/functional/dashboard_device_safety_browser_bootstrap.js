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
                devices: mode === "network-overview-ambiguous"
                    ? [device, { ...device, id: "spirit-shadow", name: "spirit-shadow" }]
                    : [device],
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

        if (["login-redirect", "cross-origin-login-redirect"].includes(mode) && path === "/k8s/list") {
            return {
                redirected: true,
                url: mode === "login-redirect"
                    ? `${window.location.origin}${prefix}/login?reason=expired`
                    : "https://login.example.invalid/login",
                ok: true,
                status: 200,
                headers: new Headers({ "Content-Type": "text/html; charset=utf-8" }),
                text: async () => "<!doctype html><html><body>Sign in</body></html>"
            };
        }

        if (path === "/devices/inventory") {
            if (mode === "inventory-unavailable") return reply({ message: "fixture inventory unavailable" }, 503);
            return reply(inventory(mode === "stale-inventory"));
        }
        if (path === "/tracey/agents") {
            if (mode === "tracey-unavailable") return reply({ message: "fixture Tracey unavailable" }, 503);
            const agents = ["network-overview", "network-overview-ambiguous"].includes(mode) ? [{
                agent_id: "tracey-spirit",
                host: "spirit",
                announce_addr: "192.168.1.2",
                status_addr: "http://192.168.1.2:9782",
                status: "healthy",
                stale: false,
                version: "browser-fixture",
                last_seen_seconds_ago: 4,
                last_seen_epoch_ms: Date.now()
            }] : [];
            return reply({ data: { agents, summary: { healthy: agents.length, total: agents.length } } });
        }
        if (path === "/devices/controllers/diagnostics") {
            return reply({ data: { controller_id: "spirit-bmc", capabilities: ["GracefulRestart"], status: "diagnostics-only" } });
        }
        if (path === "/devices/dhcp") {
            return reply({ data: { inventory_revision: "fresh-browser-fixture", inventory_stale: false, servers: [{
                server_id: "vega-dhcp",
                leases: [{ mac_address: device.mac_addresses[0], ip_address: device.addresses[0], hostname: device.name }]
            }] } });
        }
        if (emptyLists.has(path)) return reply({ data: [] });
        if (path === "/tracey/ai-lab") return reply({ data: { summary: {}, reports: [] } });
        if (path === "/k8s/healthz") return reply({ data: { status: "healthy" } });
        if (path === "/connections/status") return reply({ data: { connected: false } });
        return reply({ data: {} });
    };
})();
