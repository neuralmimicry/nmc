(function () {
    const REFRESH_INTERVAL_MS = 15000;
    const TOKEN_STORAGE_KEY = "nmc_dashboard_token";

    const nodes = {
        sessionLabel: document.getElementById("sessionLabel"),
        logoutBtn: document.getElementById("logoutBtn"),
        k8sMetric: document.getElementById("k8sMetric"),
        openshiftMetric: document.getElementById("openshiftMetric"),
        vmMetric: document.getElementById("vmMetric"),
        traceyMetric: document.getElementById("traceyMetric"),
        k8sRows: document.getElementById("k8sRows"),
        openshiftRows: document.getElementById("openshiftRows"),
        traceyRows: document.getElementById("traceyRows"),
        traceyNetworkRows: document.getElementById("traceyNetworkRows"),
        k8sApiPill: document.getElementById("k8sApiPill"),
        authPill: document.getElementById("authPill"),
        refreshPill: document.getElementById("refreshPill"),
        connStatus: document.getElementById("connStatus"),
        resourceStatus: document.getElementById("resourceStatus"),
        vmInventoryStatus: document.getElementById("vmInventoryStatus"),
        traceyStatus: document.getElementById("traceyStatus")
    };

    let authToken = loadToken();

    function nowIsoTime() {
        return new Date().toLocaleTimeString();
    }

    function escapeHtml(value) {
        return String(value)
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/\"/g, "&quot;")
            .replace(/'/g, "&#039;");
    }

    function loadToken() {
        const urlToken = (new URLSearchParams(window.location.search).get("token") || "").trim();
        if (urlToken) {
            localStorage.setItem(TOKEN_STORAGE_KEY, urlToken);
            return urlToken;
        }
        return (localStorage.getItem(TOKEN_STORAGE_KEY) || "").trim();
    }

    function saveToken(token) {
        const trimmed = (token || "").trim();
        if (trimmed) {
            localStorage.setItem(TOKEN_STORAGE_KEY, trimmed);
        } else {
            localStorage.removeItem(TOKEN_STORAGE_KEY);
        }
        authToken = trimmed;
        updateSessionLabel();
        return trimmed;
    }

    function maskToken(token) {
        if (!token) {
            return "Token not set";
        }
        if (token.length <= 10) {
            return token;
        }
        return `${token.slice(0, 4)}...${token.slice(-4)}`;
    }

    function updateSessionLabel() {
        if (!nodes.sessionLabel) {
            return;
        }
        nodes.sessionLabel.textContent = authToken ? `Bearer ${maskToken(authToken)}` : "Token not set";
    }

    function authHeaders() {
        const headers = { "Accept": "application/json" };
        if (!authToken) {
            return headers;
        }
        headers.Authorization = `Bearer ${authToken}`;
        headers["X-NMC-Token"] = authToken;
        return headers;
    }

    async function fetchJson(path) {
        try {
            const response = await fetch(path, {
                headers: authHeaders(),
                cache: "no-store"
            });
            const text = await response.text();
            const payload = text ? JSON.parse(text) : {};
            return { ok: response.ok, status: response.status, payload };
        } catch (error) {
            return { ok: false, status: 0, payload: null, error: String(error) };
        }
    }

    function responseData(payload) {
        if (payload && typeof payload === "object" && Object.prototype.hasOwnProperty.call(payload, "data")) {
            return payload.data;
        }
        return payload;
    }

    function arrayFromUnknown(value) {
        const data = responseData(value);
        if (Array.isArray(data)) {
            return data;
        }
        if (data && typeof data === "object") {
            if (Array.isArray(data.data)) {
                return data.data;
            }
            if (Array.isArray(data.clusters)) {
                return data.clusters;
            }
            if (Array.isArray(data.items)) {
                return data.items;
            }
        }
        return [];
    }

    function normalizeStatus(status) {
        const v = String(status || "unknown").toLowerCase();
        if (["running", "ready", "healthy", "active", "ok"].includes(v)) {
            return "ok";
        }
        if (["provisioning", "pending", "creating", "degraded", "warning"].includes(v)) {
            return "warn";
        }
        if (["offline", "failed", "error", "stale", "unhealthy", "suspended", "down"].includes(v)) {
            return "bad";
        }
        return "neutral";
    }

    function statusBadge(status) {
        const bucket = normalizeStatus(status);
        const cls = {
            ok: "badge-ok",
            warn: "badge-warn",
            bad: "badge-bad",
            neutral: "badge-neutral"
        }[bucket];
        return `<span class="badge ${cls}">${escapeHtml(status || "unknown")}</span>`;
    }

    function renderRows(tbody, rows, emptyMessage, columns) {
        if (!rows.length) {
            tbody.innerHTML = `<tr><td colspan="${columns}" class="empty">${escapeHtml(emptyMessage)}</td></tr>`;
            return;
        }
        tbody.innerHTML = rows.join("");
    }

    function setPill(node, label, value, tone) {
        node.innerHTML = `${label}: <span>${escapeHtml(value)}</span>`;
        node.style.borderColor = tone;
    }

    function setCheck(node, text, tone) {
        node.textContent = text;
        node.style.color = tone;
    }

    function summarizeRunning(items) {
        let running = 0;
        for (const item of items) {
            const status = String(item.status || "").toLowerCase();
            if (["running", "ready", "healthy", "active"].includes(status)) {
                running += 1;
            }
        }
        return { running, total: items.length };
    }

    function redirectToLogin(reason) {
        const query = new URLSearchParams();
        if (reason) {
            query.set("reason", reason);
        }
        const next = `${window.location.pathname}${window.location.search}${window.location.hash}`;
        if (next && !next.startsWith("/login")) {
            query.set("next", next);
        }
        const suffix = query.toString();
        window.location.href = suffix ? `/login?${suffix}` : "/login";
    }

    function updateAuthPill(lastResults) {
        const unauthorized = lastResults.some((r) => r.status === 401);
        if (unauthorized) {
            setPill(nodes.authPill, "Auth", "Unauthorized", "rgba(239,68,68,0.85)");
            window.setTimeout(() => redirectToLogin("unauthorized"), 400);
            return;
        }

        if (authToken) {
            setPill(nodes.authPill, "Auth", "Token Applied", "rgba(34,197,94,0.8)");
        } else {
            setPill(nodes.authPill, "Auth", "Token Optional", "rgba(216,210,202,0.45)");
        }
    }

    async function refreshDashboard() {
        setPill(nodes.refreshPill, "Refresh", "Updating", "rgba(245,158,11,0.8)");

        const [
            k8sListRes,
            openshiftClustersRes,
            vmListRes,
            traceyAgentsRes,
            k8sHealthRes,
            connectionRes,
            openshiftResourcesRes
        ] = await Promise.all([
            fetchJson("/k8s/list"),
            fetchJson("/openshift/clusters"),
            fetchJson("/vm/list"),
            fetchJson("/tracey/agents"),
            fetchJson("/k8s/healthz"),
            fetchJson("/connections/status"),
            fetchJson("/openshift/resources")
        ]);

        const allResponses = [
            k8sListRes,
            openshiftClustersRes,
            vmListRes,
            traceyAgentsRes,
            k8sHealthRes,
            connectionRes,
            openshiftResourcesRes
        ];
        updateAuthPill(allResponses);

        const k8sItems = arrayFromUnknown(k8sListRes.payload);
        const k8sSummary = summarizeRunning(k8sItems);
        nodes.k8sMetric.textContent = `${k8sSummary.running} / ${k8sSummary.total}`;
        const k8sRows = k8sItems.map((cluster) => {
            const name = cluster.name || cluster.id || "unknown";
            const region = cluster.region || cluster.location || "-";
            const refiner = cluster.refiner && typeof cluster.refiner === "object" ? cluster.refiner : null;
            let status = cluster.status || "Unknown";
            if (refiner) {
                const desired = Number(refiner.desired_replicas);
                const available = Number(refiner.available_replicas);
                if (Number.isFinite(desired) && Number.isFinite(available)) {
                    status = `${status} (Refiner ${available}/${desired})`;
                }
            }
            return `<tr><td>${escapeHtml(name)}</td><td>${escapeHtml(region)}</td><td>${statusBadge(status)}</td></tr>`;
        });
        renderRows(nodes.k8sRows, k8sRows, "No Kubernetes clusters detected.", 3);

        const openshiftItems = arrayFromUnknown(openshiftClustersRes.payload);
        const openshiftSummary = summarizeRunning(openshiftItems);
        nodes.openshiftMetric.textContent = `${openshiftSummary.running} / ${openshiftSummary.total}`;
        const openshiftRows = openshiftItems.map((cluster) => {
            const name = cluster.name || cluster.id || "unknown";
            const endpoint = cluster.endpoint || cluster.api || "-";
            const status = cluster.status || "Unknown";
            return `<tr><td>${escapeHtml(name)}</td><td>${escapeHtml(endpoint)}</td><td>${statusBadge(status)}</td></tr>`;
        });
        renderRows(nodes.openshiftRows, openshiftRows, "No OpenShift clusters detected.", 3);

        const vmItems = arrayFromUnknown(vmListRes.payload);
        const vmSummary = summarizeRunning(vmItems);
        nodes.vmMetric.textContent = `${vmSummary.running} / ${vmSummary.total}`;

        const traceyData = responseData(traceyAgentsRes.payload) || {};
        const traceyAgents = Array.isArray(traceyData.agents) ? traceyData.agents : [];
        const traceySummary = traceyData.summary || {};
        const requirementSummary = traceyData.requirement_summary || {};
        nodes.traceyMetric.textContent = `${traceySummary.healthy || 0} / ${traceySummary.total || 0}`;
        const traceyRows = traceyAgents.map((agent) => {
            const status = agent.stale ? "stale" : (agent.status || "unknown");
            const ago = Number(agent.last_seen_seconds_ago || 0);
            const lastSeen = ago < 1 ? "just now" : `${ago}s ago`;
            return `<tr><td>${escapeHtml(agent.agent_id || "unknown")}</td><td>${escapeHtml(agent.cluster || "-")}</td><td>${statusBadge(status)}</td><td>${escapeHtml(lastSeen)}</td><td>${escapeHtml(agent.version || "-")}</td></tr>`;
        });
        renderRows(nodes.traceyRows, traceyRows, "No Tracey agent heartbeats received yet.", 5);

        const traceyNetworkRows = traceyAgents.map((agent) => {
            const node = agent.agent_id || "unknown";
            const source = agent.source || "unknown";
            const announceAddr = agent.announce_addr || agent.host || "-";
            const statusAddr = agent.status_addr || "-";
            const linkState = agent.stale ? "offline" : (agent.link_state || "unknown");
            const linkSecurity = agent.link_security || "unknown";
            const queryFailures = Number(agent.query_failures || 0);
            const lastError = String(agent.last_error || "").trim();
            const failureText = queryFailures > 0
                ? `${queryFailures}${lastError ? ` (${lastError})` : ""}`
                : "0";
            return `<tr><td>${escapeHtml(node)}</td><td>${escapeHtml(source)}</td><td>${escapeHtml(announceAddr)}</td><td>${escapeHtml(statusAddr)}</td><td>${statusBadge(linkState)}</td><td>${escapeHtml(linkSecurity)}</td><td>${escapeHtml(failureText)}</td></tr>`;
        });
        renderRows(nodes.traceyNetworkRows, traceyNetworkRows, "No discovered Tracey nodes yet.", 7);

        const k8sHealthy = k8sHealthRes.ok;
        setPill(nodes.k8sApiPill, "K8s API", k8sHealthy ? "Healthy" : "Unavailable", k8sHealthy ? "rgba(34,197,94,0.8)" : "rgba(239,68,68,0.8)");

        const connectionData = responseData(connectionRes.payload) || {};
        const connectionState = String(connectionData.status || "").toLowerCase();
        const connectionText = connectionRes.ok
            ? (connectionState === "local_only" ? "Reachable (Local mode)" : "Reachable")
            : "Unavailable";
        setCheck(nodes.connStatus, connectionText, connectionRes.ok ? "#22c55e" : "#ef4444");
        setCheck(nodes.resourceStatus, openshiftResourcesRes.ok ? "Reachable" : "Unavailable", openshiftResourcesRes.ok ? "#22c55e" : "#ef4444");
        setCheck(nodes.vmInventoryStatus, vmListRes.ok ? `${vmItems.length} listed` : "Unavailable", vmListRes.ok ? "#22c55e" : "#ef4444");
        const reachableTracey = Number(traceySummary.reachable || 0);
        const discoveredTracey = Number(traceySummary.discovered || 0);
        const nonCompliantResources = Number(requirementSummary.noncompliant || 0);
        const managedResources = Number(requirementSummary.total || 0);
        const traceyTone = !traceyAgentsRes.ok
            ? "#ef4444"
            : (nonCompliantResources > 0 ? "#f59e0b" : "#22c55e");
        setCheck(
            nodes.traceyStatus,
            traceyAgentsRes.ok
                ? `${traceyAgents.length} detected (${reachableTracey} reachable, ${discoveredTracey} discovered, ${nonCompliantResources}/${managedResources} non-compliant managed resources)`
                : "Unavailable",
            traceyTone
        );

        setPill(nodes.refreshPill, "Refresh", `Updated ${nowIsoTime()}`, "rgba(216,210,202,0.45)");
    }

    function initializeSessionUi() {
        updateSessionLabel();
        if (!nodes.logoutBtn) {
            return;
        }
        nodes.logoutBtn.addEventListener("click", () => {
            saveToken("");
            redirectToLogin("logout");
        });
    }

    initializeSessionUi();
    refreshDashboard();
    window.setInterval(refreshDashboard, REFRESH_INTERVAL_MS);
})();
