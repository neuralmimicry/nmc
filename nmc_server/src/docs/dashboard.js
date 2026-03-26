(function () {
    const REFRESH_INTERVAL_MS = 15000;
    const TOKEN_STORAGE_KEY = "nmc_dashboard_token";

    const nodes = {
        sessionLabel: document.getElementById("sessionLabel"),
        logoutBtn: document.getElementById("logoutBtn"),
        k8sMetric: document.getElementById("k8sMetric"),
        vclusterMetric: document.getElementById("vclusterMetric"),
        openshiftMetric: document.getElementById("openshiftMetric"),
        vmMetric: document.getElementById("vmMetric"),
        traceyMetric: document.getElementById("traceyMetric"),
        traceyCard: document.getElementById("traceyCard"),
        openTraceyInsightsBtn: document.getElementById("openTraceyInsightsBtn"),
        k8sRows: document.getElementById("k8sRows"),
        vclusterRows: document.getElementById("vclusterRows"),
        openshiftRows: document.getElementById("openshiftRows"),
        traceyRows: document.getElementById("traceyRows"),
        traceyNetworkRows: document.getElementById("traceyNetworkRows"),
        k8sApiPill: document.getElementById("k8sApiPill"),
        authPill: document.getElementById("authPill"),
        refreshPill: document.getElementById("refreshPill"),
        connStatus: document.getElementById("connStatus"),
        resourceStatus: document.getElementById("resourceStatus"),
        vmInventoryStatus: document.getElementById("vmInventoryStatus"),
        traceyStatus: document.getElementById("traceyStatus"),
        clusterDetailsModal: document.getElementById("clusterDetailsModal"),
        clusterDetailsClose: document.getElementById("clusterDetailsClose"),
        clusterDetailsSubtitle: document.getElementById("clusterDetailsSubtitle"),
        clusterDetailsBody: document.getElementById("clusterDetailsBody"),
        openshiftDetailsModal: document.getElementById("openshiftDetailsModal"),
        openshiftDetailsClose: document.getElementById("openshiftDetailsClose"),
        openshiftDetailsSubtitle: document.getElementById("openshiftDetailsSubtitle"),
        openshiftDetailsBody: document.getElementById("openshiftDetailsBody"),
        traceyInsightsModal: document.getElementById("traceyInsightsModal"),
        traceyInsightsClose: document.getElementById("traceyInsightsClose"),
        traceyInsightsSubtitle: document.getElementById("traceyInsightsSubtitle"),
        traceyWindowSelect: document.getElementById("traceyWindowSelect"),
        traceyBucketSelect: document.getElementById("traceyBucketSelect"),
        traceyInsightsRefresh: document.getElementById("traceyInsightsRefresh"),
        traceyStatusChart: document.getElementById("traceyStatusChart"),
        traceyStatusLegend: document.getElementById("traceyStatusLegend"),
        traceyLogChart: document.getElementById("traceyLogChart"),
        traceyLogLegend: document.getElementById("traceyLogLegend"),
        traceyAnalyticsAgentRows: document.getElementById("traceyAnalyticsAgentRows"),
        traceyAgentDrilldownTitle: document.getElementById("traceyAgentDrilldownTitle"),
        traceyAgentDrilldownMeta: document.getElementById("traceyAgentDrilldownMeta"),
        traceyAgentStatusChart: document.getElementById("traceyAgentStatusChart"),
        traceyAgentStatusLegend: document.getElementById("traceyAgentStatusLegend"),
        traceyAgentFacts: document.getElementById("traceyAgentFacts"),
        traceyAgentLogLevel: document.getElementById("traceyAgentLogLevel"),
        traceyAgentLogCategory: document.getElementById("traceyAgentLogCategory"),
        traceyAgentLogSearch: document.getElementById("traceyAgentLogSearch"),
        traceyAgentLogRows: document.getElementById("traceyAgentLogRows")
    };

    let authToken = loadToken();
    let clusterDetailsRequestSeq = 0;
    let openshiftDetailsRequestSeq = 0;
    let traceyInsightsRequestSeq = 0;
    let traceyAgentRequestSeq = 0;
    const traceyState = {
        analytics: null,
        selectedAgentId: "",
        selectedAgentAnalysis: null,
        selectedAgentLogs: []
    };

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

    function toArray(value) {
        return Array.isArray(value) ? value : [];
    }

    function updateModalBodyLock() {
        const k8sOpen = nodes.clusterDetailsModal && !nodes.clusterDetailsModal.hidden;
        const openshiftOpen = nodes.openshiftDetailsModal && !nodes.openshiftDetailsModal.hidden;
        const traceyOpen = nodes.traceyInsightsModal && !nodes.traceyInsightsModal.hidden;
        document.body.classList.toggle("modal-open", Boolean(k8sOpen || openshiftOpen || traceyOpen));
    }

    function setClusterDetailsModalOpen(open) {
        if (!nodes.clusterDetailsModal) {
            return;
        }
        nodes.clusterDetailsModal.hidden = !open;
        updateModalBodyLock();
    }

    function closeClusterDetailsModal() {
        clusterDetailsRequestSeq += 1;
        setClusterDetailsModalOpen(false);
    }

    function setOpenShiftDetailsModalOpen(open) {
        if (!nodes.openshiftDetailsModal) {
            return;
        }
        nodes.openshiftDetailsModal.hidden = !open;
        updateModalBodyLock();
    }

    function closeOpenShiftDetailsModal() {
        openshiftDetailsRequestSeq += 1;
        setOpenShiftDetailsModalOpen(false);
    }

    function setTraceyInsightsModalOpen(open) {
        if (!nodes.traceyInsightsModal) {
            return;
        }
        nodes.traceyInsightsModal.hidden = !open;
        updateModalBodyLock();
    }

    function closeTraceyInsightsModal() {
        traceyInsightsRequestSeq += 1;
        traceyAgentRequestSeq += 1;
        setTraceyInsightsModalOpen(false);
    }

    function traceyWindowSeconds() {
        const value = Number(nodes.traceyWindowSelect ? nodes.traceyWindowSelect.value : 3600);
        if (!Number.isFinite(value) || value <= 0) {
            return 3600;
        }
        return Math.max(300, Math.min(604800, Math.round(value)));
    }

    function traceyBucketSeconds() {
        const value = Number(nodes.traceyBucketSelect ? nodes.traceyBucketSelect.value : 60);
        if (!Number.isFinite(value) || value <= 0) {
            return 60;
        }
        return Math.max(10, Math.min(3600, Math.round(value)));
    }

    function formatAge(seconds) {
        const value = Math.max(0, Number(seconds || 0));
        if (value < 1) {
            return "just now";
        }
        if (value < 60) {
            return `${Math.round(value)}s ago`;
        }
        if (value < 3600) {
            return `${Math.round(value / 60)}m ago`;
        }
        if (value < 86400) {
            return `${Math.round(value / 3600)}h ago`;
        }
        return `${Math.round(value / 86400)}d ago`;
    }

    function formatEpochMs(tsMs) {
        const value = Number(tsMs || 0);
        if (!Number.isFinite(value) || value <= 0) {
            return "-";
        }
        return new Date(value).toLocaleString();
    }

    function parseNumber(value, fallback = 0) {
        const parsed = Number(value);
        return Number.isFinite(parsed) ? parsed : fallback;
    }

    function toPercent(value) {
        return `${(parseNumber(value, 0) * 100).toFixed(1)}%`;
    }

    function statusToneColor(status) {
        const key = String(status || "").toLowerCase();
        if (key === "healthy") {
            return "#22c55e";
        }
        if (key === "degraded") {
            return "#f59e0b";
        }
        if (key === "offline") {
            return "#ef4444";
        }
        return "#94a3b8";
    }

    function levelBadge(level) {
        const normalized = String(level || "info").toLowerCase();
        if (normalized === "error") {
            return `<span class="badge badge-bad">error</span>`;
        }
        if (normalized === "warn" || normalized === "warning") {
            return `<span class="badge badge-warn">warn</span>`;
        }
        return `<span class="badge badge-neutral">info</span>`;
    }

    function createSvgNode(tag, attrs = {}) {
        const node = document.createElementNS("http://www.w3.org/2000/svg", tag);
        for (const [key, value] of Object.entries(attrs)) {
            node.setAttribute(key, String(value));
        }
        return node;
    }

    function clearSvg(svg) {
        if (!svg) {
            return;
        }
        while (svg.firstChild) {
            svg.removeChild(svg.firstChild);
        }
    }

    function renderLegend(node, entries) {
        if (!node) {
            return;
        }
        if (!entries.length) {
            node.innerHTML = `<span class="empty">No legend data available.</span>`;
            return;
        }
        node.innerHTML = entries.map((entry) => {
            const value = entry.value === null || entry.value === undefined ? "-" : entry.value;
            return `<span class="tracey-legend-item"><span class="tracey-legend-swatch" style="background:${escapeHtml(entry.color)}"></span>${escapeHtml(entry.label)}: <strong>${escapeHtml(String(value))}</strong></span>`;
        }).join("");
    }

    function renderLineChart(svg, rows, seriesDefs) {
        if (!svg) {
            return;
        }
        clearSvg(svg);
        const width = 700;
        const height = 220;
        const marginLeft = 42;
        const marginRight = 14;
        const marginTop = 12;
        const marginBottom = 24;
        const usableWidth = width - marginLeft - marginRight;
        const usableHeight = height - marginTop - marginBottom;
        svg.setAttribute("viewBox", `0 0 ${width} ${height}`);

        if (!rows.length) {
            svg.appendChild(createSvgNode("text", {
                x: width / 2,
                y: height / 2,
                "text-anchor": "middle",
                class: "tracey-chart-empty"
            })).textContent = "No data in selected window";
            return;
        }

        let maxY = 0;
        for (const row of rows) {
            for (const series of seriesDefs) {
                maxY = Math.max(maxY, parseNumber(row[series.key], 0));
            }
        }
        maxY = Math.max(1, Math.ceil(maxY));

        const axisGroup = createSvgNode("g");
        axisGroup.appendChild(createSvgNode("line", {
            x1: marginLeft,
            y1: marginTop + usableHeight,
            x2: width - marginRight,
            y2: marginTop + usableHeight,
            class: "tracey-chart-axis"
        }));
        axisGroup.appendChild(createSvgNode("line", {
            x1: marginLeft,
            y1: marginTop,
            x2: marginLeft,
            y2: marginTop + usableHeight,
            class: "tracey-chart-axis"
        }));
        svg.appendChild(axisGroup);

        const gridCount = 4;
        for (let i = 1; i <= gridCount; i += 1) {
            const y = marginTop + (usableHeight * i) / gridCount;
            svg.appendChild(createSvgNode("line", {
                x1: marginLeft,
                y1: y,
                x2: width - marginRight,
                y2: y,
                class: "tracey-chart-grid-line"
            }));
        }

        const span = Math.max(1, rows.length - 1);
        const xForIndex = (idx) => marginLeft + (usableWidth * idx) / span;
        const yForValue = (value) => marginTop + usableHeight - (usableHeight * parseNumber(value, 0)) / maxY;

        for (const series of seriesDefs) {
            let d = "";
            rows.forEach((row, idx) => {
                const x = xForIndex(idx);
                const y = yForValue(row[series.key]);
                d += idx === 0 ? `M${x},${y}` : ` L${x},${y}`;
            });
            svg.appendChild(createSvgNode("path", {
                d,
                class: "tracey-chart-line",
                stroke: series.color
            }));

            const lastRow = rows[rows.length - 1];
            const lx = xForIndex(rows.length - 1);
            const ly = yForValue(lastRow[series.key]);
            svg.appendChild(createSvgNode("circle", {
                cx: lx,
                cy: ly,
                r: 3.5,
                fill: series.color,
                class: "tracey-chart-dot"
            }));
        }
    }

    function renderTraceyAnalyticsOverview(data) {
        if (!data) {
            renderRows(nodes.traceyAnalyticsAgentRows, [], "No Tracey analytics data available.", 6);
            renderLineChart(nodes.traceyStatusChart, [], []);
            renderLineChart(nodes.traceyLogChart, [], []);
            renderLegend(nodes.traceyStatusLegend, []);
            renderLegend(nodes.traceyLogLegend, []);
            return;
        }

        const timeline = Array.isArray(data.timeline) ? data.timeline : [];
        renderLineChart(nodes.traceyStatusChart, timeline, [
            { key: "healthy", color: "#22c55e" },
            { key: "degraded", color: "#f59e0b" },
            { key: "offline", color: "#ef4444" },
            { key: "sampled_agents", color: "#94a3b8" }
        ]);
        const latestStatus = timeline.length ? timeline[timeline.length - 1] : {};
        renderLegend(nodes.traceyStatusLegend, [
            { label: "Healthy", color: "#22c55e", value: parseNumber(latestStatus.healthy, 0) },
            { label: "Degraded", color: "#f59e0b", value: parseNumber(latestStatus.degraded, 0) },
            { label: "Offline", color: "#ef4444", value: parseNumber(latestStatus.offline, 0) },
            { label: "Sampled Agents", color: "#94a3b8", value: parseNumber(latestStatus.sampled_agents, 0) }
        ]);

        renderLineChart(nodes.traceyLogChart, timeline, [
            { key: "log_info", color: "#94a3b8" },
            { key: "log_warn", color: "#f59e0b" },
            { key: "log_error", color: "#ef4444" }
        ]);
        const latestLog = timeline.length ? timeline[timeline.length - 1] : {};
        renderLegend(nodes.traceyLogLegend, [
            { label: "Info", color: "#94a3b8", value: parseNumber(latestLog.log_info, 0) },
            { label: "Warn", color: "#f59e0b", value: parseNumber(latestLog.log_warn, 0) },
            { label: "Error", color: "#ef4444", value: parseNumber(latestLog.log_error, 0) }
        ]);

        const agents = Array.isArray(data.agents) ? data.agents : [];
        const rows = agents.map((agent) => {
            const id = String(agent.agent_id || "unknown");
            const status = String(agent.status || "unknown");
            const healthyRatio = toPercent(agent.healthy_ratio);
            const avgQueryFailures = parseNumber(agent.avg_query_failures, 0).toFixed(2);
            const errorLogs = parseNumber(agent.log_error, 0);
            const lastSeen = formatAge(agent.last_seen_seconds_ago);
            const action = `<button class="cluster-link tracey-agent-link" type="button" data-tracey-agent-analysis="${escapeHtml(id)}">${escapeHtml(id)}</button>`;
            return `<tr><td>${action}</td><td>${statusBadge(status)}</td><td>${escapeHtml(healthyRatio)}</td><td>${escapeHtml(avgQueryFailures)}</td><td>${escapeHtml(String(errorLogs))}</td><td>${escapeHtml(lastSeen)}</td></tr>`;
        });
        renderRows(nodes.traceyAnalyticsAgentRows, rows, "No Tracey agents available for analytics.", 6);
    }

    function renderTraceyAgentFacts(analysis) {
        if (!nodes.traceyAgentFacts) {
            return;
        }
        if (!analysis || !analysis.current || typeof analysis.current !== "object") {
            nodes.traceyAgentFacts.innerHTML = `<p class="empty">Select a Tracey agent from the dashboard table or the selector above.</p>`;
            return;
        }
        const current = analysis.current;
        const facts = [
            { key: "Status", value: current.status || "unknown" },
            { key: "Cluster", value: current.cluster || "-" },
            { key: "Host", value: current.host || "-" },
            { key: "Version", value: current.version || "-" },
            { key: "Link State", value: current.link_state || "-" },
            { key: "Link Security", value: current.link_security || "-" },
            { key: "Reachable", value: current.status_reachable ? "yes" : "no" },
            { key: "Query Failures", value: String(parseNumber(current.query_failures, 0)) },
            { key: "Coordinator", value: current.is_coordinator ? "yes" : "no" },
            { key: "Score", value: String(parseNumber(current.score, 0)) },
            { key: "Last Seen", value: formatAge(current.last_seen_seconds_ago) },
            { key: "Last Error", value: current.last_error || "-" }
        ];
        nodes.traceyAgentFacts.innerHTML = facts.map((fact) => {
            return `<div class="tracey-fact"><span>${escapeHtml(fact.key)}</span><strong>${escapeHtml(fact.value)}</strong></div>`;
        }).join("");
    }

    function renderTraceyAgentLogs() {
        if (!nodes.traceyAgentLogRows) {
            return;
        }
        const logs = Array.isArray(traceyState.selectedAgentLogs) ? traceyState.selectedAgentLogs : [];
        if (!logs.length) {
            renderRows(nodes.traceyAgentLogRows, [], "No logs available for this agent and window.", 4);
            return;
        }

        const selectedLevel = String(nodes.traceyAgentLogLevel ? nodes.traceyAgentLogLevel.value : "all").toLowerCase();
        const categoryFilter = String(nodes.traceyAgentLogCategory ? nodes.traceyAgentLogCategory.value : "").trim().toLowerCase();
        const searchFilter = String(nodes.traceyAgentLogSearch ? nodes.traceyAgentLogSearch.value : "").trim().toLowerCase();

        const rows = [];
        for (const log of logs) {
            const level = String(log.level || "info").toLowerCase();
            const category = String(log.category || "general");
            const message = String(log.message || "");
            const context = log.context && typeof log.context === "object" ? JSON.stringify(log.context) : "";

            if (selectedLevel !== "all" && level !== selectedLevel) {
                continue;
            }
            if (categoryFilter && !category.toLowerCase().includes(categoryFilter)) {
                continue;
            }
            if (searchFilter) {
                const haystack = `${message} ${context}`.toLowerCase();
                if (!haystack.includes(searchFilter)) {
                    continue;
                }
            }

            const contextHtml = context ? `<div class="empty">${escapeHtml(context)}</div>` : "";
            rows.push(`<tr><td>${escapeHtml(formatEpochMs(log.ts_ms))}</td><td>${levelBadge(level)}</td><td>${escapeHtml(category)}</td><td>${escapeHtml(message)}${contextHtml}</td></tr>`);
        }
        renderRows(nodes.traceyAgentLogRows, rows, "No log entries match current filters.", 4);
    }

    function renderTraceyAgentDrilldown(analysis) {
        if (!analysis || typeof analysis !== "object") {
            renderTraceyAgentFacts(null);
            renderLineChart(nodes.traceyAgentStatusChart, [], []);
            renderLegend(nodes.traceyAgentStatusLegend, []);
            renderRows(nodes.traceyAgentLogRows, [], "No agent selected.", 4);
            if (nodes.traceyAgentDrilldownTitle) {
                nodes.traceyAgentDrilldownTitle.textContent = "Agent Drilldown";
            }
            if (nodes.traceyAgentDrilldownMeta) {
                nodes.traceyAgentDrilldownMeta.textContent = "Select an agent";
            }
            return;
        }

        const current = analysis.current || {};
        if (nodes.traceyAgentDrilldownTitle) {
            nodes.traceyAgentDrilldownTitle.textContent = `Agent Drilldown • ${current.agent_id || "unknown"}`;
        }
        if (nodes.traceyAgentDrilldownMeta) {
            nodes.traceyAgentDrilldownMeta.textContent = `Updated ${formatEpochMs(analysis.generated_epoch_ms)}`;
            nodes.traceyAgentDrilldownMeta.style.borderColor = statusToneColor(current.status || "unknown");
        }

        const series = Array.isArray(analysis.series) ? analysis.series : [];
        renderLineChart(nodes.traceyAgentStatusChart, series, [
            { key: "healthy", color: "#22c55e" },
            { key: "degraded", color: "#f59e0b" },
            { key: "offline", color: "#ef4444" },
            { key: "avg_query_failures", color: "#94a3b8" }
        ]);
        const latest = series.length ? series[series.length - 1] : {};
        renderLegend(nodes.traceyAgentStatusLegend, [
            { label: "Healthy", color: "#22c55e", value: parseNumber(latest.healthy, 0) },
            { label: "Degraded", color: "#f59e0b", value: parseNumber(latest.degraded, 0) },
            { label: "Offline", color: "#ef4444", value: parseNumber(latest.offline, 0) },
            { label: "Avg Query Failures", color: "#94a3b8", value: parseNumber(latest.avg_query_failures, 0).toFixed(2) }
        ]);
        renderTraceyAgentFacts(analysis);
        traceyState.selectedAgentLogs = Array.isArray(analysis.logs) ? analysis.logs : [];
        renderTraceyAgentLogs();
    }

    async function fetchTraceyAnalytics() {
        const requestSeq = ++traceyInsightsRequestSeq;
        const path = `/tracey/analytics?window_seconds=${encodeURIComponent(String(traceyWindowSeconds()))}&bucket_seconds=${encodeURIComponent(String(traceyBucketSeconds()))}&log_limit=400`;
        if (nodes.traceyInsightsSubtitle) {
            nodes.traceyInsightsSubtitle.textContent = "Loading Tracey analytics...";
        }
        const response = await fetchJson(path);
        if (requestSeq !== traceyInsightsRequestSeq) {
            return;
        }
        if (!response.ok) {
            const message = response.payload && typeof response.payload === "object"
                ? String(response.payload.message || "Unable to load Tracey analytics.")
                : "Unable to load Tracey analytics.";
            if (nodes.traceyInsightsSubtitle) {
                nodes.traceyInsightsSubtitle.textContent = message;
            }
            traceyState.analytics = null;
            renderTraceyAnalyticsOverview(null);
            return;
        }

        const data = responseData(response.payload) || {};
        traceyState.analytics = data;
        const summary = data.summary && typeof data.summary === "object" ? data.summary : {};
        if (nodes.traceyInsightsSubtitle) {
            nodes.traceyInsightsSubtitle.textContent = `${parseNumber(summary.agents_total, 0)} agents, ${parseNumber(summary.current_healthy, 0)} healthy, ${parseNumber(summary.current_offline, 0)} offline • window ${traceyWindowSeconds()}s`;
        }
        renderTraceyAnalyticsOverview(data);

        const availableAgentIds = (Array.isArray(data.agents) ? data.agents : [])
            .map((agent) => String(agent.agent_id || "").trim())
            .filter((id) => id.length > 0);
        if (!availableAgentIds.length) {
            traceyState.selectedAgentId = "";
            traceyState.selectedAgentAnalysis = null;
            renderTraceyAgentDrilldown(null);
            return;
        }

        if (!traceyState.selectedAgentId || !availableAgentIds.includes(traceyState.selectedAgentId)) {
            traceyState.selectedAgentId = availableAgentIds[0];
        }
        await fetchTraceyAgentAnalysis(traceyState.selectedAgentId);
    }

    async function fetchTraceyAgentAnalysis(agentId) {
        const normalizedId = String(agentId || "").trim();
        if (!normalizedId) {
            return;
        }
        traceyState.selectedAgentId = normalizedId;
        const requestSeq = ++traceyAgentRequestSeq;
        if (nodes.traceyAgentDrilldownMeta) {
            nodes.traceyAgentDrilldownMeta.textContent = `Loading ${normalizedId}...`;
        }
        const path = `/tracey/agents/${encodeURIComponent(normalizedId)}/analysis?window_seconds=${encodeURIComponent(String(traceyWindowSeconds()))}&bucket_seconds=${encodeURIComponent(String(traceyBucketSeconds()))}&log_limit=500`;
        const response = await fetchJson(path);
        if (requestSeq !== traceyAgentRequestSeq) {
            return;
        }
        if (!response.ok) {
            const message = response.payload && typeof response.payload === "object"
                ? String(response.payload.message || "Unable to load Tracey agent analysis.")
                : "Unable to load Tracey agent analysis.";
            traceyState.selectedAgentAnalysis = null;
            traceyState.selectedAgentLogs = [];
            renderTraceyAgentDrilldown(null);
            if (nodes.traceyAgentDrilldownMeta) {
                nodes.traceyAgentDrilldownMeta.textContent = message;
            }
            return;
        }
        const data = responseData(response.payload) || {};
        traceyState.selectedAgentAnalysis = data;
        renderTraceyAgentDrilldown(data);
    }

    async function openTraceyInsightsModal(preselectedAgentId = "") {
        closeClusterDetailsModal();
        closeOpenShiftDetailsModal();
        if (preselectedAgentId) {
            traceyState.selectedAgentId = String(preselectedAgentId).trim();
        }
        setTraceyInsightsModalOpen(true);
        await fetchTraceyAnalytics();
    }

    function initializeTraceyInsightsUi() {
        if (nodes.openTraceyInsightsBtn) {
            nodes.openTraceyInsightsBtn.addEventListener("click", () => {
                openTraceyInsightsModal();
            });
        }
        if (nodes.traceyCard) {
            nodes.traceyCard.addEventListener("click", () => {
                openTraceyInsightsModal();
            });
            nodes.traceyCard.addEventListener("keydown", (event) => {
                if (event.key === "Enter" || event.key === " ") {
                    event.preventDefault();
                    openTraceyInsightsModal();
                }
            });
        }
        if (nodes.traceyRows) {
            nodes.traceyRows.addEventListener("click", (event) => {
                const trigger = event.target.closest("button[data-tracey-agent-id]");
                if (!trigger) {
                    return;
                }
                event.preventDefault();
                const agentId = String(trigger.getAttribute("data-tracey-agent-id") || "").trim();
                if (agentId) {
                    openTraceyInsightsModal(agentId);
                }
            });
        }
        if (nodes.traceyAnalyticsAgentRows) {
            nodes.traceyAnalyticsAgentRows.addEventListener("click", (event) => {
                const trigger = event.target.closest("button[data-tracey-agent-analysis]");
                if (!trigger) {
                    return;
                }
                event.preventDefault();
                const agentId = String(trigger.getAttribute("data-tracey-agent-analysis") || "").trim();
                if (agentId) {
                    fetchTraceyAgentAnalysis(agentId);
                }
            });
        }
        if (nodes.traceyInsightsClose) {
            nodes.traceyInsightsClose.addEventListener("click", closeTraceyInsightsModal);
        }
        if (nodes.traceyInsightsModal) {
            nodes.traceyInsightsModal.addEventListener("click", (event) => {
                if (event.target === nodes.traceyInsightsModal) {
                    closeTraceyInsightsModal();
                }
            });
        }
        if (nodes.traceyInsightsRefresh) {
            nodes.traceyInsightsRefresh.addEventListener("click", () => {
                fetchTraceyAnalytics();
            });
        }
        if (nodes.traceyWindowSelect) {
            nodes.traceyWindowSelect.addEventListener("change", () => {
                fetchTraceyAnalytics();
            });
        }
        if (nodes.traceyBucketSelect) {
            nodes.traceyBucketSelect.addEventListener("change", () => {
                fetchTraceyAnalytics();
            });
        }
        if (nodes.traceyAgentLogLevel) {
            nodes.traceyAgentLogLevel.addEventListener("change", renderTraceyAgentLogs);
        }
        if (nodes.traceyAgentLogCategory) {
            nodes.traceyAgentLogCategory.addEventListener("input", renderTraceyAgentLogs);
        }
        if (nodes.traceyAgentLogSearch) {
            nodes.traceyAgentLogSearch.addEventListener("input", renderTraceyAgentLogs);
        }
    }

    function detailPillsHtml(values, emptyText) {
        if (!values.length) {
            return `<span class="empty">${escapeHtml(emptyText)}</span>`;
        }
        return values
            .map((value) => `<span class="detail-pill">${escapeHtml(value)}</span>`)
            .join("");
    }

    function serviceAddressSummary(service) {
        const entries = [];
        const clusterIp = String(service.cluster_ip || "").trim();
        if (clusterIp) {
            entries.push(clusterIp);
        }
        for (const ip of toArray(service.external_ips)) {
            const val = String(ip || "").trim();
            if (val) {
                entries.push(val);
            }
        }
        for (const ingress of toArray(service.load_balancer)) {
            const val = String(ingress || "").trim();
            if (val) {
                entries.push(val);
            }
        }
        if (!entries.length) {
            return "-";
        }
        return entries.join(", ");
    }

    function servicePortSummary(service) {
        const ports = toArray(service.ports);
        if (!ports.length) {
            return "-";
        }
        return ports.map((entry) => {
            const protocol = String(entry.protocol || "TCP");
            const servicePort = Number(entry.port);
            const targetPort = entry.target_port;
            const nodePort = Number(entry.node_port);
            let summary = `${protocol}:${Number.isFinite(servicePort) && servicePort > 0 ? servicePort : "-"}`;
            if (targetPort !== null && targetPort !== undefined && String(targetPort).trim() !== "") {
                summary += ` -> ${targetPort}`;
            }
            if (Number.isFinite(nodePort) && nodePort > 0) {
                summary += ` (node ${nodePort})`;
            }
            return escapeHtml(summary);
        }).join("<br>");
    }

    function renderClusterDetailsModal(details, fallbackName) {
        if (!nodes.clusterDetailsSubtitle || !nodes.clusterDetailsBody) {
            return;
        }

        const clusterName = String(details.name || fallbackName || "unknown");
        const clusterStatus = String(details.status || "Unknown");
        const clusterId = String(details.id || "-");
        const clusterRegion = String(details.region || details.location || "-");
        const clusterSource = String(details.source || "-");
        const networking = details.networking && typeof details.networking === "object" ? details.networking : {};
        const ipsInUse = toArray(networking.ips_in_use).map((value) => String(value)).filter((value) => value.length > 0);
        const portsInUse = toArray(networking.ports_in_use).map((value) => String(value)).filter((value) => value.length > 0);
        const services = toArray(networking.services);

        nodes.clusterDetailsSubtitle.textContent = `${clusterName} • ${clusterStatus}`;

        const serviceRows = services.map((service) => {
            const name = String(service.name || "unknown");
            const namespace = String(service.namespace || "-");
            const type = String(service.type || "-");
            return `<tr><td>${escapeHtml(name)}</td><td>${escapeHtml(namespace)}</td><td>${escapeHtml(type)}</td><td>${escapeHtml(serviceAddressSummary(service))}</td><td>${servicePortSummary(service)}</td></tr>`;
        }).join("");

        nodes.clusterDetailsBody.innerHTML = `
            <div class="cluster-detail-grid">
                <section class="detail-card">
                    <h4>Identity</h4>
                    <p><strong>Name:</strong> ${escapeHtml(clusterName)}</p>
                    <p><strong>ID:</strong> ${escapeHtml(clusterId)}</p>
                    <p><strong>Region:</strong> ${escapeHtml(clusterRegion)}</p>
                    <p><strong>Source:</strong> ${escapeHtml(clusterSource)}</p>
                </section>
                <section class="detail-card">
                    <h4>API Endpoint</h4>
                    <p>${escapeHtml(String(networking.api_server || "-"))}</p>
                </section>
                <section class="detail-card">
                    <h4>IP Addresses In Use</h4>
                    <div class="detail-pills">${detailPillsHtml(ipsInUse, "No IP addresses detected.")}</div>
                </section>
                <section class="detail-card">
                    <h4>Ports In Use</h4>
                    <div class="detail-pills">${detailPillsHtml(portsInUse, "No ports detected.")}</div>
                </section>
            </div>
            <section class="detail-card">
                <h4>Services</h4>
                ${serviceRows
                    ? `<div class="table-wrap"><table class="detail-table"><thead><tr><th>Name</th><th>Namespace</th><th>Type</th><th>Addresses</th><th>Ports</th></tr></thead><tbody>${serviceRows}</tbody></table></div>`
                    : `<p class="empty">No service details reported for this cluster.</p>`}
            </section>
            <section class="detail-card">
                <h4>Raw Payload</h4>
                <pre class="detail-json">${escapeHtml(JSON.stringify(details, null, 2))}</pre>
            </section>
        `;
    }

    function renderOpenShiftDetailsModal(details, fallbackName) {
        if (!nodes.openshiftDetailsSubtitle || !nodes.openshiftDetailsBody) {
            return;
        }

        const clusterName = String(details.name || fallbackName || details.id || "unknown");
        const clusterStatus = String(details.status || "Unknown");
        const clusterId = String(details.id || details.cluster_id || details.uuid || "-");
        const clusterRegion = String(details.region || details.location || "-");
        const provider = String(details.provider || details.platform || details.cloud || "-");
        const org = String(details.organization || details.org || "-");
        const networking = details.networking && typeof details.networking === "object" ? details.networking : {};
        const endpoint = String(networking.endpoint || details.endpoint || details.api || "-");
        const ipsInUse = toArray(networking.ips_in_use).map((value) => String(value)).filter((value) => value.length > 0);
        const portsInUse = toArray(networking.ports_in_use).map((value) => String(value)).filter((value) => value.length > 0);
        const endpointCandidates = toArray(networking.endpoint_candidates).map((value) => String(value)).filter((value) => value.length > 0);

        nodes.openshiftDetailsSubtitle.textContent = `${clusterName} • ${clusterStatus}`;
        nodes.openshiftDetailsBody.innerHTML = `
            <div class="cluster-detail-grid">
                <section class="detail-card">
                    <h4>Identity</h4>
                    <p><strong>Name:</strong> ${escapeHtml(clusterName)}</p>
                    <p><strong>ID:</strong> ${escapeHtml(clusterId)}</p>
                    <p><strong>Region:</strong> ${escapeHtml(clusterRegion)}</p>
                    <p><strong>Provider:</strong> ${escapeHtml(provider)}</p>
                    <p><strong>Organization:</strong> ${escapeHtml(org)}</p>
                </section>
                <section class="detail-card">
                    <h4>Primary Endpoint</h4>
                    <p>${escapeHtml(endpoint)}</p>
                </section>
                <section class="detail-card">
                    <h4>IP Addresses In Use</h4>
                    <div class="detail-pills">${detailPillsHtml(ipsInUse, "No IP addresses detected.")}</div>
                </section>
                <section class="detail-card">
                    <h4>Ports In Use</h4>
                    <div class="detail-pills">${detailPillsHtml(portsInUse, "No ports detected.")}</div>
                </section>
            </div>
            <section class="detail-card">
                <h4>Known Endpoints</h4>
                <div class="detail-pills">${detailPillsHtml(endpointCandidates, "No endpoint candidates reported.")}</div>
            </section>
            <section class="detail-card">
                <h4>Raw Payload</h4>
                <pre class="detail-json">${escapeHtml(JSON.stringify(details, null, 2))}</pre>
            </section>
        `;
    }

    async function openClusterDetailsModal(clusterKey, clusterName) {
        if (!clusterKey || !nodes.clusterDetailsSubtitle || !nodes.clusterDetailsBody) {
            return;
        }
        closeOpenShiftDetailsModal();
        const requestSeq = ++clusterDetailsRequestSeq;
        setClusterDetailsModalOpen(true);
        nodes.clusterDetailsSubtitle.textContent = `Loading details for ${clusterName || clusterKey}...`;
        nodes.clusterDetailsBody.innerHTML = `<p class="empty">Loading cluster details...</p>`;

        const detailsRes = await fetchJson(`/k8s/details/${encodeURIComponent(clusterKey)}`);
        if (requestSeq !== clusterDetailsRequestSeq) {
            return;
        }

        if (!detailsRes.ok) {
            const message = detailsRes.payload && typeof detailsRes.payload === "object"
                ? String(detailsRes.payload.message || "Failed to load cluster details.")
                : "Failed to load cluster details.";
            nodes.clusterDetailsSubtitle.textContent = `${clusterName || clusterKey} • unavailable`;
            nodes.clusterDetailsBody.innerHTML = `<p class="empty">${escapeHtml(message)}</p>`;
            return;
        }

        const detailsPayload = responseData(detailsRes.payload) || {};
        renderClusterDetailsModal(detailsPayload, clusterName || clusterKey);
    }

    async function openOpenShiftDetailsModal(clusterKey, clusterName) {
        if (!clusterKey || !nodes.openshiftDetailsSubtitle || !nodes.openshiftDetailsBody) {
            return;
        }
        closeClusterDetailsModal();
        const requestSeq = ++openshiftDetailsRequestSeq;
        setOpenShiftDetailsModalOpen(true);
        nodes.openshiftDetailsSubtitle.textContent = `Loading details for ${clusterName || clusterKey}...`;
        nodes.openshiftDetailsBody.innerHTML = `<p class="empty">Loading OpenShift cluster details...</p>`;

        const detailsRes = await fetchJson(`/openshift/details/${encodeURIComponent(clusterKey)}`);
        if (requestSeq !== openshiftDetailsRequestSeq) {
            return;
        }

        if (!detailsRes.ok) {
            const message = detailsRes.payload && typeof detailsRes.payload === "object"
                ? String(detailsRes.payload.message || "Failed to load OpenShift cluster details.")
                : "Failed to load OpenShift cluster details.";
            nodes.openshiftDetailsSubtitle.textContent = `${clusterName || clusterKey} • unavailable`;
            nodes.openshiftDetailsBody.innerHTML = `<p class="empty">${escapeHtml(message)}</p>`;
            return;
        }

        const detailsPayload = responseData(detailsRes.payload) || {};
        renderOpenShiftDetailsModal(detailsPayload, clusterName || clusterKey);
    }

    function initializeClusterDetailsUi() {
        if (nodes.k8sRows) {
            nodes.k8sRows.addEventListener("click", (event) => {
                const trigger = event.target.closest("button[data-cluster-key]");
                if (!trigger) {
                    return;
                }
                event.preventDefault();
                const clusterKey = String(trigger.getAttribute("data-cluster-key") || "").trim();
                const clusterName = String(trigger.getAttribute("data-cluster-name") || clusterKey).trim();
                openClusterDetailsModal(clusterKey, clusterName);
            });
        }

        if (nodes.openshiftRows) {
            nodes.openshiftRows.addEventListener("click", (event) => {
                const trigger = event.target.closest("button[data-openshift-key]");
                if (!trigger) {
                    return;
                }
                event.preventDefault();
                const clusterKey = String(trigger.getAttribute("data-openshift-key") || "").trim();
                const clusterName = String(trigger.getAttribute("data-openshift-name") || clusterKey).trim();
                openOpenShiftDetailsModal(clusterKey, clusterName);
            });
        }

        if (nodes.clusterDetailsClose) {
            nodes.clusterDetailsClose.addEventListener("click", closeClusterDetailsModal);
        }

        if (nodes.openshiftDetailsClose) {
            nodes.openshiftDetailsClose.addEventListener("click", closeOpenShiftDetailsModal);
        }

        if (nodes.clusterDetailsModal) {
            nodes.clusterDetailsModal.addEventListener("click", (event) => {
                if (event.target === nodes.clusterDetailsModal) {
                    closeClusterDetailsModal();
                }
            });
        }

        if (nodes.openshiftDetailsModal) {
            nodes.openshiftDetailsModal.addEventListener("click", (event) => {
                if (event.target === nodes.openshiftDetailsModal) {
                    closeOpenShiftDetailsModal();
                }
            });
        }

        window.addEventListener("keydown", (event) => {
            if (event.key !== "Escape") {
                return;
            }
            if (nodes.clusterDetailsModal && !nodes.clusterDetailsModal.hidden) {
                closeClusterDetailsModal();
            }
            if (nodes.openshiftDetailsModal && !nodes.openshiftDetailsModal.hidden) {
                closeOpenShiftDetailsModal();
            }
            if (nodes.traceyInsightsModal && !nodes.traceyInsightsModal.hidden) {
                closeTraceyInsightsModal();
            }
        });
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
            vclusterListRes,
            openshiftClustersRes,
            vmListRes,
            traceyAgentsRes,
            k8sHealthRes,
            connectionRes,
            openshiftResourcesRes
        ] = await Promise.all([
            fetchJson("/k8s/list"),
            fetchJson("/vcluster/list"),
            fetchJson("/openshift/clusters"),
            fetchJson("/vm/list"),
            fetchJson("/tracey/agents"),
            fetchJson("/k8s/healthz"),
            fetchJson("/connections/status"),
            fetchJson("/openshift/resources")
        ]);

        const allResponses = [
            k8sListRes,
            vclusterListRes,
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
            const clusterLookup = cluster.name || cluster.id || "";
            const clusterId = cluster.id || clusterLookup || "unknown";
            const refiner = cluster.refiner && typeof cluster.refiner === "object" ? cluster.refiner : null;
            let status = cluster.status || "Unknown";
            if (refiner) {
                const desired = Number(refiner.desired_replicas);
                const available = Number(refiner.available_replicas);
                if (Number.isFinite(desired) && Number.isFinite(available)) {
                    status = `${status} (Refiner ${available}/${desired})`;
                }
            }
            const clusterButton = clusterLookup
                ? `<button class="cluster-link" type="button" data-cluster-key="${escapeHtml(clusterLookup)}" data-cluster-name="${escapeHtml(name)}" data-cluster-id="${escapeHtml(clusterId)}">${escapeHtml(name)}</button>`
                : escapeHtml(name);
            return `<tr><td>${clusterButton}</td><td>${escapeHtml(region)}</td><td>${statusBadge(status)}</td></tr>`;
        });
        renderRows(nodes.k8sRows, k8sRows, "No Kubernetes clusters detected.", 3);

        const vclusterItems = arrayFromUnknown(vclusterListRes.payload);
        const vclusterHealthy = vclusterItems.filter(vc => {
            const status = String(vc.status || "").toLowerCase();
            return status === "running" || status === "healthy";
        }).length;
        nodes.vclusterMetric.textContent = `${vclusterHealthy} / ${vclusterItems.length}`;
        const vclusterRows = vclusterItems.map((vcluster) => {
            const name = vcluster.name || vcluster.id || "unknown";
            const namespace = vcluster.vcluster_namespace || vcluster.namespace || "-";
            const status = vcluster.status || "Unknown";
            const health = vcluster.health || "Unknown";
            return `<tr><td>${escapeHtml(name)}</td><td>${escapeHtml(namespace)}</td><td>${statusBadge(status)}</td><td>${statusBadge(health)}</td></tr>`;
        });
        renderRows(nodes.vclusterRows, vclusterRows, "No vclusters detected.", 4);

        const openshiftItems = arrayFromUnknown(openshiftClustersRes.payload);
        const openshiftSummary = summarizeRunning(openshiftItems);
        nodes.openshiftMetric.textContent = `${openshiftSummary.running} / ${openshiftSummary.total}`;
        const openshiftRows = openshiftItems.map((cluster) => {
            const name = cluster.name || cluster.id || "unknown";
            const endpoint = cluster.endpoint || cluster.api || "-";
            const status = cluster.status || "Unknown";
            const clusterLookup = cluster.name || cluster.id || "";
            const clusterButton = clusterLookup
                ? `<button class="cluster-link" type="button" data-openshift-key="${escapeHtml(clusterLookup)}" data-openshift-name="${escapeHtml(name)}">${escapeHtml(name)}</button>`
                : escapeHtml(name);
            return `<tr><td>${clusterButton}</td><td>${escapeHtml(endpoint)}</td><td>${statusBadge(status)}</td></tr>`;
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
            const agentId = String(agent.agent_id || "unknown");
            const agentCell = agentId && agentId !== "unknown"
                ? `<button class="cluster-link tracey-agent-link" type="button" data-tracey-agent-id="${escapeHtml(agentId)}">${escapeHtml(agentId)}</button>`
                : escapeHtml(agentId);
            return `<tr><td>${agentCell}</td><td>${escapeHtml(agent.cluster || "-")}</td><td>${statusBadge(status)}</td><td>${escapeHtml(lastSeen)}</td><td>${escapeHtml(agent.version || "-")}</td></tr>`;
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

        if (nodes.traceyInsightsModal && !nodes.traceyInsightsModal.hidden) {
            fetchTraceyAnalytics();
        }

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
    initializeClusterDetailsUi();
    initializeTraceyInsightsUi();
    renderTraceyAgentDrilldown(null);
    refreshDashboard();
    window.setInterval(refreshDashboard, REFRESH_INTERVAL_MS);
})();
