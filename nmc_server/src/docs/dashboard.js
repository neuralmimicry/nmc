(function () {
    const REFRESH_INTERVAL_MS = 15000;
    const TOKEN_STORAGE_KEY = "nmc_dashboard_token";
    const IDENTITY_STORAGE_KEY = "nmc_dashboard_identity";
    const TRACEY_ADAPTIVE_POLICY_KEY = "nmc_tracey_adaptive_policy";
    const monitoringPrefix = "/services/health/monitoring";
    const basePath = window.location.pathname.startsWith(monitoringPrefix)
        ? monitoringPrefix
        : "";

    const nodes = {
        sessionLabel: document.getElementById("sessionLabel"),
        logoutBtn: document.getElementById("logoutBtn"),
        k8sMetric: document.getElementById("k8sMetric"),
        vclusterMetric: document.getElementById("vclusterMetric"),
        openshiftMetric: document.getElementById("openshiftMetric"),
        openstackMetric: document.getElementById("openstackMetric"),
        proxmoxMetric: document.getElementById("proxmoxMetric"),
        vmMetric: document.getElementById("vmMetric"),
        traceyMetric: document.getElementById("traceyMetric"),
        traceyAdaptiveMetric: document.getElementById("traceyAdaptiveMetric"),
        traceyAdaptiveLabel: document.getElementById("traceyAdaptiveLabel"),
        traceyCard: document.getElementById("traceyCard"),
        traceyAdaptiveCard: document.getElementById("traceyAdaptiveCard"),
        traceyAdaptivePolicySelect: document.getElementById("traceyAdaptivePolicySelect"),
        openTraceyInsightsBtn: document.getElementById("openTraceyInsightsBtn"),
        openTraceyAdaptiveBtn: document.getElementById("openTraceyAdaptiveBtn"),
        k8sRows: document.getElementById("k8sRows"),
        vclusterRows: document.getElementById("vclusterRows"),
        openshiftRows: document.getElementById("openshiftRows"),
        openstackRows: document.getElementById("openstackRows"),
        proxmoxRows: document.getElementById("proxmoxRows"),
        traceyRows: document.getElementById("traceyRows"),
        traceyNetworkRows: document.getElementById("traceyNetworkRows"),
        traceyAdaptiveSummaryCards: document.getElementById("traceyAdaptiveSummaryCards"),
        traceyAdaptivePhaseList: document.getElementById("traceyAdaptivePhaseList"),
        traceyAdaptiveRecommendationList: document.getElementById("traceyAdaptiveRecommendationList"),
        traceyAdaptivePlacementRows: document.getElementById("traceyAdaptivePlacementRows"),
        traceyAdaptiveGpuRows: document.getElementById("traceyAdaptiveGpuRows"),
        k8sApiPill: document.getElementById("k8sApiPill"),
        authPill: document.getElementById("authPill"),
        refreshPill: document.getElementById("refreshPill"),
        connStatus: document.getElementById("connStatus"),
        resourceStatus: document.getElementById("resourceStatus"),
        openstackResourceStatus: document.getElementById("openstackResourceStatus"),
        proxmoxResourceStatus: document.getElementById("proxmoxResourceStatus"),
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
        traceyControlStatus: document.getElementById("traceyControlStatus"),
        traceyControlAgent: document.getElementById("traceyControlAgent"),
        traceyControlOverhead: document.getElementById("traceyControlOverhead"),
        traceyControlEnabled: document.getElementById("traceyControlEnabled"),
        traceyControlDeepDive: document.getElementById("traceyControlDeepDive"),
        traceyControlTmr: document.getElementById("traceyControlTmr"),
        traceyControlForceScan: document.getElementById("traceyControlForceScan"),
        traceyControlApply: document.getElementById("traceyControlApply"),
        traceyDeepDiveRefresh: document.getElementById("traceyDeepDiveRefresh"),
        traceyFleetMeta: document.getElementById("traceyFleetMeta"),
        traceyFleetSummaryCards: document.getElementById("traceyFleetSummaryCards"),
        traceyFleetZoneRows: document.getElementById("traceyFleetZoneRows"),
        traceyFleetActionRows: document.getElementById("traceyFleetActionRows"),
        traceyFleetAssessmentSearch: document.getElementById("traceyFleetAssessmentSearch"),
        traceyFleetAssessmentFilter: document.getElementById("traceyFleetAssessmentFilter"),
        traceyFleetAssessmentSort: document.getElementById("traceyFleetAssessmentSort"),
        traceyFleetAssessmentRows: document.getElementById("traceyFleetAssessmentRows"),
        traceyRackExplorerMeta: document.getElementById("traceyRackExplorerMeta"),
        traceyRackCards: document.getElementById("traceyRackCards"),
        traceyRackDetailTitle: document.getElementById("traceyRackDetailTitle"),
        traceyRackDetailMeta: document.getElementById("traceyRackDetailMeta"),
        traceyRackSummaryCards: document.getElementById("traceyRackSummaryCards"),
        traceyRackGpuHeatmap: document.getElementById("traceyRackGpuHeatmap"),
        traceyRackAssessmentSearch: document.getElementById("traceyRackAssessmentSearch"),
        traceyRackAssessmentFilter: document.getElementById("traceyRackAssessmentFilter"),
        traceyRackAssessmentSort: document.getElementById("traceyRackAssessmentSort"),
        traceyRackServerRows: document.getElementById("traceyRackServerRows"),
        traceyServerTelemetryTitle: document.getElementById("traceyServerTelemetryTitle"),
        traceyServerTelemetryMeta: document.getElementById("traceyServerTelemetryMeta"),
        traceyServerSummaryCards: document.getElementById("traceyServerSummaryCards"),
        traceyServerThermals: document.getElementById("traceyServerThermals"),
        traceyServerFans: document.getElementById("traceyServerFans"),
        traceyServerPower: document.getElementById("traceyServerPower"),
        traceyServerDisks: document.getElementById("traceyServerDisks"),
        traceyServerProcesses: document.getElementById("traceyServerProcesses"),
        traceyServerGuardFacts: document.getElementById("traceyServerGuardFacts"),
        traceyServerAssessmentSearch: document.getElementById("traceyServerAssessmentSearch"),
        traceyServerAssessmentFilter: document.getElementById("traceyServerAssessmentFilter"),
        traceyServerAssessmentSort: document.getElementById("traceyServerAssessmentSort"),
        traceyServerAssessmentFacts: document.getElementById("traceyServerAssessmentFacts"),
        traceyServerGpuTiles: document.getElementById("traceyServerGpuTiles"),
        traceyGpuTelemetryTitle: document.getElementById("traceyGpuTelemetryTitle"),
        traceyGpuTelemetryMeta: document.getElementById("traceyGpuTelemetryMeta"),
        traceyGpuSummaryCards: document.getElementById("traceyGpuSummaryCards"),
        traceyGpuSmHeatmap: document.getElementById("traceyGpuSmHeatmap"),
        traceyGpuProbeMix: document.getElementById("traceyGpuProbeMix"),
        traceyGpuFaultCounters: document.getElementById("traceyGpuFaultCounters"),
        traceyGpuExecutionRows: document.getElementById("traceyGpuExecutionRows"),
        traceyGpuFaultRows: document.getElementById("traceyGpuFaultRows"),
        traceyGpuActionRows: document.getElementById("traceyGpuActionRows"),
        traceyDeepDiveChart: document.getElementById("traceyDeepDiveChart"),
        traceyDeepDiveLegend: document.getElementById("traceyDeepDiveLegend"),
        traceyDeepDiveFacts: document.getElementById("traceyDeepDiveFacts"),
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
    let authIdentity = loadIdentity();
    let clusterDetailsRequestSeq = 0;
    let openshiftDetailsRequestSeq = 0;
    let traceyInsightsRequestSeq = 0;
    let traceyAgentRequestSeq = 0;
    let traceyFleetRequestSeq = 0;
    let traceyAssessmentRequestSeq = 0;
    let traceyAdaptiveRequestSeq = 0;
    let traceyRackRequestSeq = 0;
    let traceyServerRequestSeq = 0;
    let traceyGpuRequestSeq = 0;
    const traceyState = {
        analytics: null,
        fleet: null,
        assessmentFleet: null,
        adaptivePolicy: loadTraceyAdaptivePolicy(),
        adaptive: null,
        selectedAgentId: "",
        selectedRack: "",
        selectedRackDetail: null,
        selectedServerTelemetry: null,
        selectedGpuId: "",
        selectedGpuTelemetry: null,
        selectedAgentAnalysis: null,
        selectedAgentLogs: [],
        selectedDeepDive: null
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

    function withBase(path) {
        if (!basePath) {
            return path;
        }
        if (!path.startsWith("/")) {
            return `${basePath}/${path}`;
        }
        return `${basePath}${path}`;
    }

    function loadIdentity() {
        const raw = localStorage.getItem(IDENTITY_STORAGE_KEY) || "";
        if (!raw) {
            return null;
        }
        try {
            const parsed = JSON.parse(raw);
            if (!parsed || typeof parsed !== "object" || !parsed.user) {
                return null;
            }
            return parsed;
        } catch (_error) {
            return null;
        }
    }

    function saveIdentity(identity) {
        if (!identity || typeof identity !== "object" || !identity.user) {
            localStorage.removeItem(IDENTITY_STORAGE_KEY);
            authIdentity = null;
            updateSessionLabel();
            return null;
        }
        authIdentity = {
            user: String(identity.user || "").trim(),
            role: String(identity.role || "").trim(),
            groups: Array.isArray(identity.groups) ? identity.groups : [],
            active_team: identity.active_team && typeof identity.active_team === "object" ? identity.active_team : null,
            team_count: Number(identity.team_count || 0),
            pending_invitation_count: Number(identity.pending_invitation_count || 0),
        };
        localStorage.setItem(IDENTITY_STORAGE_KEY, JSON.stringify(authIdentity));
        updateSessionLabel();
        return authIdentity;
    }

    function normalizeTraceyAdaptivePolicy(value) {
        const normalized = String(value || "").trim().toLowerCase();
        if (["throughput", "risk", "energy"].includes(normalized)) {
            return normalized;
        }
        return "balanced";
    }

    function loadTraceyAdaptivePolicy() {
        return normalizeTraceyAdaptivePolicy(localStorage.getItem(TRACEY_ADAPTIVE_POLICY_KEY) || "");
    }

    function saveTraceyAdaptivePolicy(value) {
        const normalized = normalizeTraceyAdaptivePolicy(value);
        localStorage.setItem(TRACEY_ADAPTIVE_POLICY_KEY, normalized);
        traceyState.adaptivePolicy = normalized;
        if (nodes.traceyAdaptivePolicySelect && nodes.traceyAdaptivePolicySelect.value !== normalized) {
            nodes.traceyAdaptivePolicySelect.value = normalized;
        }
        return normalized;
    }

    function traceyAdaptivePolicyLabel(summary) {
        const policy = normalizeTraceyAdaptivePolicy(summary && summary.policy ? summary.policy : traceyState.adaptivePolicy);
        const label = String(summary && summary.policy_label ? summary.policy_label : formatActionLabel(policy)).trim();
        return label || "Balanced";
    }

    function buildTraceyAdaptivePath(policy = traceyState.adaptivePolicy) {
        const normalized = normalizeTraceyAdaptivePolicy(policy);
        if (normalized === "balanced") {
            return "/tracey/adaptive";
        }
        return `/tracey/adaptive?policy=${encodeURIComponent(normalized)}`;
    }

    function saveToken(token) {
        const trimmed = (token || "").trim();
        if (trimmed) {
            localStorage.setItem(TOKEN_STORAGE_KEY, trimmed);
        } else {
            localStorage.removeItem(TOKEN_STORAGE_KEY);
            localStorage.removeItem(IDENTITY_STORAGE_KEY);
            authIdentity = null;
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
        if (authIdentity && authIdentity.user) {
            const role = authIdentity.role ? ` (${authIdentity.role})` : "";
            const team = authIdentity.active_team?.team_name || authIdentity.active_team?.name || authIdentity.active_team?.team_id;
            const teamLabel = team ? ` · ${team}` : "";
            nodes.sessionLabel.textContent = `${authIdentity.user}${role}${teamLabel}`;
            return;
        }
        nodes.sessionLabel.textContent = authToken ? `Bearer ${maskToken(authToken)}` : "Token not set";
    }

    async function refreshAuthIdentity() {
        if (!authToken) {
            saveIdentity(null);
            return null;
        }
        try {
            const response = await fetch(withBase("/auth/session"), {
                headers: authHeaders(),
                cache: "no-store"
            });
            if (!response.ok) {
                return null;
            }
            const payload = await response.json().catch(() => null);
            return saveIdentity(payload);
        } catch (_error) {
            return null;
        }
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

    async function fetchJson(path, options = {}) {
        try {
            const headers = {
                ...authHeaders(),
                ...(options.headers || {})
            };
            const response = await fetch(path, {
                method: options.method || "GET",
                headers,
                cache: "no-store",
                body: options.body
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
        traceyFleetRequestSeq += 1;
        traceyAssessmentRequestSeq += 1;
        traceyRackRequestSeq += 1;
        traceyServerRequestSeq += 1;
        traceyGpuRequestSeq += 1;
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

    function formatCount(value, fallback = "-") {
        const parsed = Number(value);
        if (!Number.isFinite(parsed)) {
            return fallback;
        }
        return Math.round(parsed).toLocaleString();
    }

    function formatFixed(value, digits = 1, fallback = "-") {
        const parsed = Number(value);
        if (!Number.isFinite(parsed)) {
            return fallback;
        }
        return parsed.toFixed(digits);
    }

    function formatPercentValue(value, digits = 0, fallback = "-") {
        const parsed = Number(value);
        if (!Number.isFinite(parsed)) {
            return fallback;
        }
        return `${parsed.toFixed(digits)}%`;
    }

    function formatRatioPercent(value, digits = 1, fallback = "-") {
        const parsed = Number(value);
        if (!Number.isFinite(parsed)) {
            return fallback;
        }
        return `${(parsed * 100).toFixed(digits)}%`;
    }

    function hasObjectContent(value) {
        return Boolean(value) && typeof value === "object" && !Array.isArray(value) && Object.keys(value).length > 0;
    }

    function getAssessmentSnapshot(source) {
        return source && source.continuum_assessment && typeof source.continuum_assessment === "object"
            ? source.continuum_assessment
            : {};
    }

    function getAssessmentSummary(source) {
        if (source && source.continuum_assessment_summary && typeof source.continuum_assessment_summary === "object") {
            return source.continuum_assessment_summary;
        }
        const snapshot = getAssessmentSnapshot(source);
        return snapshot.summary && typeof snapshot.summary === "object"
            ? snapshot.summary
            : {};
    }

    function getAssessmentCommunication(source) {
        const snapshot = getAssessmentSnapshot(source);
        return snapshot.communication && typeof snapshot.communication === "object"
            ? snapshot.communication
            : {};
    }

    function toneClassForAssessment(summary, communication = {}) {
        const risk = parseNumber(summary.compromise_risk, 0);
        if (parseNumber(communication.auth_failures, 0) > 0 || parseNumber(communication.consecutive_failures, 0) > 0) {
            return "tracey-tone-warn";
        }
        if (risk >= 0.80) {
            return "tracey-tone-bad";
        }
        if (
            risk >= 0.55 ||
            parseNumber(summary.kev_matches, 0) > 0 ||
            parseNumber(communication.report_failures, parseNumber(communication.reports_rejected, 0)) > 0 ||
            parseNumber(communication.plan_fetch_failures, 0) > 0
        ) {
            return "tracey-tone-warn";
        }
        if (
            parseNumber(summary.cve_matches, 0) > 0 ||
            parseNumber(communication.duplicate_reports, 0) > 0 ||
            parseNumber(communication.stale_plan_recoveries, parseNumber(communication.stale_plan_reports, 0)) > 0
        ) {
            return "tracey-tone-neutral";
        }
        return "tracey-tone-ok";
    }

    function formatAssessmentCommOps(communication) {
        return `p ${formatCount(communication.plan_fetch_successes, "0")}/${formatCount(communication.plan_fetch_failures, "0")} • r ${formatCount(communication.report_successes, "0")}/${formatCount(communication.report_failures, "0")}`;
    }

    function formatAssessmentCommFaults(communication) {
        return `dup ${formatCount(communication.duplicate_reports, "0")} • stale ${formatCount(communication.stale_plan_recoveries, formatCount(communication.stale_plan_reports, "0"))} • sem ${formatCount(communication.semantic_failures, "0")}`;
    }

    function formatAssessmentProgress(progress) {
        if (!progress || typeof progress !== "object") {
            return "plan pending";
        }
        const total = parseNumber(progress.slice_count, 0);
        if (total <= 0) {
            return "plan pending";
        }
        return `${formatCount(progress.completed_slice_count, "0")}/${formatCount(total, "0")} slices`;
    }

    function formatAssessmentReportOps(progress) {
        return `acc ${formatCount(progress.reports_accepted, "0")} • rej ${formatCount(progress.reports_rejected, "0")}`;
    }

    function formatAssessmentReportFaults(progress) {
        return `dup ${formatCount(progress.duplicate_reports, "0")} • stale ${formatCount(progress.stale_plan_reports, "0")} • sem ${formatCount(progress.semantic_faults, "0")}`;
    }

    function assessmentStatusText(summary, fallback = "unknown") {
        const value = String(summary.status || summary.assessment_status || "").trim();
        return value || fallback;
    }

    function assessmentActionText(summary, fallback = "-") {
        const raw = String(summary.recommended_action || summary.recommendedAction || "").trim();
        return raw ? formatActionLabel(raw) : fallback;
    }

    function hasAssessmentTelemetry(summary, communication = {}, progress = {}) {
        return hasObjectContent(summary) || hasObjectContent(communication) || hasObjectContent(progress);
    }

    function formatAssessmentLastExchange(communication) {
        const parts = [];
        const operation = String(communication.last_operation || "").trim();
        const disposition = String(communication.last_disposition || "").trim();
        if (operation) {
            parts.push(formatActionLabel(operation));
        }
        if (disposition) {
            parts.push(formatActionLabel(disposition));
        }
        if (communication.last_http_status !== null && communication.last_http_status !== undefined) {
            parts.push(String(communication.last_http_status));
        }
        return parts.length ? parts.join(" • ") : "No exchange";
    }

    function buildAssessmentMetricRows(summary, communication = {}, progress = {}) {
        if (!hasAssessmentTelemetry(summary, communication, progress)) {
            return [];
        }

        const riskTone = toneClassForAssessment(summary, communication);
        const suspiciousTotal = parseNumber(summary.suspicious_processes, 0) +
            parseNumber(summary.suspicious_services, 0) +
            parseNumber(summary.suspicious_modules, 0);
        const signalParts = [
            `guard ${formatRatioPercent(summary.guard_signal, 0, "-")}`,
            `loader ${formatRatioPercent(summary.loader_signal, 0, "-")}`,
            `telemetry ${formatRatioPercent(summary.telemetry_signal, 0, "-")}`
        ].join(" • ");
        const progressValue = hasObjectContent(progress)
            ? formatAssessmentProgress(progress)
            : formatRatioPercent(summary.cycle_completion_pct, 0, "plan pending");
        const progressSubtitle = parseNumber(summary.next_due_slice_ms, 0) > 0
            ? `next ${formatEpochMs(summary.next_due_slice_ms)}`
            : (String(summary.last_error || "").trim() || `fuzzy ${formatRatioPercent(summary.fuzzy_risk, 0, "-")} / ${formatRatioPercent(summary.fuzzy_confidence, 0, "-")}`);
        const exchangeSubtitle = String(communication.last_error || "").trim() || [
            parseNumber(communication.last_success_ms, 0) > 0 ? `ok ${formatEpochMs(communication.last_success_ms)}` : "",
            parseNumber(communication.last_failure_ms, 0) > 0 ? `fail ${formatEpochMs(communication.last_failure_ms)}` : "",
            parseNumber(communication.next_retry_ms, 0) > 0 ? `retry ${formatEpochMs(communication.next_retry_ms)}` : ""
        ].filter(Boolean).join(" • ");

        return [
            {
                label: "Status",
                value: assessmentStatusText(summary),
                subtitle: `risk ${formatRatioPercent(summary.compromise_risk, 0, "-")} • conf ${formatRatioPercent(summary.compromise_confidence, 0, "-")}`,
                tone: riskTone,
                group: "risk",
                priority: 0,
                searchText: `status ${assessmentStatusText(summary)} risk confidence compromise`
            },
            {
                label: "Action",
                value: assessmentActionText(summary),
                subtitle: `cve ${formatCount(summary.cve_matches, "0")} • kev ${formatCount(summary.kev_matches, "0")} • cvss ${formatFixed(summary.highest_cvss, 1, "-")}`,
                tone: riskTone,
                group: "workflow",
                priority: 1,
                searchText: `action remediation cve kev cvss ${summary.recommended_action || ""}`
            },
            {
                label: "Signals",
                value: `${formatRatioPercent(summary.cve_signal, 0, "-")} / ${formatRatioPercent(summary.local_signal, 0, "-")}`,
                subtitle: signalParts,
                tone: "tracey-tone-neutral",
                group: "signals",
                priority: 2,
                searchText: `signals cve local guard loader telemetry fuzzy`
            },
            {
                label: "Inventory",
                value: `${formatCount(summary.inventory_items, "0")} items`,
                subtitle: `${formatCount(summary.suspicious_processes, "0")} proc • ${formatCount(summary.suspicious_services, "0")} svc • ${formatCount(summary.suspicious_modules, "0")} mod`,
                tone: suspiciousTotal > 0 ? "tracey-tone-warn" : "tracey-tone-neutral",
                group: "inventory",
                priority: 3,
                searchText: `inventory package process service module suspicious`
            },
            {
                label: "Progress",
                value: progressValue,
                subtitle: progressSubtitle,
                tone: hasObjectContent(progress) ? "tracey-tone-neutral" : riskTone,
                group: "workflow",
                priority: 4,
                searchText: `progress cycle slice due retry fuzzy error`
            },
            {
                label: "Communication",
                value: formatAssessmentCommOps(communication),
                subtitle: formatAssessmentCommFaults(communication),
                tone: riskTone,
                group: "communication",
                priority: 5,
                searchText: `communication plan report duplicate stale semantic auth failure`
            },
            {
                label: "Last Exchange",
                value: formatAssessmentLastExchange(communication),
                subtitle: exchangeSubtitle || "No assessment exchange recorded.",
                tone: parseNumber(communication.consecutive_failures, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-neutral",
                group: "communication",
                priority: 6,
                searchText: `exchange operation disposition http retry ${communication.last_error || ""}`
            }
        ];
    }

    function normalizedSearchValue(value) {
        return String(value || "").trim().toLowerCase();
    }

    function compareText(left, right) {
        return String(left || "").localeCompare(String(right || ""), undefined, {
            numeric: true,
            sensitivity: "base"
        });
    }

    function toneSortRank(tone) {
        if (tone === "tracey-tone-bad") {
            return 0;
        }
        if (tone === "tracey-tone-warn") {
            return 1;
        }
        if (tone === "tracey-tone-neutral") {
            return 2;
        }
        if (tone === "tracey-tone-ok") {
            return 3;
        }
        return 2;
    }

    function assessmentProgressRatio(progress = {}) {
        const total = parseNumber(progress.slice_count, 0);
        if (total <= 0) {
            return -1;
        }
        return Math.max(0, Math.min(1, parseNumber(progress.completed_slice_count, 0) / total));
    }

    function assessmentQueueCommunicationScore(progress = {}) {
        return parseNumber(progress.reports_rejected, 0) * 8 +
            parseNumber(progress.semantic_faults, 0) * 6 +
            parseNumber(progress.stale_plan_reports, 0) * 4 +
            parseNumber(progress.duplicate_reports, 0) * 2 +
            (String(progress.last_error || "").trim() ? 1 : 0);
    }

    function assessmentCommunicationScore(communication = {}) {
        return parseNumber(communication.auth_failures, 0) * 12 +
            parseNumber(communication.consecutive_failures, 0) * 10 +
            parseNumber(communication.semantic_failures, 0) * 8 +
            parseNumber(communication.report_failures, 0) * 6 +
            parseNumber(communication.plan_fetch_failures, 0) * 5 +
            parseNumber(communication.stale_plan_recoveries, 0) * 3 +
            parseNumber(communication.duplicate_reports, 0) +
            (String(communication.last_error || "").trim() ? 1 : 0);
    }

    function assessmentFilterMatches(mode, summary = {}, communication = {}, progress = {}) {
        const risk = parseNumber(summary.compromise_risk, 0);
        const kevMatches = parseNumber(summary.kev_matches, 0);
        const hasTelemetry = hasAssessmentTelemetry(summary, communication, progress);
        const commScore = hasObjectContent(communication)
            ? assessmentCommunicationScore(communication)
            : assessmentQueueCommunicationScore(progress);
        const totalSlices = parseNumber(progress.slice_count, 0);
        const completedSlices = parseNumber(progress.completed_slice_count, 0);
        const hasCommError = String(communication.last_error || progress.last_error || "").trim().length > 0;

        switch (mode) {
        case "compromised":
            return risk >= 0.80;
        case "elevated":
            return (risk >= 0.55 && risk < 0.80) || (risk < 0.80 && kevMatches > 0);
        case "communication":
            return commScore > 0 || hasCommError;
        case "in_progress":
            return totalSlices > 0 && completedSlices < totalSlices;
        case "no_assessment":
            return !hasTelemetry;
        case "warnings":
            return risk >= 0.55 || kevMatches > 0 || commScore > 0 || hasCommError;
        default:
            return true;
        }
    }

    function assessmentQueueSearchText(agent) {
        const progress = agent.progress && typeof agent.progress === "object" ? agent.progress : {};
        return normalizedSearchValue([
            agent.agent_id,
            agent.host,
            agent.rack,
            agent.zone,
            agent.assessment_status,
            agent.recommended_action,
            agent.status,
            progress.last_disposition,
            progress.last_error
        ].join(" "));
    }

    function rackAssessmentSearchText(server) {
        const assessmentSummary = getAssessmentSummary(server);
        const assessmentCommunication = getAssessmentCommunication(server);
        return normalizedSearchValue([
            server.agent_id,
            server.host,
            server.status,
            assessmentStatusText(assessmentSummary),
            assessmentActionText(assessmentSummary),
            assessmentSummary.cve_matches,
            assessmentSummary.kev_matches,
            assessmentCommunication.last_error,
            assessmentCommunication.last_disposition
        ].join(" "));
    }

    function filteredAndSortedAssessmentQueueAgents(agents) {
        const search = normalizedSearchValue(nodes.traceyFleetAssessmentSearch ? nodes.traceyFleetAssessmentSearch.value : "");
        const filter = String(nodes.traceyFleetAssessmentFilter ? nodes.traceyFleetAssessmentFilter.value : "all");
        const sort = String(nodes.traceyFleetAssessmentSort ? nodes.traceyFleetAssessmentSort.value : "risk_desc");
        const filtered = (Array.isArray(agents) ? agents : []).filter((agent) => {
            if (search && !assessmentQueueSearchText(agent).includes(search)) {
                return false;
            }
            return assessmentFilterMatches(filter, agent, {}, agent.progress || {});
        });

        filtered.sort((left, right) => {
            const leftProgress = left.progress && typeof left.progress === "object" ? left.progress : {};
            const rightProgress = right.progress && typeof right.progress === "object" ? right.progress : {};
            const leftRisk = parseNumber(left.compromise_risk, 0);
            const rightRisk = parseNumber(right.compromise_risk, 0);
            const leftComm = assessmentQueueCommunicationScore(leftProgress);
            const rightComm = assessmentQueueCommunicationScore(rightProgress);
            const leftProgressRatio = assessmentProgressRatio(leftProgress);
            const rightProgressRatio = assessmentProgressRatio(rightProgress);
            const leftHost = String(left.host || left.agent_id || "");
            const rightHost = String(right.host || right.agent_id || "");

            if (sort === "host_asc") {
                return compareText(leftHost, rightHost) || compareText(left.agent_id, right.agent_id);
            }
            if (sort === "progress_desc") {
                return (rightProgressRatio - leftProgressRatio) ||
                    (rightRisk - leftRisk) ||
                    compareText(leftHost, rightHost);
            }
            if (sort === "communication_desc") {
                return (rightComm - leftComm) ||
                    (rightRisk - leftRisk) ||
                    compareText(leftHost, rightHost);
            }
            return (rightRisk - leftRisk) ||
                (rightComm - leftComm) ||
                compareText(leftHost, rightHost);
        });
        return filtered;
    }

    function filteredAndSortedRackAssessmentServers(servers) {
        const search = normalizedSearchValue(nodes.traceyRackAssessmentSearch ? nodes.traceyRackAssessmentSearch.value : "");
        const filter = String(nodes.traceyRackAssessmentFilter ? nodes.traceyRackAssessmentFilter.value : "all");
        const sort = String(nodes.traceyRackAssessmentSort ? nodes.traceyRackAssessmentSort.value : "risk_desc");
        const filtered = (Array.isArray(servers) ? servers : []).filter((server) => {
            const assessmentSummary = getAssessmentSummary(server);
            const assessmentCommunication = getAssessmentCommunication(server);
            if (search && !rackAssessmentSearchText(server).includes(search)) {
                return false;
            }
            return assessmentFilterMatches(filter, assessmentSummary, assessmentCommunication, {});
        });

        filtered.sort((left, right) => {
            const leftSummary = getAssessmentSummary(left);
            const rightSummary = getAssessmentSummary(right);
            const leftCommunication = getAssessmentCommunication(left);
            const rightCommunication = getAssessmentCommunication(right);
            const leftRisk = parseNumber(leftSummary.compromise_risk, 0);
            const rightRisk = parseNumber(rightSummary.compromise_risk, 0);
            const leftComm = assessmentCommunicationScore(leftCommunication);
            const rightComm = assessmentCommunicationScore(rightCommunication);
            const leftHost = String(left.host || left.agent_id || "");
            const rightHost = String(right.host || right.agent_id || "");
            const leftSeen = parseNumber(left.last_seen_seconds_ago, Number.MAX_SAFE_INTEGER);
            const rightSeen = parseNumber(right.last_seen_seconds_ago, Number.MAX_SAFE_INTEGER);

            if (sort === "host_asc") {
                return compareText(leftHost, rightHost) || compareText(left.agent_id, right.agent_id);
            }
            if (sort === "last_seen_recent") {
                return (leftSeen - rightSeen) ||
                    (rightRisk - leftRisk) ||
                    compareText(leftHost, rightHost);
            }
            if (sort === "communication_desc") {
                return (rightComm - leftComm) ||
                    (rightRisk - leftRisk) ||
                    compareText(leftHost, rightHost);
            }
            return (rightRisk - leftRisk) ||
                (rightComm - leftComm) ||
                compareText(leftHost, rightHost);
        });
        return filtered;
    }

    function filteredAndSortedServerAssessmentMetricRows(rows) {
        const base = (Array.isArray(rows) ? rows : []).map((row, index) => ({ ...row, _index: index }));
        const search = normalizedSearchValue(nodes.traceyServerAssessmentSearch ? nodes.traceyServerAssessmentSearch.value : "");
        const filter = String(nodes.traceyServerAssessmentFilter ? nodes.traceyServerAssessmentFilter.value : "all");
        const sort = String(nodes.traceyServerAssessmentSort ? nodes.traceyServerAssessmentSort.value : "priority");

        const filtered = base.filter((row) => {
            const haystack = normalizedSearchValue([
                row.label,
                row.value,
                row.subtitle,
                row.searchText
            ].join(" "));
            if (search && !haystack.includes(search)) {
                return false;
            }
            if (filter === "warnings") {
                return row.tone === "tracey-tone-bad" || row.tone === "tracey-tone-warn";
            }
            if (filter === "communication") {
                return row.group === "communication";
            }
            if (filter === "signals") {
                return row.group === "signals";
            }
            if (filter === "inventory") {
                return row.group === "inventory";
            }
            if (filter === "workflow") {
                return row.group === "workflow" || row.group === "risk";
            }
            return true;
        });

        filtered.sort((left, right) => {
            if (sort === "label") {
                return compareText(left.label, right.label) || (left._index - right._index);
            }
            if (sort === "tone") {
                return (toneSortRank(left.tone) - toneSortRank(right.tone)) ||
                    ((left.priority || 0) - (right.priority || 0)) ||
                    compareText(left.label, right.label);
            }
            return ((left.priority || 0) - (right.priority || 0)) ||
                compareText(left.label, right.label) ||
                (left._index - right._index);
        });

        return {
            rows: filtered.map(({ _index, ...row }) => row),
            emptyMessage: base.length > 0 && filtered.length === 0
                ? "No assessment metrics match current filters."
                : "No assessment snapshot loaded."
        };
    }

    function collectAssessmentAggregate(items) {
        const aggregate = {
            agentCount: 0,
            compromised: 0,
            elevated: 0,
            cveMatches: 0,
            kevMatches: 0,
            maxRisk: 0,
            reportsRejected: 0,
            duplicateReports: 0,
            stalePlanRecoveries: 0,
            semanticFailures: 0,
            authFailures: 0,
            consecutiveFailures: 0
        };
        for (const item of Array.isArray(items) ? items : []) {
            const summary = getAssessmentSummary(item);
            const communication = getAssessmentCommunication(item);
            const risk = parseNumber(summary.compromise_risk, 0);
            aggregate.agentCount += 1;
            aggregate.cveMatches += parseNumber(summary.cve_matches, 0);
            aggregate.kevMatches += parseNumber(summary.kev_matches, 0);
            aggregate.maxRisk = Math.max(aggregate.maxRisk, risk);
            if (risk >= 0.80) {
                aggregate.compromised += 1;
            } else if (risk >= 0.55) {
                aggregate.elevated += 1;
            }
            aggregate.reportsRejected += parseNumber(communication.report_failures, 0);
            aggregate.duplicateReports += parseNumber(communication.duplicate_reports, 0);
            aggregate.stalePlanRecoveries += parseNumber(communication.stale_plan_recoveries, 0);
            aggregate.semanticFailures += parseNumber(communication.semantic_failures, 0);
            aggregate.authFailures += parseNumber(communication.auth_failures, 0);
            aggregate.consecutiveFailures += parseNumber(communication.consecutive_failures, 0);
        }
        return aggregate;
    }

    function collectAssessmentQueueAggregate(items) {
        const aggregate = {
            agentCount: 0,
            compromised: 0,
            elevated: 0,
            cveMatches: 0,
            kevMatches: 0,
            maxRisk: 0
        };
        for (const item of Array.isArray(items) ? items : []) {
            const risk = parseNumber(item.compromise_risk, 0);
            aggregate.agentCount += 1;
            aggregate.cveMatches += parseNumber(item.cve_matches, 0);
            aggregate.kevMatches += parseNumber(item.kev_matches, 0);
            aggregate.maxRisk = Math.max(aggregate.maxRisk, risk);
            if (risk >= 0.80) {
                aggregate.compromised += 1;
            } else if (risk >= 0.55) {
                aggregate.elevated += 1;
            }
        }
        return aggregate;
    }

    function formatBytes(value, fallback = "-") {
        let parsed = Number(value);
        if (!Number.isFinite(parsed) || parsed < 0) {
            return fallback;
        }
        const units = ["B", "KB", "MB", "GB", "TB", "PB"];
        let unitIndex = 0;
        while (parsed >= 1024 && unitIndex < units.length - 1) {
            parsed /= 1024;
            unitIndex += 1;
        }
        const digits = parsed >= 100 || unitIndex === 0 ? 0 : 1;
        return `${parsed.toFixed(digits)} ${units[unitIndex]}`;
    }

    function formatBytesRate(value, fallback = "-") {
        const formatted = formatBytes(value, "");
        return formatted ? `${formatted}/s` : fallback;
    }

    function formatPower(value, fallback = "-") {
        const parsed = Number(value);
        if (!Number.isFinite(parsed)) {
            return fallback;
        }
        return `${parsed.toFixed(parsed >= 100 ? 0 : 1)} W`;
    }

    function formatTemperature(value, fallback = "-") {
        const parsed = Number(value);
        if (!Number.isFinite(parsed)) {
            return fallback;
        }
        return `${parsed.toFixed(parsed >= 100 ? 0 : 1)} C`;
    }

    function formatClockMHz(value, fallback = "-") {
        const parsed = Number(value);
        if (!Number.isFinite(parsed)) {
            return fallback;
        }
        return `${Math.round(parsed).toLocaleString()} MHz`;
    }

    function formatDurationMs(value, fallback = "-") {
        const parsed = Number(value);
        if (!Number.isFinite(parsed) || parsed < 0) {
            return fallback;
        }
        if (parsed < 1) {
            return `${(parsed * 1000).toFixed(0)} us`;
        }
        if (parsed < 1000) {
            return `${parsed.toFixed(parsed >= 100 ? 0 : 1)} ms`;
        }
        return `${(parsed / 1000).toFixed(1)} s`;
    }

    function toneClassFromStatus(status) {
        const normalized = normalizeStatus(status);
        if (normalized === "ok") {
            return "tracey-tone-ok";
        }
        if (normalized === "warn") {
            return "tracey-tone-warn";
        }
        if (normalized === "bad") {
            return "tracey-tone-bad";
        }
        return "tracey-tone-neutral";
    }

    function toneClassFromSeverity(severity) {
        const value = String(severity || "").trim().toLowerCase();
        if (["critical", "high", "error", "offline", "quarantined", "condemned", "fail", "failed"].includes(value)) {
            return "tracey-tone-bad";
        }
        if (["warn", "warning", "medium", "suspect", "degraded", "timeout", "deep_test", "deep-test"].includes(value)) {
            return "tracey-tone-warn";
        }
        if (["ok", "healthy", "pass", "nominal", "low"].includes(value)) {
            return "tracey-tone-ok";
        }
        return "tracey-tone-neutral";
    }

    function toneClassForGpu(gpu) {
        const guardState = String(gpu.guard_state || gpu.state || "").trim().toLowerCase();
        if (guardState) {
            return toneClassFromSeverity(guardState);
        }
        const probeFailures = parseNumber(gpu.probe_fail_count, 0) + parseNumber(gpu.probe_error_count, 0);
        const temperature = parseNumber(gpu.temp_c, -1);
        if (temperature >= 88) {
            return "tracey-tone-bad";
        }
        if (probeFailures > 0 || temperature >= 82) {
            return "tracey-tone-warn";
        }
        return "tracey-tone-ok";
    }

    function renderStatCards(node, cards, emptyMessage = "No telemetry available.") {
        if (!node) {
            return;
        }
        if (!Array.isArray(cards) || !cards.length) {
            node.innerHTML = `<p class="empty">${escapeHtml(emptyMessage)}</p>`;
            return;
        }
        node.innerHTML = cards.map((card) => {
            const tone = card && card.tone ? ` ${card.tone}` : "";
            const value = card && card.value !== null && card.value !== undefined && card.value !== "" ? card.value : "-";
            const meta = card && card.meta !== null && card.meta !== undefined && card.meta !== "" ? `<small>${escapeHtml(card.meta)}</small>` : "";
            return `<div class="tracey-stat-card${tone}"><span>${escapeHtml(card && card.label ? card.label : "-")}</span><strong>${escapeHtml(value)}</strong>${meta}</div>`;
        }).join("");
    }

    function renderMetricList(node, rows, emptyMessage = "No metrics available.") {
        if (!node) {
            return;
        }
        if (!Array.isArray(rows) || !rows.length) {
            node.innerHTML = `<p class="empty">${escapeHtml(emptyMessage)}</p>`;
            return;
        }
        node.innerHTML = rows.map((row) => {
            const value = row && row.value !== null && row.value !== undefined && row.value !== "" ? row.value : "-";
            const subtitle = row && row.subtitle !== null && row.subtitle !== undefined && row.subtitle !== "" ? `<small>${escapeHtml(row.subtitle)}</small>` : "";
            const tone = row && row.tone ? ` ${row.tone}` : "";
            return `<div class="tracey-metric-row${tone}"><div class="tracey-metric-copy"><span>${escapeHtml(row && row.label ? row.label : "-")}</span>${subtitle}</div><strong>${escapeHtml(value)}</strong></div>`;
        }).join("");
    }

    function renderSensorList(node, rows, emptyMessage = "No telemetry available.") {
        if (!node) {
            return;
        }
        if (!Array.isArray(rows) || !rows.length) {
            node.innerHTML = `<p class="empty">${escapeHtml(emptyMessage)}</p>`;
            return;
        }
        node.innerHTML = rows.map((row) => {
            const value = row && row.value !== null && row.value !== undefined && row.value !== "" ? row.value : "-";
            const subtitle = row && row.subtitle !== null && row.subtitle !== undefined && row.subtitle !== "" ? `<small>${escapeHtml(row.subtitle)}</small>` : "";
            const tone = row && row.tone ? ` ${row.tone}` : "";
            return `<div class="tracey-sensor-row${tone}"><div class="tracey-sensor-copy"><span>${escapeHtml(row && row.label ? row.label : "-")}</span>${subtitle}</div><strong>${escapeHtml(value)}</strong></div>`;
        }).join("");
    }

    function formatActionLabel(action) {
        return String(action || "-")
            .replace(/_/g, " ")
            .replace(/\b\w/g, (match) => match.toUpperCase());
    }

    function toneClassForAdaptiveStatus(status) {
        const value = String(status || "").trim().toLowerCase();
        if (["ready", "open", "steady", "learning"].includes(value)) {
            return "tracey-tone-ok";
        }
        if (["balanced", "watch", "manual", "standby", "local_only"].includes(value)) {
            return "tracey-tone-neutral";
        }
        if (["active", "degraded", "tight", "ramping"].includes(value)) {
            return "tracey-tone-warn";
        }
        if (["constrained", "avoid", "stale"].includes(value)) {
            return "tracey-tone-bad";
        }
        return "tracey-tone-neutral";
    }

    function renderTraceyAdaptiveOverview(data) {
        const adaptiveData = data && typeof data === "object" ? data : null;
        const summary = adaptiveData && adaptiveData.summary && typeof adaptiveData.summary === "object"
            ? adaptiveData.summary
            : {};
        const plan = adaptiveData && adaptiveData.plan && typeof adaptiveData.plan === "object"
            ? adaptiveData.plan
            : {};
        const ramp = adaptiveData && adaptiveData.ramp && typeof adaptiveData.ramp === "object"
            ? adaptiveData.ramp
            : {};
        const optimize = adaptiveData && adaptiveData.optimize && typeof adaptiveData.optimize === "object"
            ? adaptiveData.optimize
            : {};
        const repeat = adaptiveData && adaptiveData.repeat && typeof adaptiveData.repeat === "object"
            ? adaptiveData.repeat
            : {};
        const recommendations = Array.isArray(adaptiveData && adaptiveData.recommendations)
            ? adaptiveData.recommendations
            : [];
        const placementCandidates = Array.isArray(adaptiveData && adaptiveData.placement_candidates)
            ? adaptiveData.placement_candidates
            : [];
        const gpuCandidates = Array.isArray(adaptiveData && adaptiveData.gpu_candidates)
            ? adaptiveData.gpu_candidates
            : [];

        const mode = String(summary.mode || "unknown");
        const policy = normalizeTraceyAdaptivePolicy(summary.policy || traceyState.adaptivePolicy);
        const policyLabel = traceyAdaptivePolicyLabel(summary);
        const nextAction = String(
            summary.next_action ||
            (recommendations[0] && recommendations[0].action) ||
            ""
        ).trim();

        if (nodes.traceyAdaptivePolicySelect && nodes.traceyAdaptivePolicySelect.value !== policy) {
            nodes.traceyAdaptivePolicySelect.value = policy;
        }

        if (nodes.traceyAdaptiveMetric) {
            nodes.traceyAdaptiveMetric.textContent = hasObjectContent(summary)
                ? formatActionLabel(mode)
                : "--";
        }
        if (nodes.traceyAdaptiveLabel) {
            nodes.traceyAdaptiveLabel.textContent = hasObjectContent(summary)
                ? `${policyLabel} policy • overall ${formatRatioPercent(summary.overall_score, 0, "-")} • place ${formatRatioPercent(summary.placement_score, 0, "-")}`
                : "Plan / Ramp / Optimise / Repeat";
        }

        const cards = hasObjectContent(summary)
            ? [
                {
                    label: "Loop Mode",
                    value: formatActionLabel(mode),
                    meta: nextAction ? `${policyLabel} policy • next ${formatActionLabel(nextAction)}` : `${policyLabel} policy • always-on loop`,
                    tone: toneClassForAdaptiveStatus(mode)
                },
                {
                    label: "Overall / Readiness",
                    value: `${formatRatioPercent(summary.overall_score, 0, "-")} / ${formatRatioPercent(summary.readiness_score, 0, "-")}`,
                    meta: `placement ${formatRatioPercent(summary.placement_score, 0, "-")}`,
                    tone: toneClassForAdaptiveStatus(mode)
                },
                {
                    label: "Ramp Pressure",
                    value: `${formatCount(summary.pressured_agents, "0")} agents`,
                    meta: `req ${formatCount(summary.requested_remote_nodes, "0")} / active ${formatCount(summary.active_remote_nodes, "0")}`,
                    tone: parseNumber(summary.pressured_agents, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-neutral"
                },
                {
                    label: "Placement Targets",
                    value: `${formatCount(summary.preferred_hosts, "0")} / ${formatCount(summary.preferred_gpus, "0")}`,
                    meta: `${policyLabel.toLowerCase()} policy • hosts / gpus preferred`,
                    tone: parseNumber(summary.preferred_hosts, 0) > 0 || parseNumber(summary.preferred_gpus, 0) > 0
                        ? "tracey-tone-ok"
                        : "tracey-tone-neutral"
                },
                {
                    label: "Risk Queue",
                    value: `${formatCount(summary.compromised_agents, "0")} / ${formatCount(summary.elevated_agents, "0")}`,
                    meta: `${formatCount(summary.cve_matches, "0")} cve • ${formatCount(summary.kev_matches, "0")} kev`,
                    tone: parseNumber(summary.compromised_agents, 0) > 0
                        ? "tracey-tone-bad"
                        : (parseNumber(summary.elevated_agents, 0) > 0 || parseNumber(summary.kev_matches, 0) > 0
                            ? "tracey-tone-warn"
                            : "tracey-tone-ok")
                },
                {
                    label: "Feedback Loop",
                    value: `${formatCount(summary.recent_action_count, "0")} actions`,
                    meta: `${formatCount(summary.stale_agents, "0")} stale • ${formatPercentValue(summary.gpu_headroom_pct, 0, "-")} headroom`,
                    tone: parseNumber(summary.stale_agents, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-neutral"
                }
            ]
            : [];
        renderStatCards(nodes.traceyAdaptiveSummaryCards, cards, "No adaptive loop telemetry available.");

        const phaseRows = [
            {
                label: `Plan • ${formatActionLabel(plan.status || "unknown")}`,
                value: formatRatioPercent(plan.score, 0, "-"),
                subtitle: String(plan.headline || "No planning state."),
                tone: toneClassForAdaptiveStatus(plan.status)
            },
            {
                label: `Ramp • ${formatActionLabel(ramp.status || "unknown")}`,
                value: formatRatioPercent(ramp.score, 0, "-"),
                subtitle: String(ramp.headline || "No ramp signal."),
                tone: toneClassForAdaptiveStatus(ramp.status)
            },
            {
                label: `Optimize • ${formatActionLabel(optimize.status || "unknown")}`,
                value: formatRatioPercent(optimize.score, 0, "-"),
                subtitle: String(optimize.headline || "No placement signal."),
                tone: toneClassForAdaptiveStatus(optimize.status)
            },
            {
                label: `Repeat • ${formatActionLabel(repeat.status || "unknown")}`,
                value: formatRatioPercent(repeat.score, 0, "-"),
                subtitle: String(repeat.headline || "No feedback state."),
                tone: toneClassForAdaptiveStatus(repeat.status)
            }
        ];
        renderMetricList(
            nodes.traceyAdaptivePhaseList,
            hasObjectContent(summary) ? phaseRows : [],
            "No adaptive phases available."
        );

        const recommendationRows = recommendations.slice(0, 6).map((item) => {
            const stage = formatActionLabel(item.stage || "repeat");
            const action = formatActionLabel(item.action || "Hold Steady");
            const priority = formatActionLabel(item.priority || "low");
            const target = [item.host, item.rack, item.zone].filter(Boolean).join(" • ");
            let tone = "tracey-tone-neutral";
            if (String(item.priority || "").trim().toLowerCase() === "critical") {
                tone = "tracey-tone-bad";
            } else if (String(item.priority || "").trim().toLowerCase() === "high") {
                tone = "tracey-tone-warn";
            } else if (String(item.priority || "").trim().toLowerCase() === "medium") {
                tone = "tracey-tone-neutral";
            } else {
                tone = "tracey-tone-ok";
            }
            return {
                label: `${stage} • ${action}`,
                value: priority,
                subtitle: [target, String(item.reason || "").trim()].filter(Boolean).join(" • "),
                tone
            };
        });
        renderMetricList(
            nodes.traceyAdaptiveRecommendationList,
            recommendationRows,
            "No adaptive actions recommended."
        );

        if (nodes.traceyAdaptivePlacementRows) {
            const placementRows = placementCandidates.slice(0, 8).map((candidate) => {
                const host = String(candidate.host || candidate.agent_id || "unknown");
                const lane = formatActionLabel(candidate.lane || "watch");
                const score = formatRatioPercent(candidate.placement_score, 0, "-");
                const reason = String(candidate.reason || "-");
                const hostMeta = [candidate.rack, candidate.zone].filter(Boolean).join(" • ");
                return `<tr><td><div class="tracey-cell-stack"><div>${escapeHtml(host)}</div>${hostMeta ? `<div class="empty">${escapeHtml(hostMeta)}</div>` : ""}</div></td><td>${statusBadge(candidate.lane || "watch")}</td><td>${escapeHtml(score)}</td><td>${escapeHtml(reason)}</td></tr>`;
            });
            renderRows(
                nodes.traceyAdaptivePlacementRows,
                placementRows,
                hasObjectContent(summary) ? "No hosts are currently placement-ready." : "No adaptive loop telemetry available.",
                4
            );
        }

        if (nodes.traceyAdaptiveGpuRows) {
            const gpuRows = gpuCandidates.slice(0, 12).map((candidate) => {
                const gpuId = String(candidate.gpu_id || "gpu");
                const host = String(candidate.host || candidate.agent_id || "-");
                const score = formatRatioPercent(candidate.placement_score, 0, "-");
                const signal = String(candidate.reason || "-");
                return `<tr><td>${escapeHtml(gpuId)}</td><td>${escapeHtml(host)}</td><td>${escapeHtml(score)}</td><td>${escapeHtml(signal)}</td></tr>`;
            });
            renderRows(
                nodes.traceyAdaptiveGpuRows,
                gpuRows,
                hasObjectContent(summary) ? "No GPUs are currently placement-ready." : "No adaptive loop telemetry available.",
                4
            );
        }
    }

    function updateTraceyInsightsSubtitle() {
        if (!nodes.traceyInsightsSubtitle) {
            return;
        }
        const analyticsSummary = traceyState.analytics && typeof traceyState.analytics.summary === "object"
            ? traceyState.analytics.summary
            : {};
        const fleetSummary = traceyState.fleet && typeof traceyState.fleet.summary === "object"
            ? traceyState.fleet.summary
            : {};
        const assessmentFleetSummary = traceyState.assessmentFleet && typeof traceyState.assessmentFleet.fleet === "object"
            ? traceyState.assessmentFleet.fleet
            : {};
        const adaptiveSummary = traceyState.adaptive && typeof traceyState.adaptive.summary === "object"
            ? traceyState.adaptive.summary
            : {};
        const agents = formatCount(
            fleetSummary.agents_total,
            formatCount(assessmentFleetSummary.agents_total, formatCount(analyticsSummary.agents_total, "0"))
        );
        const racks = formatCount(fleetSummary.racks_total, formatCount(assessmentFleetSummary.racks_total, "0"));
        const gpus = formatCount(fleetSummary.gpu_total, formatCount(assessmentFleetSummary.gpu_total, "0"));
        const reachable = formatCount(
            fleetSummary.reachable,
            formatCount(assessmentFleetSummary.reachable, formatCount(analyticsSummary.current_healthy, "0"))
        );
        const assessmentProgress = traceyState.assessmentFleet && traceyState.assessmentFleet.progress && typeof traceyState.assessmentFleet.progress === "object"
            ? traceyState.assessmentFleet.progress
            : {};
        const selectedRack = traceyState.selectedRack ? `rack ${traceyState.selectedRack}` : "";
        const parts = [
            `${agents} agents`,
            `${racks} racks`,
            `${gpus} GPUs`,
            `${reachable} reachable`,
            assessmentProgress.completion_pct !== undefined ? `assess ${formatRatioPercent(assessmentProgress.completion_pct, 0)}` : "",
            hasObjectContent(adaptiveSummary) ? `loop ${formatActionLabel(adaptiveSummary.mode || "balanced")} • ${traceyAdaptivePolicyLabel(adaptiveSummary)} policy` : "",
            `window ${traceyWindowSeconds()}s`,
            selectedRack
        ].filter(Boolean);
        nodes.traceyInsightsSubtitle.textContent = parts.join(" • ");
    }

    function parseTriState(value) {
        const normalized = String(value || "").trim().toLowerCase();
        if (normalized === "true") {
            return true;
        }
        if (normalized === "false") {
            return false;
        }
        return null;
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
        const tracey_guard = current.tracey_guard && typeof current.tracey_guard === "object" ? current.tracey_guard : {};
        const loader_threats = current.loader_threats && typeof current.loader_threats === "object" ? current.loader_threats : {};
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
            { key: "Last Error", value: current.last_error || "-" },
            { key: "Tracey Enabled", value: tracey_guard.enabled === true ? "yes" : "no" },
            { key: "Tracey Quarantined", value: String(parseNumber(tracey_guard.quarantined_devices, 0)) },
            { key: "Tracey Failures", value: String(parseNumber(tracey_guard.total_failures, 0)) },
            { key: "Tracey Errors", value: String(parseNumber(tracey_guard.total_errors, 0) + parseNumber(tracey_guard.total_timeouts, 0)) },
            { key: "Remote Fault Support", value: String(parseNumber(tracey_guard.remote_fault_support, 0)) },
            { key: "Loader Threat Providers", value: String(parseNumber(loader_threats.local_provider_count, 0)) },
            { key: "Loader Threat Artifacts", value: String(parseNumber(loader_threats.local_artifact_count, 0)) },
            { key: "Loader Blocked Providers", value: String(parseNumber(loader_threats.blocked_provider_count, 0)) },
            { key: "Loader Blocked Artifacts", value: String(parseNumber(loader_threats.blocked_artifact_count, 0)) },
            { key: "Loader Threat Reporters", value: String(parseNumber(loader_threats.remote_reporters, 0)) }
        ];
        const selectedTelemetry = traceyState.selectedServerTelemetry &&
            String(traceyState.selectedServerTelemetry.agent_id || "").trim() === String(current.agent_id || "").trim()
            ? traceyState.selectedServerTelemetry
            : null;
        if (selectedTelemetry) {
            const assessmentSummary = getAssessmentSummary(selectedTelemetry);
            const assessmentCommunication = getAssessmentCommunication(selectedTelemetry);
            if (hasAssessmentTelemetry(assessmentSummary, assessmentCommunication)) {
                facts.push(
                    {
                        key: "Assessment",
                        value: `${assessmentStatusText(assessmentSummary)} ${formatRatioPercent(assessmentSummary.compromise_risk, 0, "-")}`
                    },
                    {
                        key: "Assessment Action",
                        value: assessmentActionText(assessmentSummary)
                    },
                    {
                        key: "Assessment Matches",
                        value: `${formatCount(assessmentSummary.cve_matches, "0")} cve / ${formatCount(assessmentSummary.kev_matches, "0")} kev`
                    },
                    {
                        key: "Assessment Comm",
                        value: formatAssessmentCommOps(assessmentCommunication)
                    },
                    {
                        key: "Assessment Faults",
                        value: formatAssessmentCommFaults(assessmentCommunication)
                    }
                );
            }
        }
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
            { key: "avg_query_failures", color: "#94a3b8" },
            { key: "avg_tracey_guard_failures", color: "#c2410c" }
        ]);
        const latest = series.length ? series[series.length - 1] : {};
        renderLegend(nodes.traceyAgentStatusLegend, [
            { label: "Healthy", color: "#22c55e", value: parseNumber(latest.healthy, 0) },
            { label: "Degraded", color: "#f59e0b", value: parseNumber(latest.degraded, 0) },
            { label: "Offline", color: "#ef4444", value: parseNumber(latest.offline, 0) },
            { label: "Avg Query Failures", color: "#94a3b8", value: parseNumber(latest.avg_query_failures, 0).toFixed(2) },
            { label: "Avg Tracey Failures", color: "#c2410c", value: parseNumber(latest.avg_tracey_guard_failures, 0).toFixed(2) }
        ]);
        renderTraceyAgentFacts(analysis);
        traceyState.selectedAgentLogs = Array.isArray(analysis.logs) ? analysis.logs : [];
        renderTraceyAgentLogs();
    }

    function syncControlAgentInput() {
        if (!nodes.traceyControlAgent) {
            return;
        }
        const selected = String(traceyState.selectedAgentId || "").trim();
        if (selected && !nodes.traceyControlAgent.value.trim()) {
            nodes.traceyControlAgent.value = selected;
        }
    }

    function renderTraceyDeepDive(payload) {
        if (!nodes.traceyDeepDiveChart || !nodes.traceyDeepDiveFacts) {
            return;
        }
        const deepdive = payload && typeof payload === "object" && payload.deepdive && typeof payload.deepdive === "object"
            ? payload.deepdive
            : payload;
        if (!deepdive || typeof deepdive !== "object") {
            renderLineChart(nodes.traceyDeepDiveChart, [], []);
            renderLegend(nodes.traceyDeepDiveLegend, []);
            nodes.traceyDeepDiveFacts.innerHTML = `<p class="empty">No deep-dive payload available.</p>`;
            if (nodes.traceyControlStatus) {
                nodes.traceyControlStatus.textContent = "No deep-dive data";
            }
            return;
        }

        const timeline = Array.isArray(deepdive.timeline) ? deepdive.timeline : [];
        renderLineChart(nodes.traceyDeepDiveChart, timeline, [
            { key: "probe_fail", color: "#ef4444" },
            { key: "probe_error", color: "#f59e0b" },
            { key: "probe_timeout", color: "#c2410c" },
            { key: "quarantined_devices", color: "#06b6d4" }
        ]);
        const latest = timeline.length ? timeline[timeline.length - 1] : {};
        renderLegend(nodes.traceyDeepDiveLegend, [
            { label: "Probe Fail", color: "#ef4444", value: parseNumber(latest.probe_fail, 0) },
            { label: "Probe Error", color: "#f59e0b", value: parseNumber(latest.probe_error, 0) },
            { label: "Probe Timeout", color: "#c2410c", value: parseNumber(latest.probe_timeout, 0) },
            { label: "Quarantined", color: "#06b6d4", value: parseNumber(latest.quarantined_devices, 0) }
        ]);

        const summary = deepdive.summary && typeof deepdive.summary === "object" ? deepdive.summary : {};
        const control = deepdive.control && typeof deepdive.control === "object" ? deepdive.control : {};
        const facts = [
            { key: "Enabled", value: summary.enabled === true ? "yes" : "no" },
            { key: "Deep Dive", value: summary.deep_dive === true ? "yes" : "no" },
            { key: "Total Devices", value: String(parseNumber(summary.total_devices, 0)) },
            { key: "Quarantined", value: String(parseNumber(summary.quarantined_devices, 0)) },
            { key: "Total Failures", value: String(parseNumber(summary.total_failures, 0)) },
            { key: "Total Errors", value: String(parseNumber(summary.total_errors, 0)) },
            { key: "Total Timeouts", value: String(parseNumber(summary.total_timeouts, 0)) },
            { key: "Remote Fault Support", value: String(parseNumber(summary.remote_fault_support, 0)) },
            { key: "Overhead %", value: parseNumber(control.overhead_budget_pct, parseNumber(summary.overhead_budget_pct, 0)).toFixed(2) }
        ];
        nodes.traceyDeepDiveFacts.innerHTML = facts.map((fact) => {
            return `<div class="tracey-fact"><span>${escapeHtml(fact.key)}</span><strong>${escapeHtml(fact.value)}</strong></div>`;
        }).join("");

        if (nodes.traceyControlStatus) {
            nodes.traceyControlStatus.textContent = `Deep-dive updated ${formatEpochMs(summary.ts_ms)}`;
            nodes.traceyControlStatus.style.borderColor = summary.enabled ? "#22c55e" : "#f59e0b";
        }
    }

    function renderTraceyFleetAssessmentRows() {
        const assessmentData = traceyState.assessmentFleet && typeof traceyState.assessmentFleet === "object"
            ? traceyState.assessmentFleet
            : null;
        const hasAssessmentData = hasObjectContent(assessmentData);
        const agents = Array.isArray(assessmentData && assessmentData.agents) ? assessmentData.agents : [];
        const visibleAgents = filteredAndSortedAssessmentQueueAgents(agents);
        const rows = visibleAgents.map((agent) => {
            const agentId = String(agent.agent_id || "unknown");
            const host = String(agent.host || agentId);
            const location = [agentId !== host ? agentId : "", String(agent.rack || ""), String(agent.zone || "")]
                .filter(Boolean)
                .join(" • ");
            const status = String(agent.assessment_status || "unknown");
            const progress = agent.progress && typeof agent.progress === "object" ? agent.progress : {};
            const progressTiming = String(progress.last_error || "").trim() || [
                parseNumber(progress.last_report_ms, 0) > 0 ? `report ${formatEpochMs(progress.last_report_ms)}` : "",
                parseNumber(progress.last_plan_ms, 0) > 0 ? `plan ${formatEpochMs(progress.last_plan_ms)}` : ""
            ].filter(Boolean).join(" • ");
            const slotText = parseNumber(progress.slot_count, 0) > 0
                ? `slot ${parseNumber(progress.slot_index, 0) + 1}/${formatCount(progress.slot_count, "0")}`
                : "slot pending";
            return `<tr><td><div class="tracey-cell-stack"><button class="cluster-link tracey-agent-link" type="button" data-tracey-assessment-agent="${escapeHtml(agentId)}">${escapeHtml(host)}</button>${location ? `<div class="empty">${escapeHtml(location)}</div>` : ""}</div></td><td><div class="tracey-cell-stack"><div>${statusBadge(status)}</div><div class="empty">${escapeHtml(`risk ${formatRatioPercent(agent.compromise_risk, 0, "-")} • conf ${formatRatioPercent(agent.compromise_confidence, 0, "-")}`)}</div><div class="empty">${escapeHtml(`${assessmentActionText(agent)} • cve ${formatCount(agent.cve_matches, "0")} • kev ${formatCount(agent.kev_matches, "0")}`)}</div></div></td><td><div class="tracey-cell-stack"><div>${escapeHtml(formatAssessmentReportOps(progress))}</div><div class="empty">${escapeHtml(formatAssessmentReportFaults(progress))}</div><div class="empty">${escapeHtml(`${formatActionLabel(progress.last_disposition || "pending")} • v${formatCount(progress.protocol_version, "1")}`)}</div></div></td><td><div class="tracey-cell-stack"><div>${escapeHtml(formatAssessmentProgress(progress))}</div><div class="empty">${escapeHtml(slotText)}</div><div class="empty">${escapeHtml(progressTiming || "Awaiting assessment report.")}</div></div></td></tr>`;
        });
        renderRows(
            nodes.traceyFleetAssessmentRows,
            rows,
            hasAssessmentData
                ? (agents.length ? "No assessments match current filters." : "No agent assessments in the current cycle.")
                : "No assessment telemetry available.",
            4
        );
    }

    function findTraceyFleetAgent(agentId) {
        const normalizedId = String(agentId || "").trim();
        if (!normalizedId || !traceyState.fleet || !Array.isArray(traceyState.fleet.agents)) {
            return null;
        }
        return traceyState.fleet.agents.find((agent) => String(agent.agent_id || "").trim() === normalizedId) || null;
    }

    function renderTraceyFleetOverview(data) {
        const fleetData = data && typeof data === "object" ? data : null;
        const assessmentData = traceyState.assessmentFleet && typeof traceyState.assessmentFleet === "object"
            ? traceyState.assessmentFleet
            : null;
        const adaptiveData = traceyState.adaptive && typeof traceyState.adaptive === "object"
            ? traceyState.adaptive
            : null;
        const hasAssessmentData = hasObjectContent(assessmentData);
        const summary = fleetData && fleetData.summary && typeof fleetData.summary === "object"
            ? fleetData.summary
            : (assessmentData && assessmentData.fleet && typeof assessmentData.fleet === "object" ? assessmentData.fleet : {});
        const adaptiveSummary = adaptiveData && adaptiveData.summary && typeof adaptiveData.summary === "object"
            ? adaptiveData.summary
            : {};
        const assessmentAgents = Array.isArray(assessmentData && assessmentData.agents) ? assessmentData.agents : [];
        const assessmentAggregate = collectAssessmentQueueAggregate(assessmentAgents);
        const assessmentProgress = assessmentData && assessmentData.progress && typeof assessmentData.progress === "object"
            ? assessmentData.progress
            : {};
        const assessmentCommunication = assessmentData && assessmentData.communication && typeof assessmentData.communication === "object"
            ? assessmentData.communication
            : {};
        const mirror = assessmentData && assessmentData.mirror && typeof assessmentData.mirror === "object"
            ? assessmentData.mirror
            : {};
        const generatedEpochMs = parseNumber(
            fleetData ? fleetData.generated_epoch_ms : 0,
            parseNumber(assessmentData ? assessmentData.generated_epoch_ms : 0, 0)
        );

        if (nodes.traceyFleetMeta) {
            const metaParts = [];
            if (generatedEpochMs > 0) {
                metaParts.push(`Updated ${formatEpochMs(generatedEpochMs)}`);
            }
            if (mirror.assessment_protocol_version !== null && mirror.assessment_protocol_version !== undefined) {
                metaParts.push(`assess v${mirror.assessment_protocol_version}`);
            }
            if (mirror.indexed_records !== null && mirror.indexed_records !== undefined) {
                metaParts.push(`${formatCount(mirror.indexed_records, "0")} CVEs`);
            }
            const indexedHead = String(mirror.indexed_head || mirror.head || "").trim();
            if (indexedHead) {
                metaParts.push(`head ${indexedHead.slice(0, 12)}`);
            }
            nodes.traceyFleetMeta.textContent = metaParts.length ? metaParts.join(" • ") : "Fleet telemetry unavailable";
        }

        const cards = [];
        if (hasObjectContent(adaptiveSummary)) {
            const adaptivePolicyLabelText = traceyAdaptivePolicyLabel(adaptiveSummary);
            cards.push(
                {
                    label: "Adaptive Loop",
                    value: formatActionLabel(adaptiveSummary.mode || "balanced"),
                    meta: `${adaptivePolicyLabelText} policy • ${formatRatioPercent(adaptiveSummary.overall_score, 0, "-")} overall • ${formatRatioPercent(adaptiveSummary.placement_score, 0, "-")} place`,
                    tone: toneClassForAdaptiveStatus(adaptiveSummary.mode)
                },
                {
                    label: "Remote Ramp",
                    value: `${formatCount(adaptiveSummary.requested_remote_nodes, "0")} / ${formatCount(adaptiveSummary.active_remote_nodes, "0")}`,
                    meta: `${formatCount(adaptiveSummary.pressured_agents, "0")} pressured agents`,
                    tone: parseNumber(adaptiveSummary.pressured_agents, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-neutral"
                },
                {
                    label: "Placement Loop",
                    value: `${formatCount(adaptiveSummary.preferred_hosts, "0")} / ${formatCount(adaptiveSummary.preferred_gpus, "0")}`,
                    meta: `${adaptivePolicyLabelText.toLowerCase()} policy • ${formatCount(adaptiveSummary.placement_candidate_count, "0")} host • ${formatCount(adaptiveSummary.gpu_candidate_count, "0")} gpu candidates`,
                    tone: parseNumber(adaptiveSummary.constrained_agents, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-ok"
                }
            );
        }
        if (hasObjectContent(summary)) {
            cards.push(
                {
                    label: "Healthy Agents",
                    value: `${formatCount(summary.healthy, "0")} / ${formatCount(summary.agents_total, "0")}`,
                    meta: `${formatCount(summary.reachable, "0")} reachable`,
                    tone: "tracey-tone-ok"
                },
                {
                    label: "Degraded",
                    value: formatCount(summary.degraded, "0"),
                    meta: `${formatCount(summary.offline, "0")} offline`,
                    tone: parseNumber(summary.degraded, 0) > 0 || parseNumber(summary.offline, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-neutral"
                },
                {
                    label: "Racks",
                    value: formatCount(summary.racks_total, "0"),
                    meta: `${formatCount(summary.zones_total, "0")} zones`,
                    tone: "tracey-tone-neutral"
                },
                {
                    label: "GPU Estate",
                    value: formatCount(summary.gpu_total, "0"),
                    meta: formatPercentValue(summary.gpu_utilization_avg_pct, 0),
                    tone: "tracey-tone-ok"
                },
                {
                    label: "GPU Power",
                    value: formatPower(summary.gpu_power_total_w),
                    meta: `max ${formatTemperature(summary.gpu_temperature_max_c)}`,
                    tone: "tracey-tone-neutral"
                },
                {
                    label: "Thermals / Fans",
                    value: `${formatCount(summary.thermal_alerts, "0")} / ${formatCount(summary.fan_alerts, "0")}`,
                    meta: "alerts",
                    tone: parseNumber(summary.thermal_alerts, 0) > 0 || parseNumber(summary.fan_alerts, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-ok"
                },
                {
                    label: "ECC",
                    value: `${formatCount(summary.ecc_corrected_total, "0")} / ${formatCount(summary.ecc_uncorrected_total, "0")}`,
                    meta: "corr / uncorr",
                    tone: parseNumber(summary.ecc_uncorrected_total, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-neutral"
                },
                {
                    label: "Guard Isolation",
                    value: formatCount(summary.tracey_guard_quarantined, "0"),
                    meta: `${formatCount(summary.blocked_loader_providers, "0")} loader blocks`,
                    tone: parseNumber(summary.tracey_guard_quarantined, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-neutral"
                },
                {
                    label: "Autonomy",
                    value: formatCount(summary.autonomy_actions, "0"),
                    meta: `risk ${formatRatioPercent(summary.autonomy_risk_max, 0)}`,
                    tone: parseNumber(summary.autonomy_actions, 0) > 0 ? "tracey-tone-ok" : "tracey-tone-neutral"
                }
            );
        }
        if (hasAssessmentData) {
            cards.push(
                {
                    label: "Assessment Cycle",
                    value: parseNumber(assessmentProgress.expected_slices, 0) > 0
                        ? `${formatCount(assessmentProgress.completed_slices, "0")} / ${formatCount(assessmentProgress.expected_slices, "0")}`
                        : "plan pending",
                    meta: `${formatRatioPercent(assessmentProgress.completion_pct, 0, "0%")} • ${formatCount(assessmentProgress.agents_completed_cycle, "0")} / ${formatCount(assessmentProgress.agents_with_progress, "0")} agents`,
                    tone: parseNumber(assessmentCommunication.reports_rejected, 0) > 0 || parseNumber(assessmentCommunication.semantic_faults, 0) > 0
                        ? "tracey-tone-warn"
                        : (parseNumber(assessmentProgress.expected_slices, 0) > 0 && parseNumber(assessmentProgress.completion_pct, 0) >= 1 ? "tracey-tone-ok" : "tracey-tone-neutral")
                },
                {
                    label: "Compromise Queue",
                    value: `${formatCount(assessmentAggregate.compromised, "0")} isolate / ${formatCount(assessmentAggregate.elevated, "0")} alert`,
                    meta: `max ${formatRatioPercent(assessmentAggregate.maxRisk, 0)} • ${formatCount(assessmentAggregate.cveMatches, "0")} cve / ${formatCount(assessmentAggregate.kevMatches, "0")} kev`,
                    tone: assessmentAggregate.compromised > 0
                        ? "tracey-tone-bad"
                        : (assessmentAggregate.elevated > 0 || assessmentAggregate.kevMatches > 0 ? "tracey-tone-warn" : "tracey-tone-ok")
                },
                {
                    label: "Assessment Comms",
                    value: `${formatCount(assessmentCommunication.reports_rejected, "0")} rej / ${formatCount(assessmentCommunication.duplicate_reports, "0")} dup`,
                    meta: `stale ${formatCount(assessmentCommunication.stale_plan_reports, "0")} • sem ${formatCount(assessmentCommunication.semantic_faults, "0")}`,
                    tone: parseNumber(assessmentCommunication.reports_rejected, 0) > 0 || parseNumber(assessmentCommunication.semantic_faults, 0) > 0
                        ? "tracey-tone-warn"
                        : (parseNumber(assessmentCommunication.duplicate_reports, 0) > 0 || parseNumber(assessmentCommunication.stale_plan_reports, 0) > 0 ? "tracey-tone-neutral" : "tracey-tone-ok")
                }
            );
        }
        renderStatCards(nodes.traceyFleetSummaryCards, cards, "No fleet telemetry available.");

        const zones = Array.isArray(fleetData && fleetData.zone_breakdown) ? fleetData.zone_breakdown : [];
        const zoneRows = zones.map((zone) => {
            const alerts = `${formatCount(zone.thermal_alerts, "0")} t / ${formatCount(zone.fan_alerts, "0")} f / ${formatCount(zone.tracey_guard_quarantined, "0")} q`;
            return `<tr><td>${escapeHtml(String(zone.zone || "unassigned"))}</td><td>${statusBadge(zone.health || "unknown")}</td><td>${escapeHtml(`${formatCount(zone.rack_count, "0")} racks / ${formatCount(zone.agent_count, "0")} agents`)}</td><td>${escapeHtml(formatCount(zone.gpu_count, "0"))}</td><td>${escapeHtml(formatPercentValue(zone.gpu_utilization_avg_pct, 0))}</td><td>${escapeHtml(alerts)}</td></tr>`;
        });
        renderRows(nodes.traceyFleetZoneRows, zoneRows, "No zone telemetry available.", 6);

        const actions = Array.isArray(fleetData && fleetData.recent_actions) ? fleetData.recent_actions.slice(0, 12) : [];
        const actionRows = actions.map((action) => {
            const rack = String(action.rack || action.zone || "-");
            return `<tr><td>${escapeHtml(formatEpochMs(action.ts_ms))}</td><td>${escapeHtml(rack)}</td><td>${escapeHtml(formatActionLabel(action.action || action.category || "-"))}</td><td>${escapeHtml(String(action.detail || "-"))}</td></tr>`;
        });
        renderRows(nodes.traceyFleetActionRows, actionRows, "No autonomous actions recorded.", 4);

        const heatmapByRack = new Map();
        for (const entry of Array.isArray(fleetData && fleetData.gpu_heatmap) ? fleetData.gpu_heatmap : []) {
            heatmapByRack.set(String(entry.rack || ""), Array.isArray(entry.cells) ? entry.cells : []);
        }
        const racks = Array.isArray(fleetData && fleetData.racks) ? fleetData.racks : [];
        if (nodes.traceyRackExplorerMeta) {
            nodes.traceyRackExplorerMeta.textContent = traceyState.selectedRack
                ? `Selected ${traceyState.selectedRack}`
                : (racks.length ? `${formatCount(racks.length, "0")} racks visible` : "No racks available");
        }
        if (nodes.traceyRackCards) {
            if (!racks.length) {
                nodes.traceyRackCards.innerHTML = `<p class="empty">No rack summaries available.</p>`;
            } else {
                nodes.traceyRackCards.innerHTML = racks.map((rack) => {
                    const rackId = String(rack.rack || "unassigned");
                    const previewCells = (heatmapByRack.get(rackId) || []).slice(0, 24);
                    const preview = previewCells.length
                        ? previewCells.map((cell) => {
                            const gpuId = String(cell.gpu_id || cell.gpuId || "gpu");
                            return `<span class="tracey-heat-cell ${toneClassForGpu(cell)}" title="${escapeHtml(gpuId)}"></span>`;
                        }).join("")
                        : `<span class="empty">No GPU tiles</span>`;
                    const activeClass = rackId === traceyState.selectedRack ? " is-active" : "";
                    return `<button class="tracey-rack-card ${toneClassFromStatus(rack.health)}${activeClass}" type="button" data-tracey-rack="${escapeHtml(rackId)}"><div class="tracey-rack-card-header"><strong>${escapeHtml(rackId)}</strong><span>${escapeHtml(String(rack.zone || "unassigned"))}</span></div><div class="tracey-rack-card-meta"><span>${escapeHtml(formatCount(rack.gpu_count, "0"))} GPU</span><span>${escapeHtml(formatPercentValue(rack.gpu_utilization_avg_pct, 0))} util</span><span>${escapeHtml(formatTemperature(rack.gpu_temperature_max_c))}</span><span>${escapeHtml(formatCount(rack.autonomy_actions, "0"))} act</span></div><div class="tracey-heat-preview">${preview}</div></button>`;
                }).join("");
            }
        }
        renderTraceyFleetAssessmentRows();
        updateTraceyInsightsSubtitle();
    }

    function renderTraceyRackDetail(detail) {
        if (!detail || typeof detail !== "object") {
            if (nodes.traceyRackDetailTitle) {
                nodes.traceyRackDetailTitle.textContent = "Rack Detail";
            }
            if (nodes.traceyRackDetailMeta) {
                nodes.traceyRackDetailMeta.textContent = "Select a rack to inspect slot telemetry";
            }
            renderStatCards(nodes.traceyRackSummaryCards, [], "No rack selected.");
            if (nodes.traceyRackGpuHeatmap) {
                nodes.traceyRackGpuHeatmap.innerHTML = `<p class="empty">Select a rack to load its GPU slot map.</p>`;
            }
            renderRows(nodes.traceyRackServerRows, [], "Select a rack to inspect its servers.", 8);
            return;
        }

        const summary = detail.summary && typeof detail.summary === "object" ? detail.summary : {};
        const rackId = String(detail.rack || summary.rack || "unassigned");
        const servers = Array.isArray(detail.servers) ? detail.servers : [];
        const assessmentAggregate = collectAssessmentAggregate(servers);
        if (nodes.traceyRackDetailTitle) {
            nodes.traceyRackDetailTitle.textContent = `Rack Detail • ${rackId}`;
        }
        if (nodes.traceyRackDetailMeta) {
            nodes.traceyRackDetailMeta.textContent = `${String(detail.zone || summary.zone || "unassigned")} • ${formatEpochMs(detail.generated_epoch_ms)}`;
        }

        renderStatCards(nodes.traceyRackSummaryCards, [
            { label: "Health", value: String(summary.health || "unknown"), meta: `${formatCount(summary.reachable_agents, "0")} reachable`, tone: toneClassFromStatus(summary.health) },
            { label: "Servers", value: formatCount(summary.agent_count, "0"), meta: `${formatCount((detail.servers || []).length, "0")} listed`, tone: "tracey-tone-neutral" },
            { label: "GPUs", value: formatCount(summary.gpu_count, "0"), meta: formatPercentValue(summary.gpu_utilization_avg_pct, 0), tone: "tracey-tone-ok" },
            { label: "Temp", value: formatTemperature(summary.gpu_temperature_max_c), meta: formatPower(summary.gpu_power_total_w), tone: "tracey-tone-neutral" },
            { label: "Network", value: formatBytesRate(summary.net_rx_bps), meta: `tx ${formatBytesRate(summary.net_tx_bps)}`, tone: "tracey-tone-neutral" },
            { label: "Alerts", value: `${formatCount(summary.thermal_alerts, "0")} / ${formatCount(summary.fan_alerts, "0")}`, meta: "thermal / fan", tone: parseNumber(summary.thermal_alerts, 0) > 0 || parseNumber(summary.fan_alerts, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-ok" },
            { label: "ECC", value: `${formatCount(summary.ecc_corrected_total, "0")} / ${formatCount(summary.ecc_uncorrected_total, "0")}`, meta: "corr / uncorr", tone: parseNumber(summary.ecc_uncorrected_total, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-neutral" },
            { label: "Autonomy", value: formatCount(summary.autonomy_actions, "0"), meta: `risk ${formatRatioPercent(summary.autonomy_risk_max, 0)}`, tone: parseNumber(summary.autonomy_actions, 0) > 0 ? "tracey-tone-ok" : "tracey-tone-neutral" },
            {
                label: "Assessment Risk",
                value: `${formatCount(assessmentAggregate.compromised, "0")} isolate / ${formatCount(assessmentAggregate.elevated, "0")} alert`,
                meta: `max ${formatRatioPercent(assessmentAggregate.maxRisk, 0)} • ${formatCount(assessmentAggregate.agentCount, "0")} servers`,
                tone: assessmentAggregate.compromised > 0
                    ? "tracey-tone-bad"
                    : (assessmentAggregate.elevated > 0 ? "tracey-tone-warn" : "tracey-tone-ok")
            },
            {
                label: "Assessment Matches",
                value: `${formatCount(assessmentAggregate.cveMatches, "0")} / ${formatCount(assessmentAggregate.kevMatches, "0")}`,
                meta: "cve / kev",
                tone: assessmentAggregate.kevMatches > 0
                    ? "tracey-tone-warn"
                    : (assessmentAggregate.cveMatches > 0 ? "tracey-tone-neutral" : "tracey-tone-ok")
            },
            {
                label: "Assessment Comm",
                value: `${formatCount(assessmentAggregate.reportsRejected, "0")} fail / ${formatCount(assessmentAggregate.duplicateReports, "0")} dup`,
                meta: `stale ${formatCount(assessmentAggregate.stalePlanRecoveries, "0")} • sem ${formatCount(assessmentAggregate.semanticFailures, "0")} • auth ${formatCount(assessmentAggregate.authFailures, "0")}`,
                tone: assessmentAggregate.authFailures > 0 || assessmentAggregate.consecutiveFailures > 0 || assessmentAggregate.semanticFailures > 0 || assessmentAggregate.reportsRejected > 0
                    ? "tracey-tone-warn"
                    : (assessmentAggregate.duplicateReports > 0 || assessmentAggregate.stalePlanRecoveries > 0 ? "tracey-tone-neutral" : "tracey-tone-ok")
            }
        ], "No rack summary available.");

        const heatmap = Array.isArray(detail.gpu_heatmap) ? detail.gpu_heatmap : [];
        if (nodes.traceyRackGpuHeatmap) {
            nodes.traceyRackGpuHeatmap.innerHTML = heatmap.length
                ? heatmap.map((gpu) => {
                    const gpuId = String(gpu.gpu_id || gpu.gpuId || "gpu");
                    const slotIndex = gpu.slot_index !== null && gpu.slot_index !== undefined ? String(gpu.slot_index) : "-";
                    const agentId = String(gpu.agent_id || "");
                    const activeClass = gpuId === traceyState.selectedGpuId ? " is-active" : "";
                    return `<button class="tracey-slot-card ${toneClassForGpu(gpu)}${activeClass}" type="button" data-tracey-server-id="${escapeHtml(agentId)}" data-tracey-gpu-id="${escapeHtml(gpuId)}"><div class="tracey-slot-card-header"><strong>${escapeHtml(gpuId)}</strong><span>slot ${escapeHtml(slotIndex)}</span></div><div class="tracey-slot-meta"><span><em>Host</em><strong>${escapeHtml(String(gpu.host || "-"))}</strong></span><span><em>Util</em><strong>${escapeHtml(formatPercentValue(gpu.util_pct, 0))}</strong></span><span><em>Temp</em><strong>${escapeHtml(formatTemperature(gpu.temp_c))}</strong></span><span><em>Guard</em><strong>${escapeHtml(String(gpu.guard_state || gpu.state || "-"))}</strong></span></div></button>`;
                }).join("")
                : `<p class="empty">No GPU slot telemetry available for this rack.</p>`;
        }

        const visibleServers = filteredAndSortedRackAssessmentServers(servers);
        const serverRows = visibleServers.map((server) => {
            const agentId = String(server.agent_id || "unknown");
            const serverSummary = server.summary && typeof server.summary === "object" ? server.summary : {};
            const assessmentSummary = getAssessmentSummary(server);
            const assessmentCommunication = getAssessmentCommunication(server);
            const assessmentTone = toneClassForAssessment(assessmentSummary, assessmentCommunication);
            const assessmentCell = hasAssessmentTelemetry(assessmentSummary, assessmentCommunication)
                ? `<div class="tracey-cell-stack ${assessmentTone}"><div>${statusBadge(assessmentStatusText(assessmentSummary))}</div><div class="empty">${escapeHtml(`risk ${formatRatioPercent(assessmentSummary.compromise_risk, 0, "-")} • cve ${formatCount(assessmentSummary.cve_matches, "0")} • kev ${formatCount(assessmentSummary.kev_matches, "0")}`)}</div><div class="empty">${escapeHtml(`${assessmentActionText(assessmentSummary)} • ${formatAssessmentCommOps(assessmentCommunication)}`)}</div></div>`
                : `<span class="empty">No assessment</span>`;
            const button = `<button class="cluster-link tracey-agent-link" type="button" data-tracey-server-id="${escapeHtml(agentId)}">${escapeHtml(String(server.host || agentId))}</button>`;
            const alerts = `${formatCount(serverSummary.thermal_alerts, "0")} t / ${formatCount(serverSummary.fan_alerts, "0")} f / ${formatCount((server.tracey_guard_summary || {}).quarantined_devices, "0")} q`;
            return `<tr><td>${button}</td><td>${statusBadge(server.status || "unknown")}</td><td>${escapeHtml(formatCount((server.gpus || []).length, "0"))}</td><td>${escapeHtml(formatPercentValue(serverSummary.gpu_utilization_avg_pct, 0))}</td><td>${escapeHtml(formatTemperature(serverSummary.gpu_temperature_max_c))}</td><td>${escapeHtml(alerts)}</td><td>${assessmentCell}</td><td>${escapeHtml(formatAge(server.last_seen_seconds_ago))}</td></tr>`;
        });
        renderRows(
            nodes.traceyRackServerRows,
            serverRows,
            servers.length ? "No servers match current assessment filters." : "No servers reported for this rack.",
            8
        );
    }

    function renderTraceyServerTelemetry(data) {
        if (!data || typeof data !== "object") {
            if (nodes.traceyServerTelemetryTitle) {
                nodes.traceyServerTelemetryTitle.textContent = "Server Telemetry";
            }
            if (nodes.traceyServerTelemetryMeta) {
                nodes.traceyServerTelemetryMeta.textContent = "Select a server";
            }
            renderStatCards(nodes.traceyServerSummaryCards, [], "No server selected.");
            renderSensorList(nodes.traceyServerThermals, [], "No server selected.");
            renderSensorList(nodes.traceyServerFans, [], "No fan telemetry loaded.");
            renderSensorList(nodes.traceyServerPower, [], "No power telemetry loaded.");
            renderSensorList(nodes.traceyServerDisks, [], "No disk telemetry loaded.");
            renderSensorList(nodes.traceyServerProcesses, [], "No process telemetry loaded.");
            renderMetricList(nodes.traceyServerGuardFacts, [], "No TraceyGuard summary loaded.");
            renderMetricList(nodes.traceyServerAssessmentFacts, [], "No assessment snapshot loaded.");
            if (nodes.traceyServerGpuTiles) {
                nodes.traceyServerGpuTiles.innerHTML = `<p class="empty">No GPU inventory available.</p>`;
            }
            return;
        }

        const server = data.server && typeof data.server === "object" ? data.server : {};
        const ecc = server.ecc && typeof server.ecc === "object" ? server.ecc : {};
        const guard = data.tracey_guard_summary && typeof data.tracey_guard_summary === "object"
            ? data.tracey_guard_summary
            : {};
        const gpus = Array.isArray(data.gpus) ? data.gpus : [];
        const assessmentSnapshot = getAssessmentSnapshot(data);
        const assessmentSummary = getAssessmentSummary(data);
        const assessmentCommunication = getAssessmentCommunication(data);
        const assessmentProgress = assessmentSnapshot.progress && typeof assessmentSnapshot.progress === "object"
            ? assessmentSnapshot.progress
            : {};
        const assessmentTone = toneClassForAssessment(assessmentSummary, assessmentCommunication);
        if (nodes.traceyServerTelemetryTitle) {
            nodes.traceyServerTelemetryTitle.textContent = `Server Telemetry • ${String(data.host || data.agent_id || "unknown")}`;
        }
        if (nodes.traceyServerTelemetryMeta) {
            nodes.traceyServerTelemetryMeta.textContent = `${String(data.rack || "unassigned")} • ${String(data.zone || "unassigned")} • ${formatEpochMs(data.generated_epoch_ms)}`;
        }
        const summaryCards = [
            { label: "Status", value: String(data.status || "unknown"), meta: String(data.agent_id || "-"), tone: toneClassFromStatus(data.status) },
            { label: "CPU", value: formatPercentValue(server.cpu_usage_pct, 0), meta: "host load", tone: "tracey-tone-neutral" },
            { label: "Memory", value: formatPercentValue(server.mem_used_pct, 0), meta: `apps ${formatPercentValue(server.mem_app_used_pct, 0)}`, tone: "tracey-tone-neutral" },
            { label: "Swap", value: formatPercentValue(server.swap_used_pct, 0), meta: "system swap", tone: parseNumber(server.swap_used_pct, 0) >= 50 ? "tracey-tone-warn" : "tracey-tone-neutral" },
            { label: "Network RX", value: formatBytesRate(server.net_rx_bps), meta: `tx ${formatBytesRate(server.net_tx_bps)}`, tone: "tracey-tone-neutral" },
            { label: "GPU Util", value: formatPercentValue(server.gpu_utilization_avg_pct, 0), meta: `${formatCount(gpus.length, "0")} GPUs`, tone: "tracey-tone-ok" },
            { label: "Max Temp", value: formatTemperature(server.gpu_temperature_max_c), meta: formatPower(server.gpu_power_total_w), tone: parseNumber(server.gpu_temperature_max_c, 0) >= 80 ? "tracey-tone-warn" : "tracey-tone-neutral" },
            { label: "ECC", value: `${formatCount(ecc.corrected_total, "0")} / ${formatCount(ecc.uncorrected_total, "0")}`, meta: "corr / uncorr", tone: parseNumber(ecc.uncorrected_total, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-neutral" },
            { label: "Autonomy", value: formatRatioPercent(server.autonomy_risk, 0), meta: server.autonomy_action ? formatActionLabel(server.autonomy_action) : "No action", tone: parseNumber(server.autonomy_risk, 0) >= 0.5 ? "tracey-tone-warn" : "tracey-tone-ok" },
            { label: "Guard", value: formatCount(guard.quarantined_devices, "0"), meta: `${formatCount(guard.total_failures, "0")} fail / ${formatCount(guard.total_errors, "0")} err`, tone: parseNumber(guard.quarantined_devices, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-neutral" }
        ];
        if (hasAssessmentTelemetry(assessmentSummary, assessmentCommunication, assessmentProgress)) {
            summaryCards.push(
                {
                    label: "Assessment Risk",
                    value: formatRatioPercent(assessmentSummary.compromise_risk, 0, "-"),
                    meta: `${assessmentStatusText(assessmentSummary)} • ${assessmentActionText(assessmentSummary)}`,
                    tone: assessmentTone
                },
                {
                    label: "Assessment Comm",
                    value: formatAssessmentCommOps(assessmentCommunication),
                    meta: formatAssessmentCommFaults(assessmentCommunication),
                    tone: assessmentTone
                }
            );
        }
        renderStatCards(nodes.traceyServerSummaryCards, summaryCards, "No server telemetry available.");

        renderSensorList(nodes.traceyServerThermals, (Array.isArray(server.thermal_sensors) ? server.thermal_sensors : []).map((sensor) => ({
            label: String(sensor.label || sensor.name || "thermal"),
            subtitle: String(sensor.sensor_type || sensor.severity || ""),
            value: formatTemperature(sensor.temp_c),
            tone: toneClassFromSeverity(sensor.severity)
        })), "No thermal telemetry loaded.");

        renderSensorList(nodes.traceyServerFans, (Array.isArray(server.fan_sensors) ? server.fan_sensors : []).map((sensor) => ({
            label: String(sensor.label || sensor.name || "fan"),
            subtitle: sensor.pwm_percent !== null && sensor.pwm_percent !== undefined ? `PWM ${formatPercentValue(sensor.pwm_percent, 0)}` : String(sensor.severity || ""),
            value: sensor.rpm !== null && sensor.rpm !== undefined ? `${formatCount(sensor.rpm, "0")} rpm` : formatPercentValue(sensor.pwm_percent, 0),
            tone: toneClassFromSeverity(sensor.severity)
        })), "No fan telemetry loaded.");

        renderSensorList(nodes.traceyServerPower, (Array.isArray(server.power_sensors) ? server.power_sensors : []).map((sensor) => ({
            label: String(sensor.label || sensor.name || "power"),
            subtitle: String(sensor.severity || ""),
            value: formatPower(sensor.power_w),
            tone: toneClassFromSeverity(sensor.severity)
        })), "No power telemetry loaded.");

        renderSensorList(nodes.traceyServerDisks, (Array.isArray(server.disks) ? server.disks : []).map((disk) => ({
            label: String(disk.mount || "/"),
            subtitle: `${formatBytes(disk.used_bytes)} / ${formatBytes(disk.total_bytes)} • r ${formatBytesRate(disk.read_bps)} • w ${formatBytesRate(disk.write_bps)}`,
            value: formatRatioPercent(disk.used_ratio, 0),
            tone: parseNumber(disk.used_ratio, 0) >= 0.85 ? "tracey-tone-warn" : "tracey-tone-neutral"
        })), "No disk telemetry loaded.");

        renderSensorList(nodes.traceyServerProcesses, (Array.isArray(server.processes) ? server.processes : []).map((process) => ({
            label: String(process.name || `pid ${process.pid || "-"}`),
            subtitle: `pid ${process.pid || "-"} • mem ${formatBytes(process.mem_bytes)} • io ${formatBytesRate(process.io_bps)}`,
            value: formatPercentValue(process.cpu_pct, 0),
            tone: parseNumber(process.cpu_pct, 0) >= 70 ? "tracey-tone-warn" : "tracey-tone-neutral"
        })), "No process telemetry loaded.");

        renderMetricList(nodes.traceyServerGuardFacts, [
            { label: "Enabled", value: guard.enabled === true ? "yes" : "no", subtitle: `deep dive ${guard.deep_dive === true ? "on" : "off"}`, tone: guard.enabled === true ? "tracey-tone-ok" : "tracey-tone-neutral" },
            { label: "Quarantined", value: formatCount(guard.quarantined_devices, "0"), subtitle: `${formatCount(guard.healthy_devices, "0")} healthy / ${formatCount(guard.suspect_devices, "0")} suspect`, tone: parseNumber(guard.quarantined_devices, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-neutral" },
            { label: "Failures", value: formatCount(guard.total_failures, "0"), subtitle: `${formatCount(guard.total_errors, "0")} errors / ${formatCount(guard.total_timeouts, "0")} timeouts`, tone: parseNumber(guard.total_failures, 0) > 0 || parseNumber(guard.total_errors, 0) > 0 || parseNumber(guard.total_timeouts, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-neutral" },
            { label: "Remote Support", value: formatCount(guard.remote_fault_support, "0"), subtitle: `${formatFixed(guard.overhead_budget_pct, 1, "-")}% budget`, tone: "tracey-tone-neutral" }
        ], "No TraceyGuard summary loaded.");
        const filteredAssessmentMetrics = filteredAndSortedServerAssessmentMetricRows(
            buildAssessmentMetricRows(assessmentSummary, assessmentCommunication, assessmentProgress)
        );
        renderMetricList(
            nodes.traceyServerAssessmentFacts,
            filteredAssessmentMetrics.rows,
            filteredAssessmentMetrics.emptyMessage
        );

        if (nodes.traceyServerGpuTiles) {
            nodes.traceyServerGpuTiles.innerHTML = gpus.length
                ? gpus.map((gpu) => {
                    const gpuId = String(gpu.gpu_id || gpu.gpuId || "gpu");
                    const activeClass = gpuId === traceyState.selectedGpuId ? " is-active" : "";
                    return `<button class="tracey-slot-card ${toneClassForGpu(gpu)}${activeClass}" type="button" data-tracey-gpu-agent="${escapeHtml(String(data.agent_id || ""))}" data-tracey-gpu-id="${escapeHtml(gpuId)}"><div class="tracey-slot-card-header"><strong>${escapeHtml(gpuId)}</strong><span>${escapeHtml(String(gpu.name || gpu.vendor || ""))}</span></div><div class="tracey-slot-meta"><span><em>Util</em><strong>${escapeHtml(formatPercentValue(gpu.util_pct, 0))}</strong></span><span><em>Temp</em><strong>${escapeHtml(formatTemperature(gpu.temp_c))}</strong></span><span><em>Power</em><strong>${escapeHtml(formatPower(gpu.power_w))}</strong></span><span><em>Memory</em><strong>${escapeHtml(`${formatBytes(gpu.mem_used_bytes)} / ${formatBytes(gpu.mem_total_bytes)}`)}</strong></span></div></button>`;
                }).join("")
                : `<p class="empty">No GPU inventory available.</p>`;
        }
    }

    function renderTraceyGpuTelemetry(data) {
        if (!data || typeof data !== "object") {
            if (nodes.traceyGpuTelemetryTitle) {
                nodes.traceyGpuTelemetryTitle.textContent = "GPU Telemetry";
            }
            if (nodes.traceyGpuTelemetryMeta) {
                nodes.traceyGpuTelemetryMeta.textContent = "Select a GPU";
            }
            renderStatCards(nodes.traceyGpuSummaryCards, [], "No GPU selected.");
            if (nodes.traceyGpuSmHeatmap) {
                nodes.traceyGpuSmHeatmap.innerHTML = `<p class="empty">Select a GPU to inspect per-SM probe activity.</p>`;
            }
            renderMetricList(nodes.traceyGpuProbeMix, [], "No GPU telemetry loaded.");
            renderMetricList(nodes.traceyGpuFaultCounters, [], "No fault counters available.");
            renderRows(nodes.traceyGpuExecutionRows, [], "No recent executions loaded.", 6);
            renderRows(nodes.traceyGpuFaultRows, [], "No fault signatures loaded.", 5);
            renderRows(nodes.traceyGpuActionRows, [], "No remediation history loaded.", 4);
            return;
        }

        const summary = data.summary && typeof data.summary === "object" ? data.summary : {};
        if (nodes.traceyGpuTelemetryTitle) {
            nodes.traceyGpuTelemetryTitle.textContent = `GPU Telemetry • ${String(data.gpu_id || summary.gpu_id || "unknown")}`;
        }
        if (nodes.traceyGpuTelemetryMeta) {
            nodes.traceyGpuTelemetryMeta.textContent = `${String(data.host || "-")} • ${String(data.rack || "unassigned")} • ${formatEpochMs(data.generated_epoch_ms)}`;
        }

        renderStatCards(nodes.traceyGpuSummaryCards, [
            { label: "Guard State", value: formatActionLabel(summary.guard_state || summary.state || "unknown"), meta: String(summary.last_guard_reason || "-"), tone: toneClassForGpu(summary) },
            { label: "Reliability", value: formatFixed(summary.reliability_score, 3, "-"), meta: `${formatCount(summary.consecutive_failures, "0")} consecutive`, tone: parseNumber(summary.reliability_score, 1) < 0.95 ? "tracey-tone-warn" : "tracey-tone-ok" },
            { label: "Utilization", value: formatPercentValue(summary.util_pct, 0), meta: `enc ${formatPercentValue(summary.encoder_util_percent, 0)} / dec ${formatPercentValue(summary.decoder_util_percent, 0)}`, tone: "tracey-tone-ok" },
            { label: "Thermals", value: formatTemperature(summary.temp_c), meta: formatPower(summary.power_w), tone: parseNumber(summary.temp_c, 0) >= 80 ? "tracey-tone-warn" : "tracey-tone-neutral" },
            { label: "Memory", value: `${formatBytes(summary.mem_used_bytes)} / ${formatBytes(summary.mem_total_bytes)}`, meta: formatPercentValue(summary.mem_used_pct, 0), tone: parseNumber(summary.mem_used_pct, 0) >= 90 ? "tracey-tone-warn" : "tracey-tone-neutral" },
            { label: "Clocks", value: formatClockMHz(summary.graphics_clock_mhz), meta: `mem ${formatClockMHz(summary.memory_clock_mhz)}`, tone: "tracey-tone-neutral" },
            { label: "SM Coverage", value: formatCount(summary.sm_count, "0"), meta: `last probe ${formatEpochMs(summary.last_probe_ms)}`, tone: "tracey-tone-neutral" },
            { label: "Probe Failures", value: formatCount(summary.probe_fail_count, "0"), meta: `${formatCount(summary.probe_error_count, "0")} errors`, tone: parseNumber(summary.probe_fail_count, 0) > 0 || parseNumber(summary.probe_error_count, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-ok" },
            { label: "Risk / Confidence", value: `${formatRatioPercent(summary.last_guard_risk, 0)} / ${formatRatioPercent(summary.last_guard_confidence, 0)}`, meta: `${formatCount(summary.ecc_error_count, "0")} ECC`, tone: parseNumber(summary.last_guard_risk, 0) >= 0.6 ? "tracey-tone-warn" : "tracey-tone-neutral" }
        ], "No GPU telemetry available.");

        const smHeatmap = Array.isArray(data.sm_heatmap) ? data.sm_heatmap : [];
        if (nodes.traceyGpuSmHeatmap) {
            nodes.traceyGpuSmHeatmap.innerHTML = smHeatmap.length
                ? smHeatmap.map((cell) => {
                    let tone = "tracey-tone-ok";
                    if (parseNumber(cell.timeout_count, 0) > 0 || parseNumber(cell.fail_count, 0) > 0) {
                        tone = "tracey-tone-bad";
                    } else if (parseNumber(cell.error_count, 0) > 0 || parseNumber(cell.risk_max, 0) >= 0.5) {
                        tone = "tracey-tone-warn";
                    }
                    const executions = formatCount(cell.executions, "0");
                    return `<div class="tracey-sm-cell ${tone}" title="${escapeHtml(`SM ${cell.sm_id} • risk ${formatRatioPercent(cell.risk_max, 0)} • conf ${formatRatioPercent(cell.confidence_avg, 0)}`)}"><span>SM ${escapeHtml(String(cell.sm_id))}</span><strong>${escapeHtml(executions)}x</strong></div>`;
                }).join("")
                : `<p class="empty">No per-SM probe activity available.</p>`;
        }

        renderMetricList(nodes.traceyGpuProbeMix, (Array.isArray(data.probe_mix) ? data.probe_mix : []).map((entry) => ({
            label: formatActionLabel(entry.probe_type || "-"),
            subtitle: "completed executions",
            value: formatCount(entry.count, "0"),
            tone: "tracey-tone-neutral"
        })), "No probe execution mix available.");

        renderMetricList(nodes.traceyGpuFaultCounters, [
            { label: "Local Faults", value: formatCount((data.recent_faults || []).length, "0"), subtitle: "recent fault signatures", tone: (data.recent_faults || []).length ? "tracey-tone-warn" : "tracey-tone-ok" },
            { label: "Remote Faults", value: formatCount((data.remote_faults || []).length, "0"), subtitle: "corroborated by peers", tone: (data.remote_faults || []).length ? "tracey-tone-warn" : "tracey-tone-neutral" },
            { label: "Recovery Actions", value: formatCount((data.recent_actions || []).length, "0"), subtitle: "autonomous remediations", tone: (data.recent_actions || []).length ? "tracey-tone-ok" : "tracey-tone-neutral" }
        ], "No fault counters available.");

        const executionRows = (Array.isArray(data.recent_executions) ? data.recent_executions : []).slice(0, 12).map((execution) => {
            const context = execution.context && typeof execution.context === "object" ? execution.context : {};
            const details = [
                `dur ${formatDurationMs(parseNumber(execution.execution_time_ns, 0) / 1000000)}`,
                context.mem_used_ratio !== undefined ? `mem ${formatRatioPercent(context.mem_used_ratio, 0)}` : "",
                context.ecc_error_count !== undefined ? `ecc ${formatCount(context.ecc_error_count, "0")}` : "",
                context.deep_dive !== undefined ? `deep ${String(context.deep_dive)}` : "",
                context.remote_support !== undefined ? `remote ${formatCount(context.remote_support, "0")}` : ""
            ].filter(Boolean).join(" • ");
            return `<tr><td>${escapeHtml(formatEpochMs(execution.ts_ms))}</td><td>${escapeHtml(formatActionLabel(execution.probe_type))}</td><td>${escapeHtml(String(execution.sm_id))}</td><td>${statusBadge(execution.probe_state)}</td><td>${escapeHtml(`${formatRatioPercent(execution.risk, 0)} / ${formatRatioPercent(execution.confidence, 0)}`)}</td><td>${escapeHtml(details || "-")}</td></tr>`;
        });
        renderRows(nodes.traceyGpuExecutionRows, executionRows, "No recent executions loaded.", 6);

        const faults = [];
        for (const fault of Array.isArray(data.recent_faults) ? data.recent_faults : []) {
            faults.push({ ...fault, scope: "local" });
        }
        for (const fault of Array.isArray(data.remote_faults) ? data.remote_faults : []) {
            faults.push({ ...fault, scope: "remote" });
        }
        faults.sort((left, right) => parseNumber(right.last_seen_ms, 0) - parseNumber(left.last_seen_ms, 0));
        const faultRows = faults.slice(0, 12).map((fault) => {
            const scopeText = String(fault.scope || "-");
            const seen = parseNumber(fault.first_seen_ms, 0) > 0
                ? `${formatEpochMs(fault.first_seen_ms)} -> ${formatEpochMs(fault.last_seen_ms)}`
                : formatEpochMs(fault.last_seen_ms);
            return `<tr><td>${escapeHtml(scopeText)}</td><td>${escapeHtml(formatActionLabel(fault.probe_type))}</td><td>${statusBadge(fault.state || fault.severity || "unknown")}</td><td>${escapeHtml(`${formatRatioPercent(fault.risk, 0)} / ${formatRatioPercent(fault.confidence, 0)}`)}</td><td>${escapeHtml(seen)}</td></tr>`;
        });
        renderRows(nodes.traceyGpuFaultRows, faultRows, "No fault signatures loaded.", 5);

        const actionRows = (Array.isArray(data.recent_actions) ? data.recent_actions : []).slice(0, 12).map((action) => {
            return `<tr><td>${escapeHtml(formatEpochMs(action.ts_ms))}</td><td>${escapeHtml(formatActionLabel(action.action || action.category || "-"))}</td><td>${escapeHtml(String(action.source || action.category || "-"))}</td><td>${escapeHtml(String(action.detail || "-"))}</td></tr>`;
        });
        renderRows(nodes.traceyGpuActionRows, actionRows, "No remediation history loaded.", 4);
    }

    async function fetchTraceyAssessmentFleet() {
        const requestSeq = ++traceyAssessmentRequestSeq;
        if (!traceyState.assessmentFleet) {
            renderRows(nodes.traceyFleetAssessmentRows, [], "Loading assessment telemetry...", 4);
        }
        const response = await fetchJson("/tracey/assessment/fleet");
        if (requestSeq !== traceyAssessmentRequestSeq) {
            return;
        }
        if (!response.ok) {
            traceyState.assessmentFleet = null;
            if (traceyState.fleet) {
                renderTraceyFleetOverview(traceyState.fleet);
            } else {
                renderTraceyFleetAssessmentRows();
                updateTraceyInsightsSubtitle();
            }
            return;
        }

        traceyState.assessmentFleet = responseData(response.payload) || {};
        if (traceyState.fleet) {
            renderTraceyFleetOverview(traceyState.fleet);
        } else {
            renderTraceyFleetAssessmentRows();
            updateTraceyInsightsSubtitle();
        }
    }

    async function fetchTraceyAdaptive() {
        const requestSeq = ++traceyAdaptiveRequestSeq;
        const response = await fetchJson(buildTraceyAdaptivePath());
        if (requestSeq !== traceyAdaptiveRequestSeq) {
            return;
        }
        if (!response.ok) {
            traceyState.adaptive = null;
            renderTraceyAdaptiveOverview(null);
            if (traceyState.fleet) {
                renderTraceyFleetOverview(traceyState.fleet);
            } else {
                updateTraceyInsightsSubtitle();
            }
            return;
        }

        traceyState.adaptive = responseData(response.payload) || {};
        renderTraceyAdaptiveOverview(traceyState.adaptive);
        if (traceyState.fleet) {
            renderTraceyFleetOverview(traceyState.fleet);
        } else {
            updateTraceyInsightsSubtitle();
        }
    }

    async function fetchTraceyFleet() {
        const requestSeq = ++traceyFleetRequestSeq;
        if (nodes.traceyFleetMeta) {
            nodes.traceyFleetMeta.textContent = "Loading fleet telemetry...";
        }
        const response = await fetchJson("/tracey/fleet");
        if (requestSeq !== traceyFleetRequestSeq) {
            return;
        }
        if (!response.ok) {
            traceyState.fleet = null;
            traceyState.selectedRackDetail = null;
            traceyState.selectedServerTelemetry = null;
            traceyState.selectedGpuTelemetry = null;
            renderTraceyFleetOverview(null);
            renderTraceyRackDetail(null);
            renderTraceyServerTelemetry(null);
            renderTraceyGpuTelemetry(null);
            renderTraceyAgentFacts(traceyState.selectedAgentAnalysis);
            return;
        }

        const data = responseData(response.payload) || {};
        traceyState.fleet = data;
        renderTraceyFleetOverview(data);

        const racks = Array.isArray(data.racks) ? data.racks : [];
        if (!racks.length) {
            traceyState.selectedRack = "";
            traceyState.selectedRackDetail = null;
            traceyState.selectedServerTelemetry = null;
            traceyState.selectedGpuTelemetry = null;
            renderTraceyRackDetail(null);
            renderTraceyServerTelemetry(null);
            renderTraceyGpuTelemetry(null);
            renderTraceyAgentFacts(traceyState.selectedAgentAnalysis);
            return;
        }

        let desiredRack = String(traceyState.selectedRack || "").trim();
        const fleetAgent = findTraceyFleetAgent(traceyState.selectedAgentId);
        if (!desiredRack && fleetAgent) {
            desiredRack = String(fleetAgent.rack || "").trim();
        }
        if (!desiredRack || !racks.some((rack) => String(rack.rack || "").trim() === desiredRack)) {
            desiredRack = String(racks[0].rack || "").trim();
        }
        if (desiredRack) {
            await fetchTraceyRackDetail(desiredRack);
        }
    }

    async function fetchTraceyRackDetail(rackId, options = {}) {
        const normalizedRack = String(rackId || "").trim();
        if (!normalizedRack) {
            traceyState.selectedRack = "";
            traceyState.selectedRackDetail = null;
            traceyState.selectedServerTelemetry = null;
            traceyState.selectedGpuTelemetry = null;
            renderTraceyRackDetail(null);
            renderTraceyServerTelemetry(null);
            renderTraceyGpuTelemetry(null);
            renderTraceyAgentFacts(traceyState.selectedAgentAnalysis);
            return;
        }

        traceyState.selectedRack = normalizedRack;
        renderTraceyFleetOverview(traceyState.fleet);
        const requestSeq = ++traceyRackRequestSeq;
        if (nodes.traceyRackDetailMeta) {
            nodes.traceyRackDetailMeta.textContent = `Loading ${normalizedRack}...`;
        }
        const response = await fetchJson(`/tracey/racks/${encodeURIComponent(normalizedRack)}`);
        if (requestSeq !== traceyRackRequestSeq) {
            return;
        }
        if (!response.ok) {
            traceyState.selectedRackDetail = null;
            traceyState.selectedServerTelemetry = null;
            traceyState.selectedGpuTelemetry = null;
            renderTraceyRackDetail(null);
            renderTraceyServerTelemetry(null);
            renderTraceyGpuTelemetry(null);
            renderTraceyAgentFacts(traceyState.selectedAgentAnalysis);
            return;
        }

        const data = responseData(response.payload) || {};
        traceyState.selectedRackDetail = data;
        renderTraceyRackDetail(data);
        updateTraceyInsightsSubtitle();

        if (options.skipServer === true) {
            return;
        }

        const servers = Array.isArray(data.servers) ? data.servers : [];
        if (!servers.length) {
            traceyState.selectedServerTelemetry = null;
            traceyState.selectedGpuTelemetry = null;
            renderTraceyServerTelemetry(null);
            renderTraceyGpuTelemetry(null);
            renderTraceyAgentFacts(traceyState.selectedAgentAnalysis);
            return;
        }

        let desiredAgentId = String(options.preferredAgentId || traceyState.selectedAgentId || "").trim();
        if (!desiredAgentId || !servers.some((server) => String(server.agent_id || "").trim() === desiredAgentId)) {
            desiredAgentId = String(servers[0].agent_id || "").trim();
        }
        if (desiredAgentId) {
            await fetchTraceyServerTelemetry(desiredAgentId, { preferredGpuId: options.preferredGpuId || "" });
        }
    }

    async function fetchTraceyServerTelemetry(agentId, options = {}) {
        const normalizedId = String(agentId || "").trim();
        if (!normalizedId) {
            traceyState.selectedServerTelemetry = null;
            traceyState.selectedGpuTelemetry = null;
            renderTraceyServerTelemetry(null);
            renderTraceyGpuTelemetry(null);
            renderTraceyAgentFacts(traceyState.selectedAgentAnalysis);
            return;
        }

        traceyState.selectedAgentId = normalizedId;
        syncControlAgentInput();
        if (traceyState.selectedRackDetail) {
            renderTraceyRackDetail(traceyState.selectedRackDetail);
        }
        const requestSeq = ++traceyServerRequestSeq;
        if (nodes.traceyServerTelemetryMeta) {
            nodes.traceyServerTelemetryMeta.textContent = `Loading ${normalizedId}...`;
        }
        const response = await fetchJson(`/tracey/agents/${encodeURIComponent(normalizedId)}/server`);
        if (requestSeq !== traceyServerRequestSeq) {
            return;
        }
        if (!response.ok) {
            traceyState.selectedServerTelemetry = null;
            traceyState.selectedGpuTelemetry = null;
            renderTraceyServerTelemetry(null);
            renderTraceyGpuTelemetry(null);
            renderTraceyAgentFacts(traceyState.selectedAgentAnalysis);
            return;
        }

        const data = responseData(response.payload) || {};
        traceyState.selectedServerTelemetry = data;
        if (data.rack) {
            traceyState.selectedRack = String(data.rack);
            renderTraceyFleetOverview(traceyState.fleet);
            if (traceyState.selectedRackDetail && String(traceyState.selectedRackDetail.rack || "") === traceyState.selectedRack) {
                renderTraceyRackDetail(traceyState.selectedRackDetail);
            }
        }
        renderTraceyServerTelemetry(data);
        renderTraceyAgentFacts(traceyState.selectedAgentAnalysis);
        updateTraceyInsightsSubtitle();

        const gpus = Array.isArray(data.gpus) ? data.gpus : [];
        if (!gpus.length) {
            traceyState.selectedGpuId = "";
            traceyState.selectedGpuTelemetry = null;
            renderTraceyGpuTelemetry(null);
            return;
        }

        let desiredGpuId = String(options.preferredGpuId || traceyState.selectedGpuId || "").trim();
        if (!desiredGpuId || !gpus.some((gpu) => String(gpu.gpu_id || gpu.gpuId || "").trim() === desiredGpuId)) {
            desiredGpuId = String(gpus[0].gpu_id || gpus[0].gpuId || "").trim();
        }
        if (desiredGpuId) {
            await fetchTraceyGpuTelemetry(normalizedId, desiredGpuId);
        }
    }

    async function fetchTraceyGpuTelemetry(agentId, gpuId) {
        const normalizedAgentId = String(agentId || traceyState.selectedAgentId || "").trim();
        const normalizedGpuId = String(gpuId || "").trim();
        if (!normalizedAgentId || !normalizedGpuId) {
            traceyState.selectedGpuId = "";
            traceyState.selectedGpuTelemetry = null;
            renderTraceyGpuTelemetry(null);
            return;
        }

        traceyState.selectedGpuId = normalizedGpuId;
        if (traceyState.selectedRackDetail) {
            renderTraceyRackDetail(traceyState.selectedRackDetail);
        }
        if (traceyState.selectedServerTelemetry) {
            renderTraceyServerTelemetry(traceyState.selectedServerTelemetry);
        }
        const requestSeq = ++traceyGpuRequestSeq;
        if (nodes.traceyGpuTelemetryMeta) {
            nodes.traceyGpuTelemetryMeta.textContent = `Loading ${normalizedGpuId}...`;
        }
        const response = await fetchJson(`/tracey/agents/${encodeURIComponent(normalizedAgentId)}/gpus/${encodeURIComponent(normalizedGpuId)}/telemetry`);
        if (requestSeq !== traceyGpuRequestSeq) {
            return;
        }
        if (!response.ok) {
            traceyState.selectedGpuTelemetry = null;
            renderTraceyGpuTelemetry(null);
            return;
        }

        const data = responseData(response.payload) || {};
        traceyState.selectedGpuTelemetry = data;
        renderTraceyGpuTelemetry(data);
    }

    async function selectTraceyAgent(agentId, preferredGpuId = "") {
        const normalizedId = String(agentId || "").trim();
        if (!normalizedId) {
            return;
        }
        traceyState.selectedAgentId = normalizedId;
        if (preferredGpuId) {
            traceyState.selectedGpuId = String(preferredGpuId).trim();
        }
        syncControlAgentInput();

        const selectedFleetAgent = findTraceyFleetAgent(normalizedId);
        await Promise.all([
            fetchTraceyAgentAnalysis(normalizedId),
            fetchTraceyDeepDive(normalizedId)
        ]);

        if (selectedFleetAgent && selectedFleetAgent.rack) {
            await fetchTraceyRackDetail(selectedFleetAgent.rack, {
                preferredAgentId: normalizedId,
                preferredGpuId
            });
            return;
        }

        await fetchTraceyServerTelemetry(normalizedId, { preferredGpuId });
    }

    async function refreshTraceyInsights() {
        await Promise.all([
            fetchTraceyAnalytics(),
            fetchTraceyAssessmentFleet(),
            fetchTraceyAdaptive()
        ]);
        await fetchTraceyFleet();
    }

    async function fetchTraceyDeepDive(agentId = "") {
        const normalizedId = String(agentId || traceyState.selectedAgentId || "").trim();
        if (!normalizedId) {
            renderTraceyDeepDive(null);
            return;
        }
        const response = await fetchJson(`/tracey/agents/${encodeURIComponent(normalizedId)}/deepdive`);
        if (!response.ok) {
            if (nodes.traceyControlStatus) {
                const message = response.payload && typeof response.payload === "object"
                    ? String(response.payload.message || "Deep-dive request failed.")
                    : "Deep-dive request failed.";
                nodes.traceyControlStatus.textContent = message;
                nodes.traceyControlStatus.style.borderColor = "#ef4444";
            }
            renderTraceyDeepDive(null);
            return;
        }
        const payload = responseData(response.payload) || {};
        traceyState.selectedDeepDive = payload;
        renderTraceyDeepDive(payload);
    }

    async function applyTraceyControl() {
        const agentId = String((nodes.traceyControlAgent && nodes.traceyControlAgent.value) || traceyState.selectedAgentId || "").trim();
        if (!agentId) {
            if (nodes.traceyControlStatus) {
                nodes.traceyControlStatus.textContent = "Select an agent before applying control.";
                nodes.traceyControlStatus.style.borderColor = "#ef4444";
            }
            return;
        }

        const payload = {};
        const enabled = parseTriState(nodes.traceyControlEnabled ? nodes.traceyControlEnabled.value : "unchanged");
        const deepDive = parseTriState(nodes.traceyControlDeepDive ? nodes.traceyControlDeepDive.value : "unchanged");
        const tmr = parseTriState(nodes.traceyControlTmr ? nodes.traceyControlTmr.value : "unchanged");
        const forceScan = parseTriState(nodes.traceyControlForceScan ? nodes.traceyControlForceScan.value : "false");
        const overhead = parseNumber(nodes.traceyControlOverhead ? nodes.traceyControlOverhead.value : NaN, NaN);

        if (enabled !== null) {
            payload.enabled = enabled;
        }
        if (deepDive !== null) {
            payload.deep_dive = deepDive;
        }
        if (tmr !== null) {
            payload.tmr_enabled = tmr;
        }
        if (Number.isFinite(overhead)) {
            payload.overhead_budget_pct = Math.max(0.1, Math.min(50, overhead));
        }
        if (forceScan === true) {
            payload.force_scan = true;
        }

        if (nodes.traceyControlStatus) {
            nodes.traceyControlStatus.textContent = `Applying control to ${agentId}...`;
            nodes.traceyControlStatus.style.borderColor = "#f59e0b";
        }

        const response = await fetchJson(`/tracey/agents/${encodeURIComponent(agentId)}/control`, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(payload)
        });

        if (!response.ok) {
            const message = response.payload && typeof response.payload === "object"
                ? String(response.payload.message || "Control update failed.")
                : "Control update failed.";
            if (nodes.traceyControlStatus) {
                nodes.traceyControlStatus.textContent = message;
                nodes.traceyControlStatus.style.borderColor = "#ef4444";
            }
            return;
        }

        if (nodes.traceyControlStatus) {
            nodes.traceyControlStatus.textContent = `Control applied to ${agentId}`;
            nodes.traceyControlStatus.style.borderColor = "#22c55e";
        }
        await fetchTraceyDeepDive(agentId);
        await fetchTraceyAgentAnalysis(agentId);
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
        syncControlAgentInput();
        await fetchTraceyAgentAnalysis(traceyState.selectedAgentId);
        await fetchTraceyDeepDive(traceyState.selectedAgentId);
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
        if (nodes.traceyControlAgent) {
            nodes.traceyControlAgent.value = normalizedId;
        }
        renderTraceyAgentDrilldown(data);
    }

    async function openTraceyInsightsModal(preselectedAgentId = "") {
        closeClusterDetailsModal();
        closeOpenShiftDetailsModal();
        if (preselectedAgentId) {
            traceyState.selectedAgentId = String(preselectedAgentId).trim();
        }
        syncControlAgentInput();
        setTraceyInsightsModalOpen(true);
        await refreshTraceyInsights();
    }

    function initializeTraceyInsightsUi() {
        const rerenderFleetAssessment = () => {
            renderTraceyFleetAssessmentRows();
        };
        const rerenderRackAssessment = () => {
            renderTraceyRackDetail(traceyState.selectedRackDetail);
        };
        const rerenderServerAssessment = () => {
            if (traceyState.selectedServerTelemetry) {
                const assessmentSnapshot = getAssessmentSnapshot(traceyState.selectedServerTelemetry);
                const assessmentSummary = getAssessmentSummary(traceyState.selectedServerTelemetry);
                const assessmentCommunication = getAssessmentCommunication(traceyState.selectedServerTelemetry);
                const assessmentProgress = assessmentSnapshot.progress && typeof assessmentSnapshot.progress === "object"
                    ? assessmentSnapshot.progress
                    : {};
                const filteredAssessmentMetrics = filteredAndSortedServerAssessmentMetricRows(
                    buildAssessmentMetricRows(assessmentSummary, assessmentCommunication, assessmentProgress)
                );
                renderMetricList(
                    nodes.traceyServerAssessmentFacts,
                    filteredAssessmentMetrics.rows,
                    filteredAssessmentMetrics.emptyMessage
                );
                return;
            }
            renderMetricList(nodes.traceyServerAssessmentFacts, [], "No assessment snapshot loaded.");
        };
        if (nodes.openTraceyInsightsBtn) {
            nodes.openTraceyInsightsBtn.addEventListener("click", () => {
                openTraceyInsightsModal();
            });
        }
        if (nodes.openTraceyAdaptiveBtn) {
            nodes.openTraceyAdaptiveBtn.addEventListener("click", () => {
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
        if (nodes.traceyAdaptiveCard) {
            nodes.traceyAdaptiveCard.addEventListener("click", () => {
                openTraceyInsightsModal();
            });
            nodes.traceyAdaptiveCard.addEventListener("keydown", (event) => {
                if (event.key === "Enter" || event.key === " ") {
                    event.preventDefault();
                    openTraceyInsightsModal();
                }
            });
        }
        if (nodes.traceyAdaptivePolicySelect) {
            nodes.traceyAdaptivePolicySelect.value = normalizeTraceyAdaptivePolicy(traceyState.adaptivePolicy);
            nodes.traceyAdaptivePolicySelect.addEventListener("change", () => {
                saveTraceyAdaptivePolicy(nodes.traceyAdaptivePolicySelect.value);
                fetchTraceyAdaptive();
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
                    selectTraceyAgent(agentId);
                }
            });
        }
        if (nodes.traceyFleetAssessmentRows) {
            nodes.traceyFleetAssessmentRows.addEventListener("click", (event) => {
                const trigger = event.target.closest("button[data-tracey-assessment-agent]");
                if (!trigger) {
                    return;
                }
                event.preventDefault();
                const agentId = String(trigger.getAttribute("data-tracey-assessment-agent") || "").trim();
                if (agentId) {
                    selectTraceyAgent(agentId);
                }
            });
        }
        if (nodes.traceyRackCards) {
            nodes.traceyRackCards.addEventListener("click", (event) => {
                const trigger = event.target.closest("button[data-tracey-rack]");
                if (!trigger) {
                    return;
                }
                event.preventDefault();
                const rackId = String(trigger.getAttribute("data-tracey-rack") || "").trim();
                if (rackId) {
                    fetchTraceyRackDetail(rackId);
                }
            });
        }
        if (nodes.traceyRackServerRows) {
            nodes.traceyRackServerRows.addEventListener("click", (event) => {
                const trigger = event.target.closest("button[data-tracey-server-id]");
                if (!trigger) {
                    return;
                }
                event.preventDefault();
                const agentId = String(trigger.getAttribute("data-tracey-server-id") || "").trim();
                if (agentId) {
                    selectTraceyAgent(agentId);
                }
            });
        }
        if (nodes.traceyRackGpuHeatmap) {
            nodes.traceyRackGpuHeatmap.addEventListener("click", (event) => {
                const trigger = event.target.closest("button[data-tracey-server-id][data-tracey-gpu-id]");
                if (!trigger) {
                    return;
                }
                event.preventDefault();
                const agentId = String(trigger.getAttribute("data-tracey-server-id") || "").trim();
                const gpuId = String(trigger.getAttribute("data-tracey-gpu-id") || "").trim();
                if (agentId && gpuId) {
                    selectTraceyAgent(agentId, gpuId);
                }
            });
        }
        if (nodes.traceyServerGpuTiles) {
            nodes.traceyServerGpuTiles.addEventListener("click", (event) => {
                const trigger = event.target.closest("button[data-tracey-gpu-agent][data-tracey-gpu-id]");
                if (!trigger) {
                    return;
                }
                event.preventDefault();
                const agentId = String(trigger.getAttribute("data-tracey-gpu-agent") || "").trim();
                const gpuId = String(trigger.getAttribute("data-tracey-gpu-id") || "").trim();
                if (agentId && gpuId) {
                    selectTraceyAgent(agentId, gpuId);
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
                refreshTraceyInsights();
            });
        }
        if (nodes.traceyDeepDiveRefresh) {
            nodes.traceyDeepDiveRefresh.addEventListener("click", () => {
                fetchTraceyDeepDive();
            });
        }
        if (nodes.traceyControlApply) {
            nodes.traceyControlApply.addEventListener("click", () => {
                applyTraceyControl();
            });
        }
        if (nodes.traceyControlAgent) {
            nodes.traceyControlAgent.addEventListener("change", () => {
                const id = String(nodes.traceyControlAgent.value || "").trim();
                if (id) {
                    selectTraceyAgent(id);
                }
            });
        }
        if (nodes.traceyWindowSelect) {
            nodes.traceyWindowSelect.addEventListener("change", () => {
                refreshTraceyInsights();
            });
        }
        if (nodes.traceyBucketSelect) {
            nodes.traceyBucketSelect.addEventListener("change", () => {
                refreshTraceyInsights();
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
        if (nodes.traceyFleetAssessmentSearch) {
            nodes.traceyFleetAssessmentSearch.addEventListener("input", rerenderFleetAssessment);
        }
        if (nodes.traceyFleetAssessmentFilter) {
            nodes.traceyFleetAssessmentFilter.addEventListener("change", rerenderFleetAssessment);
        }
        if (nodes.traceyFleetAssessmentSort) {
            nodes.traceyFleetAssessmentSort.addEventListener("change", rerenderFleetAssessment);
        }
        if (nodes.traceyRackAssessmentSearch) {
            nodes.traceyRackAssessmentSearch.addEventListener("input", rerenderRackAssessment);
        }
        if (nodes.traceyRackAssessmentFilter) {
            nodes.traceyRackAssessmentFilter.addEventListener("change", rerenderRackAssessment);
        }
        if (nodes.traceyRackAssessmentSort) {
            nodes.traceyRackAssessmentSort.addEventListener("change", rerenderRackAssessment);
        }
        if (nodes.traceyServerAssessmentSearch) {
            nodes.traceyServerAssessmentSearch.addEventListener("input", rerenderServerAssessment);
        }
        if (nodes.traceyServerAssessmentFilter) {
            nodes.traceyServerAssessmentFilter.addEventListener("change", rerenderServerAssessment);
        }
        if (nodes.traceyServerAssessmentSort) {
            nodes.traceyServerAssessmentSort.addEventListener("change", rerenderServerAssessment);
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
            openstackClustersRes,
            proxmoxClustersRes,
            vmListRes,
            traceyAgentsRes,
            traceyAdaptiveRes,
            k8sHealthRes,
            connectionRes,
            openshiftResourcesRes,
            openstackResourcesRes,
            proxmoxResourcesRes
        ] = await Promise.all([
            fetchJson("/k8s/list"),
            fetchJson("/vcluster/list"),
            fetchJson("/openshift/clusters"),
            fetchJson("/openstack/clusters"),
            fetchJson("/proxmox/clusters"),
            fetchJson("/vm/list"),
            fetchJson("/tracey/agents"),
            fetchJson(buildTraceyAdaptivePath()),
            fetchJson("/k8s/healthz"),
            fetchJson("/connections/status"),
            fetchJson("/openshift/resources"),
            fetchJson("/openstack/resources"),
            fetchJson("/proxmox/resources")
        ]);

        const allResponses = [
            k8sListRes,
            vclusterListRes,
            openshiftClustersRes,
            openstackClustersRes,
            proxmoxClustersRes,
            vmListRes,
            traceyAgentsRes,
            traceyAdaptiveRes,
            k8sHealthRes,
            connectionRes,
            openshiftResourcesRes,
            openstackResourcesRes,
            proxmoxResourcesRes
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

        const openstackItems = arrayFromUnknown(openstackClustersRes.payload);
        const openstackSummary = summarizeRunning(openstackItems);
        nodes.openstackMetric.textContent = `${openstackSummary.running} / ${openstackSummary.total}`;
        const openstackRows = openstackItems.map((cluster) => {
            const name = cluster.name || cluster.id || "unknown";
            const endpoint = cluster.endpoint || cluster.api || "-";
            const status = cluster.status || "Unknown";
            return `<tr><td>${escapeHtml(name)}</td><td>${escapeHtml(endpoint)}</td><td>${statusBadge(status)}</td></tr>`;
        });
        renderRows(nodes.openstackRows, openstackRows, "No OpenStack clusters detected.", 3);

        const proxmoxItems = arrayFromUnknown(proxmoxClustersRes.payload);
        const proxmoxSummary = summarizeRunning(proxmoxItems);
        nodes.proxmoxMetric.textContent = `${proxmoxSummary.running} / ${proxmoxSummary.total}`;
        const proxmoxRows = proxmoxItems.map((cluster) => {
            const name = cluster.name || cluster.id || "unknown";
            const endpoint = cluster.endpoint || cluster.api || cluster.console || "-";
            const status = cluster.status || "Unknown";
            return `<tr><td>${escapeHtml(name)}</td><td>${escapeHtml(endpoint)}</td><td>${statusBadge(status)}</td></tr>`;
        });
        renderRows(nodes.proxmoxRows, proxmoxRows, "No Proxmox clusters detected.", 3);

        const vmItems = arrayFromUnknown(vmListRes.payload);
        const vmSummary = summarizeRunning(vmItems);
        nodes.vmMetric.textContent = `${vmSummary.running} / ${vmSummary.total}`;

        const traceyData = responseData(traceyAgentsRes.payload) || {};
        const traceyAgents = Array.isArray(traceyData.agents) ? traceyData.agents : [];
        const traceySummary = traceyData.summary || {};
        const tracey_guardSummary = traceyData.tracey_guard_summary || {};
        const loaderThreatSummary = traceyData.loader_threat_summary || {};
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

        const adaptiveData = traceyAdaptiveRes.ok ? (responseData(traceyAdaptiveRes.payload) || {}) : null;
        traceyState.adaptive = adaptiveData;
        renderTraceyAdaptiveOverview(adaptiveData);

        const k8sHealthy = k8sHealthRes.ok;
        setPill(nodes.k8sApiPill, "K8s API", k8sHealthy ? "Healthy" : "Unavailable", k8sHealthy ? "rgba(34,197,94,0.8)" : "rgba(239,68,68,0.8)");

        const connectionData = responseData(connectionRes.payload) || {};
        const connectionState = String(connectionData.status || "").toLowerCase();
        const connectionText = connectionRes.ok
            ? (connectionState === "local_only" ? "Reachable (Local mode)" : "Reachable")
            : "Unavailable";
        setCheck(nodes.connStatus, connectionText, connectionRes.ok ? "#22c55e" : "#ef4444");
        setCheck(nodes.resourceStatus, openshiftResourcesRes.ok ? "Reachable" : "Unavailable", openshiftResourcesRes.ok ? "#22c55e" : "#ef4444");
        setCheck(nodes.openstackResourceStatus, openstackResourcesRes.ok ? "Reachable" : "Unavailable", openstackResourcesRes.ok ? "#22c55e" : "#ef4444");
        setCheck(nodes.proxmoxResourceStatus, proxmoxResourcesRes.ok ? "Reachable" : "Unavailable", proxmoxResourcesRes.ok ? "#22c55e" : "#ef4444");
        setCheck(nodes.vmInventoryStatus, vmListRes.ok ? `${vmItems.length} listed` : "Unavailable", vmListRes.ok ? "#22c55e" : "#ef4444");
        const reachableTracey = Number(traceySummary.reachable || 0);
        const discoveredTracey = Number(traceySummary.discovered || 0);
        const nonCompliantResources = Number(requirementSummary.noncompliant || 0);
        const managedResources = Number(requirementSummary.total || 0);
        const tracey_guardQuarantined = Number(tracey_guardSummary.quarantined_devices || 0);
        const tracey_guardFailures = Number(tracey_guardSummary.total_failures || 0);
        const blockedLoaderProviders = Number(loaderThreatSummary.blocked_provider_count || 0);
        const blockedLoaderArtifacts = Number(loaderThreatSummary.blocked_artifact_count || 0);
        const traceyTone = !traceyAgentsRes.ok
            ? "#ef4444"
            : (nonCompliantResources > 0 || tracey_guardQuarantined > 0 || tracey_guardFailures > 0 || blockedLoaderProviders > 0 || blockedLoaderArtifacts > 0 ? "#f59e0b" : "#22c55e");
        setCheck(
            nodes.traceyStatus,
            traceyAgentsRes.ok
                ? `${traceyAgents.length} detected (${reachableTracey} reachable, ${discoveredTracey} discovered, ${nonCompliantResources}/${managedResources} non-compliant, ${tracey_guardQuarantined} quarantined, ${tracey_guardFailures} probe failures, ${blockedLoaderProviders} blocked providers, ${blockedLoaderArtifacts} blocked artifacts)`
                : "Unavailable",
            traceyTone
        );

        if (nodes.traceyInsightsModal && !nodes.traceyInsightsModal.hidden) {
            refreshTraceyInsights();
        }

        setPill(nodes.refreshPill, "Refresh", `Updated ${nowIsoTime()}`, "rgba(216,210,202,0.45)");
    }

    function initializeSessionUi() {
        updateSessionLabel();
        if (authToken && !authIdentity) {
            void refreshAuthIdentity();
        }
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
    renderTraceyAdaptiveOverview(null);
    renderTraceyFleetOverview(null);
    renderTraceyRackDetail(null);
    renderTraceyServerTelemetry(null);
    renderTraceyGpuTelemetry(null);
    renderTraceyAgentDrilldown(null);
    renderTraceyDeepDive(null);
    refreshDashboard();
    window.setInterval(refreshDashboard, REFRESH_INTERVAL_MS);
})();
