(function () {
    const REFRESH_INTERVAL_MS = 15000;
    const TOKEN_STORAGE_KEY = "nmc_dashboard_token";
    const IDENTITY_STORAGE_KEY = "nmc_dashboard_identity";
    const TRACEY_ADAPTIVE_POLICY_KEY = "nmc_tracey_adaptive_policy";
    const monitoringPrefix = "/services/health/monitoring";
    const basePath = window.location.pathname.startsWith(monitoringPrefix)
        ? monitoringPrefix
        : "";
    const serviceAccess = window.NMCServiceAccess || {
        getServiceAccessMap: () => ({}),
        getServiceAccess: () => ({
            visible: false,
            can_observe: false,
            can_use: false,
            can_control: false
        }),
        isServiceVisible: () => false,
        canControlService: () => false
    };

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
        aiLabMetric: document.getElementById("aiLabMetric"),
        aiLabCard: document.getElementById("aiLabCard"),
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
        aiLabRows: document.getElementById("aiLabRows"),
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
        traceyFleetTopologyTabs: document.getElementById("traceyFleetTopologyTabs"),
        traceyFleetTopologyTabActual: document.getElementById("traceyFleetTopologyTabActual"),
        traceyFleetTopologyTabExpansion: document.getElementById("traceyFleetTopologyTabExpansion"),
        traceyFleetTopologyForecast: document.getElementById("traceyFleetTopologyForecast"),
        traceyFleetTalkerMode: document.getElementById("traceyFleetTalkerMode"),
        traceyFleetSimulationControls: document.getElementById("traceyFleetSimulationControls"),
        traceyFleetSimulationNodes: document.getElementById("traceyFleetSimulationNodes"),
        traceyFleetSimulationGpus: document.getElementById("traceyFleetSimulationGpus"),
        traceyFleetSimulationCores: document.getElementById("traceyFleetSimulationCores"),
        traceyFleetSimulationStrategy: document.getElementById("traceyFleetSimulationStrategy"),
        traceyFleetSimulationRecommended: document.getElementById("traceyFleetSimulationRecommended"),
        traceyFleetTopologyChart: document.getElementById("traceyFleetTopologyChart"),
        traceyFleetTopologyLegend: document.getElementById("traceyFleetTopologyLegend"),
        traceyFleetSimulationFacts: document.getElementById("traceyFleetSimulationFacts"),
        traceyFleetTalkerRows: document.getElementById("traceyFleetTalkerRows"),
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
        traceyRackTopologyTabs: document.getElementById("traceyRackTopologyTabs"),
        traceyRackTopologyTabActual: document.getElementById("traceyRackTopologyTabActual"),
        traceyRackTopologyTabExpansion: document.getElementById("traceyRackTopologyTabExpansion"),
        traceyRackTopologyForecast: document.getElementById("traceyRackTopologyForecast"),
        traceyRackTalkerMode: document.getElementById("traceyRackTalkerMode"),
        traceyRackSimulationControls: document.getElementById("traceyRackSimulationControls"),
        traceyRackSimulationNodes: document.getElementById("traceyRackSimulationNodes"),
        traceyRackSimulationGpus: document.getElementById("traceyRackSimulationGpus"),
        traceyRackSimulationCores: document.getElementById("traceyRackSimulationCores"),
        traceyRackSimulationStrategy: document.getElementById("traceyRackSimulationStrategy"),
        traceyRackSimulationRecommended: document.getElementById("traceyRackSimulationRecommended"),
        traceyRackTopologyChart: document.getElementById("traceyRackTopologyChart"),
        traceyRackTopologyLegend: document.getElementById("traceyRackTopologyLegend"),
        traceyRackSimulationFacts: document.getElementById("traceyRackSimulationFacts"),
        traceyRackTalkerRows: document.getElementById("traceyRackTalkerRows"),
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
        traceyServerNetworkFacts: document.getElementById("traceyServerNetworkFacts"),
        traceyServerForecastFacts: document.getElementById("traceyServerForecastFacts"),
        traceyServerForecastAdvice: document.getElementById("traceyServerForecastAdvice"),
        traceyServerGuardFacts: document.getElementById("traceyServerGuardFacts"),
        traceyServerTopologyTabs: document.getElementById("traceyServerTopologyTabs"),
        traceyServerTopologyTabActual: document.getElementById("traceyServerTopologyTabActual"),
        traceyServerTopologyTabExpansion: document.getElementById("traceyServerTopologyTabExpansion"),
        traceyServerTopologyForecast: document.getElementById("traceyServerTopologyForecast"),
        traceyServerTalkerMode: document.getElementById("traceyServerTalkerMode"),
        traceyServerSimulationControls: document.getElementById("traceyServerSimulationControls"),
        traceyServerSimulationNodes: document.getElementById("traceyServerSimulationNodes"),
        traceyServerSimulationGpus: document.getElementById("traceyServerSimulationGpus"),
        traceyServerSimulationCores: document.getElementById("traceyServerSimulationCores"),
        traceyServerSimulationStrategy: document.getElementById("traceyServerSimulationStrategy"),
        traceyServerSimulationRecommended: document.getElementById("traceyServerSimulationRecommended"),
        traceyServerTopologyChart: document.getElementById("traceyServerTopologyChart"),
        traceyServerTopologyLegend: document.getElementById("traceyServerTopologyLegend"),
        traceyServerSimulationFacts: document.getElementById("traceyServerSimulationFacts"),
        traceyServerTalkerBars: document.getElementById("traceyServerTalkerBars"),
        traceyServerTopologyFocus: document.getElementById("traceyServerTopologyFocus"),
        traceyServerTopologyClear: document.getElementById("traceyServerTopologyClear"),
        traceyServerFlowRows: document.getElementById("traceyServerFlowRows"),
        traceyServerListenerRows: document.getElementById("traceyServerListenerRows"),
        traceyServerPortRows: document.getElementById("traceyServerPortRows"),
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
        traceyNetworkChart: document.getElementById("traceyNetworkChart"),
        traceyNetworkLegend: document.getElementById("traceyNetworkLegend"),
        traceyLogChart: document.getElementById("traceyLogChart"),
        traceyLogLegend: document.getElementById("traceyLogLegend"),
        traceyAnalyticsAgentRows: document.getElementById("traceyAnalyticsAgentRows"),
        traceyAgentDrilldownTitle: document.getElementById("traceyAgentDrilldownTitle"),
        traceyAgentDrilldownMeta: document.getElementById("traceyAgentDrilldownMeta"),
        traceyAgentStatusChart: document.getElementById("traceyAgentStatusChart"),
        traceyAgentStatusLegend: document.getElementById("traceyAgentStatusLegend"),
        traceyAgentNetworkChart: document.getElementById("traceyAgentNetworkChart"),
        traceyAgentNetworkLegend: document.getElementById("traceyAgentNetworkLegend"),
        traceyAgentFacts: document.getElementById("traceyAgentFacts"),
        traceyAgentLogLevel: document.getElementById("traceyAgentLogLevel"),
        traceyAgentLogCategory: document.getElementById("traceyAgentLogCategory"),
        traceyAgentLogSearch: document.getElementById("traceyAgentLogSearch"),
        traceyAgentLogRows: document.getElementById("traceyAgentLogRows"),
        // Gail Trading
        gailTradingCard: document.getElementById("gailTradingCard"),
        gailTradingMetric: document.getElementById("gailTradingMetric"),
        gailTradingPanel: document.getElementById("gailTradingPanel"),
        gailTradingPanelStatus: document.getElementById("gailTradingPanelStatus"),
        gailTradingPanelIssues: document.getElementById("gailTradingPanelIssues"),
        gailTradingPanelEvaluation: document.getElementById("gailTradingPanelEvaluation"),
        gailTradingPanelTrade: document.getElementById("gailTradingPanelTrade"),
        gailTradingModal: document.getElementById("gailTradingModal"),
        gailTradingModalClose: document.getElementById("gailTradingModalClose"),
        gailTradingModalSubtitle: document.getElementById("gailTradingModalSubtitle"),
        gailTradingModalBody: document.getElementById("gailTradingModalBody"),
        openGailTradingBtn: document.getElementById("openGailTradingBtn"),
        gailTradingPauseBtn: document.getElementById("gailTradingPauseBtn"),
        gailTradingResumeBtn: document.getElementById("gailTradingResumeBtn"),
        gailTradingEvaluateBtn: document.getElementById("gailTradingEvaluateBtn")
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
        selectedDeepDive: null,
        serverTopologyFocus: null
    };
    const TRACEY_SIMULATION_SCOPE_CONFIG = {
        fleet: {
            scope: "fleet",
            tabs: nodes.traceyFleetTopologyTabs,
            actualTab: nodes.traceyFleetTopologyTabActual,
            expansionTab: nodes.traceyFleetTopologyTabExpansion,
            forecast: nodes.traceyFleetTopologyForecast,
            controls: nodes.traceyFleetSimulationControls,
            facts: nodes.traceyFleetSimulationFacts,
            inputs: {
                nodes: nodes.traceyFleetSimulationNodes,
                gpus: nodes.traceyFleetSimulationGpus,
                cores: nodes.traceyFleetSimulationCores,
                strategy: nodes.traceyFleetSimulationStrategy
            },
            recommendedButton: nodes.traceyFleetSimulationRecommended
        },
        rack: {
            scope: "rack",
            tabs: nodes.traceyRackTopologyTabs,
            actualTab: nodes.traceyRackTopologyTabActual,
            expansionTab: nodes.traceyRackTopologyTabExpansion,
            forecast: nodes.traceyRackTopologyForecast,
            controls: nodes.traceyRackSimulationControls,
            facts: nodes.traceyRackSimulationFacts,
            inputs: {
                nodes: nodes.traceyRackSimulationNodes,
                gpus: nodes.traceyRackSimulationGpus,
                cores: nodes.traceyRackSimulationCores,
                strategy: nodes.traceyRackSimulationStrategy
            },
            recommendedButton: nodes.traceyRackSimulationRecommended
        },
        server: {
            scope: "server",
            tabs: nodes.traceyServerTopologyTabs,
            actualTab: nodes.traceyServerTopologyTabActual,
            expansionTab: nodes.traceyServerTopologyTabExpansion,
            forecast: nodes.traceyServerTopologyForecast,
            controls: nodes.traceyServerSimulationControls,
            facts: nodes.traceyServerSimulationFacts,
            inputs: {
                nodes: nodes.traceyServerSimulationNodes,
                gpus: nodes.traceyServerSimulationGpus,
                cores: nodes.traceyServerSimulationCores,
                strategy: nodes.traceyServerSimulationStrategy
            },
            recommendedButton: nodes.traceyServerSimulationRecommended
        }
    };
    const TRACEY_URL_PARAM_KEYS = [
        "tracey",
        "tracey_agent",
        "tracey_rack",
        "tracey_gpu",
        "tracey_policy",
        "tracey_window",
        "tracey_bucket",
        "tracey_focus"
    ];
    let traceyUrlStateMuted = false;

    function nowIsoTime() {
        return new Date().toLocaleTimeString();
    }

    function withTraceyUrlStateMuted(callback) {
        const previous = traceyUrlStateMuted;
        traceyUrlStateMuted = true;
        try {
            return callback();
        } finally {
            traceyUrlStateMuted = previous;
        }
    }

    function isTraceyInsightsModalOpen() {
        return Boolean(nodes.traceyInsightsModal && !nodes.traceyInsightsModal.hidden);
    }

    function serializeTraceyServerTopologyFocus(focus) {
        const normalized = buildTraceyServerTopologyFocus(focus);
        if (!normalized) {
            return "";
        }
        return JSON.stringify({
            kind: normalized.kind,
            label: normalized.label,
            subtitle: normalized.subtitle,
            process: normalized.process,
            pid: normalized.pid,
            localPort: normalized.localPort,
            protocol: normalized.protocol,
            remoteIp: normalized.remoteIp,
            remotePort: normalized.remotePort,
            activeLocalId: normalized.activeLocalId,
            activeRemoteId: normalized.activeRemoteId,
            activeLinkId: normalized.activeLinkId
        });
    }

    function parseTraceyUrlTopologyFocus(rawValue) {
        const raw = String(rawValue || "").trim();
        if (!raw) {
            return null;
        }
        try {
            return buildTraceyServerTopologyFocus(JSON.parse(raw));
        } catch (_error) {
            return null;
        }
    }

    function parseTraceyUrlInteger(rawValue, fallback, minValue, maxValue) {
        const value = Math.round(parseNumber(rawValue, fallback));
        if (!Number.isFinite(value)) {
            return fallback;
        }
        return Math.max(minValue, Math.min(maxValue, value));
    }

    function readTraceyUrlState() {
        const params = new URLSearchParams(window.location.search);
        const hasTraceyState = TRACEY_URL_PARAM_KEYS.some((key) => params.has(key));
        if (!hasTraceyState) {
            return null;
        }
        const agentId = String(params.get("tracey_agent") || "").trim();
        const rackId = String(params.get("tracey_rack") || "").trim();
        const gpuId = String(params.get("tracey_gpu") || "").trim();
        const policy = normalizeTraceyAdaptivePolicy(params.get("tracey_policy") || traceyState.adaptivePolicy);
        return {
            open: params.get("tracey") !== "0",
            agentId,
            rackId,
            gpuId,
            policy,
            windowSeconds: parseTraceyUrlInteger(params.get("tracey_window"), 3600, 300, 604800),
            bucketSeconds: parseTraceyUrlInteger(params.get("tracey_bucket"), 60, 10, 3600),
            focus: parseTraceyUrlTopologyFocus(params.get("tracey_focus") || "")
        };
    }

    function syncTraceyUrlState() {
        if (traceyUrlStateMuted || !window.history || typeof window.history.replaceState !== "function") {
            return;
        }
        const params = new URLSearchParams(window.location.search);
        TRACEY_URL_PARAM_KEYS.forEach((key) => {
            params.delete(key);
        });
        if (isTraceyInsightsModalOpen()) {
            params.set("tracey", "1");
            if (traceyState.selectedAgentId) {
                params.set("tracey_agent", traceyState.selectedAgentId);
            }
            if (traceyState.selectedRack) {
                params.set("tracey_rack", traceyState.selectedRack);
            }
            if (traceyState.selectedGpuId) {
                params.set("tracey_gpu", traceyState.selectedGpuId);
            }
            params.set("tracey_policy", normalizeTraceyAdaptivePolicy(traceyState.adaptivePolicy));
            params.set("tracey_window", String(traceyWindowSeconds()));
            params.set("tracey_bucket", String(traceyBucketSeconds()));
            const serializedFocus = serializeTraceyServerTopologyFocus(traceyState.serverTopologyFocus);
            if (serializedFocus) {
                params.set("tracey_focus", serializedFocus);
            }
        }
        const nextQuery = params.toString();
        const nextUrl = `${window.location.pathname}${nextQuery ? `?${nextQuery}` : ""}${window.location.hash}`;
        const currentUrl = `${window.location.pathname}${window.location.search}${window.location.hash}`;
        if (nextUrl !== currentUrl) {
            window.history.replaceState(window.history.state, "", nextUrl);
        }
    }

    async function applyTraceyUrlState(state = readTraceyUrlState()) {
        if (!state) {
            return;
        }
        withTraceyUrlStateMuted(() => {
            if (nodes.traceyWindowSelect) {
                nodes.traceyWindowSelect.value = String(state.windowSeconds);
            }
            if (nodes.traceyBucketSelect) {
                nodes.traceyBucketSelect.value = String(state.bucketSeconds);
            }
            saveTraceyAdaptivePolicy(state.policy);
            traceyState.selectedRack = state.rackId;
            traceyState.selectedAgentId = state.agentId;
            traceyState.selectedGpuId = state.gpuId;
        });
        if (!state.open) {
            if (isTraceyInsightsModalOpen()) {
                closeTraceyInsightsModal();
            }
            return;
        }
        await openTraceyInsightsModal(state.agentId);
        if (state.focus) {
            setTraceyServerTopologyFocus(state.focus);
        } else {
            clearTraceyServerTopologyFocus();
        }
        syncTraceyUrlState();
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
        if (/^(?:[a-z][a-z0-9+.-]*:)?\/\//i.test(path)) {
            return path;
        }
        if (path === basePath || path.startsWith(`${basePath}/`)) {
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
            return normalizeIdentityPayload(parsed);
        } catch (_error) {
            return null;
        }
    }

    function normalizeIdentityPayload(identity) {
        if (!identity || typeof identity !== "object" || !identity.user) {
            return null;
        }
        const normalizedServiceAccess = serviceAccess.getServiceAccessMap(identity);
        const visibleServices = Object.values(normalizedServiceAccess)
            .filter((entry) => entry && entry.visible)
            .map((entry) => entry.service_key);
        return {
            user: String(identity.user || "").trim(),
            role: String(identity.role || "").trim(),
            groups: Array.isArray(identity.groups) ? identity.groups : [],
            is_admin: Boolean(identity.is_admin),
            active_team: identity.active_team && typeof identity.active_team === "object" ? identity.active_team : null,
            team_count: Number(identity.team_count || 0),
            pending_invitation_count: Number(identity.pending_invitation_count || 0),
            group_memberships: Array.isArray(identity.group_memberships) ? identity.group_memberships : [],
            manageable_groups: Array.isArray(identity.manageable_groups) ? identity.manageable_groups : [],
            visible_groups: Array.isArray(identity.visible_groups) ? identity.visible_groups : [],
            can_manage_access: Boolean(identity.can_manage_access),
            service_access: normalizedServiceAccess,
            visible_services: visibleServices
        };
    }

    function saveIdentity(identity) {
        const normalizedIdentity = normalizeIdentityPayload(identity);
        if (!normalizedIdentity) {
            localStorage.removeItem(IDENTITY_STORAGE_KEY);
            authIdentity = null;
            updateSessionLabel();
            applyTraceyAccessUi();
            applyGailTradingAccessUi();
            return null;
        }
        authIdentity = normalizedIdentity;
        localStorage.setItem(IDENTITY_STORAGE_KEY, JSON.stringify(authIdentity));
        updateSessionLabel();
        applyTraceyAccessUi();
        applyGailTradingAccessUi();
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
        syncTraceyUrlState();
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
            applyTraceyAccessUi();
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

    function traceyServiceAccess() {
        return serviceAccess.getServiceAccess(authIdentity || {}, "tracey");
    }

    function hasTraceyVisibility() {
        return Boolean(serviceAccess.isServiceVisible(authIdentity || {}, "tracey"));
    }

    function canControlTracey() {
        return Boolean(serviceAccess.canControlService(authIdentity || {}, "tracey"));
    }

    function applyTraceyAccessUi() {
        const traceyVisible = hasTraceyVisibility();
        const traceyControllable = canControlTracey();
        if (nodes.traceyCard) {
            nodes.traceyCard.hidden = !traceyVisible;
        }
        if (nodes.aiLabCard) {
            nodes.aiLabCard.hidden = !traceyVisible;
        }
        if (nodes.openTraceyInsightsBtn) {
            nodes.openTraceyInsightsBtn.hidden = !traceyVisible;
        }
        if (nodes.openTraceyAdaptiveBtn) {
            nodes.openTraceyAdaptiveBtn.hidden = !traceyVisible;
        }
        if (!traceyVisible && nodes.traceyInsightsModal && !nodes.traceyInsightsModal.hidden) {
            closeTraceyInsightsModal();
        }
        [
            nodes.traceyControlAgent,
            nodes.traceyControlOverhead,
            nodes.traceyControlEnabled,
            nodes.traceyControlDeepDive,
            nodes.traceyControlTmr,
            nodes.traceyControlForceScan,
            nodes.traceyControlApply
        ].forEach((node) => {
            if (node) {
                node.disabled = !traceyControllable;
            }
        });
        if (!nodes.traceyControlStatus) {
            return;
        }
        if (!traceyVisible) {
            nodes.traceyControlStatus.textContent = "Tracey service is not granted.";
            nodes.traceyControlStatus.style.borderColor = "#9ca3af";
            return;
        }
        if (!traceyControllable) {
            nodes.traceyControlStatus.textContent = "Tracey access is read-only.";
            nodes.traceyControlStatus.style.borderColor = "#9ca3af";
            return;
        }
        if (nodes.traceyControlStatus.textContent === "Tracey access is read-only."
            || nodes.traceyControlStatus.textContent === "Tracey service is not granted.") {
            nodes.traceyControlStatus.textContent = "";
            nodes.traceyControlStatus.style.borderColor = "";
        }
    }

    // --- Gail Trading access helpers ---

    function hasGailTradingVisibility() {
        return Boolean(
            serviceAccess.isServiceVisible(authIdentity || {}, "gail_trading")
            || serviceAccess.isServiceVisible(authIdentity || {}, "gail")
        );
    }

    function canControlGailTrading() {
        return Boolean(
            serviceAccess.canControlService(authIdentity || {}, "gail_trading")
            || serviceAccess.canControlService(authIdentity || {}, "gail")
        );
    }

    function applyGailTradingAccessUi() {
        const visible = hasGailTradingVisibility();
        const controllable = canControlGailTrading();
        if (nodes.gailTradingCard) {
            nodes.gailTradingCard.hidden = !visible;
        }
        if (nodes.gailTradingPanel) {
            nodes.gailTradingPanel.hidden = !visible;
        }
        if (nodes.openGailTradingBtn) {
            nodes.openGailTradingBtn.hidden = !visible;
        }
        [nodes.gailTradingPauseBtn, nodes.gailTradingResumeBtn, nodes.gailTradingEvaluateBtn].forEach((btn) => {
            if (btn) {
                btn.disabled = !controllable;
            }
        });
    }

    let gailTradingModalRequestSeq = 0;

    function gailTradingData(response, fallback = {}) {
        const topLevelData = responseData(response && response.payload);
        const upstreamPayload = topLevelData
            && typeof topLevelData === "object"
            && Object.prototype.hasOwnProperty.call(topLevelData, "payload")
            ? topLevelData.payload
            : topLevelData;
        const data = responseData(upstreamPayload);
        return data == null ? fallback : data;
    }

    function gailTradingItems(response, preferredKey) {
        const data = gailTradingData(response, {});
        if (Array.isArray(data)) {
            return data;
        }
        if (!data || typeof data !== "object") {
            return [];
        }
        if (preferredKey && Array.isArray(data[preferredKey])) {
            return data[preferredKey];
        }
        if (Array.isArray(data.items)) {
            return data.items;
        }
        if (Array.isArray(data.data)) {
            return data.data;
        }
        return [];
    }

    function gailApiIssuesData(response) {
        const data = gailTradingData(response, {});
        if (data && typeof data === "object" && data.api_issues && typeof data.api_issues === "object") {
            return data.api_issues;
        }
        return data && typeof data === "object" ? data : {};
    }

    function gailTradingErrorMessage(response, fallback = "Unable to load Gail Trading data.") {
        const payload = response && response.payload && typeof response.payload === "object" ? response.payload : {};
        const data = responseData(payload) || {};
        const upstream = data && typeof data === "object" ? data.payload : null;
        const upstreamText = upstream && typeof upstream === "object"
            ? (upstream.message || upstream.error || upstream.detail || "")
            : "";
        return String(payload.message || upstreamText || response.error || fallback);
    }

    function formatGailTradingTimestamp(value) {
        const ts = Number(value || 0);
        return ts > 0 ? new Date(ts * 1000).toLocaleString() : "--";
    }

    function setGailTradingModalOpen(open) {
        if (!nodes.gailTradingModal) {
            return;
        }
        nodes.gailTradingModal.hidden = !open;
        updateModalBodyLock();
    }

    function closeGailTradingModal() {
        gailTradingModalRequestSeq += 1;
        setGailTradingModalOpen(false);
    }

    function renderGailTradingModal(statusData, portfolioData, historyData, logsData, issuesData) {
        if (!nodes.gailTradingModalBody) {
            return;
        }
        if (!statusData || !statusData.ok) {
            nodes.gailTradingModalBody.innerHTML = `
                <p class="trading-error">${escapeHtml(gailTradingErrorMessage(statusData))}</p>
            `;
            return;
        }
        const s = gailTradingData(statusData, {});
        const portfolio = gailTradingData(portfolioData, {});
        const history = gailTradingItems(historyData, "trades");
        const logs = gailTradingItems(logsData, "logs");
        const issues = gailApiIssuesData(issuesData);
        const issueList = issues && issues.issues && typeof issues.issues === "object"
            ? Object.values(issues.issues)
            : [];

        const paused = s.paused ? "Paused" : (s.enabled ? "Running" : "Disabled");
        const lastEval = s.last_evaluation_at
            ? new Date(s.last_evaluation_at * 1000).toLocaleString()
            : "—";
        const lastTrade = s.last_trade_at
            ? new Date(s.last_trade_at * 1000).toLocaleString()
            : "—";

        const currencies = portfolio && portfolio.currencies
            ? Object.entries(portfolio.currencies)
                .filter(([, b]) => b && b.total > 0)
                .map(([sym, b]) =>
                    `<tr><td>${escapeHtml(sym)}</td><td>${escapeHtml(String(b.total))}</td><td>${b.value_usd != null ? "$" + Number(b.value_usd).toFixed(2) : "—"}</td></tr>`
                ).join("")
            : "";

        const historyRows = history.slice(0, 20).map((t) =>
            `<tr><td>${escapeHtml(t.symbol || "—")}</td><td>${escapeHtml(t.side || "—")}</td><td>${t.amount_usd != null ? "$" + Number(t.amount_usd).toFixed(2) : "—"}</td><td>${escapeHtml(t.exchange || "—")}</td><td>${t.executed_at ? new Date(t.executed_at * 1000).toLocaleString() : "—"}</td></tr>`
        ).join("");

        const logRows = logs.slice(0, 50).map((l) =>
            `<tr><td>${l.timestamp ? new Date(l.timestamp * 1000).toLocaleString() : "—"}</td><td>${escapeHtml(l.level || "")}</td><td>${escapeHtml(l.category || "")}</td><td>${escapeHtml(l.message || "")}</td></tr>`
        ).join("");

        const issueRows = issueList
            .filter((issue) => issue && (issue.active || issue.status === "mitigating"))
            .slice(0, 20)
            .map((issue) => {
                const seen = issue.last_seen_at ? new Date(issue.last_seen_at * 1000).toLocaleString() : "—";
                const retry = issue.next_retry_at ? new Date(issue.next_retry_at * 1000).toLocaleTimeString() : "—";
                return `<tr><td>${escapeHtml(issue.provider || issue.api || "—")}</td><td>${escapeHtml(issue.category || "—")}</td><td>${escapeHtml(issue.status || "—")}</td><td>${escapeHtml(issue.mitigation || issue.summary || "—")}</td><td>${escapeHtml(seen)}</td><td>${escapeHtml(retry)}</td></tr>`;
            })
            .join("");

        nodes.gailTradingModalBody.innerHTML = `
            <div class="trading-status-grid">
                <div class="trading-stat"><span>Status</span><strong>${escapeHtml(paused)}</strong></div>
                <div class="trading-stat"><span>Evaluations</span><strong>${escapeHtml(String(s.evaluation_count || 0))}</strong></div>
                <div class="trading-stat"><span>Trades</span><strong>${escapeHtml(String(s.trade_count || 0))}</strong></div>
                <div class="trading-stat"><span>Last Evaluation</span><strong>${escapeHtml(lastEval)}</strong></div>
                <div class="trading-stat"><span>Last Trade</span><strong>${escapeHtml(lastTrade)}</strong></div>
                <div class="trading-stat"><span>Portfolio Value</span><strong>${portfolio.total_value_usd != null ? "$" + Number(portfolio.total_value_usd).toFixed(2) : "—"}</strong></div>
            </div>
            ${currencies ? `
            <h4>Portfolio Holdings</h4>
            <table class="trading-table"><thead><tr><th>Symbol</th><th>Balance</th><th>USD Value</th></tr></thead><tbody>${currencies}</tbody></table>` : ""}
            ${historyRows ? `
            <h4>Recent Trades</h4>
            <table class="trading-table"><thead><tr><th>Symbol</th><th>Side</th><th>Amount</th><th>Exchange</th><th>Time</th></tr></thead><tbody>${historyRows}</tbody></table>` : ""}
            ${issueRows ? `
            <h4>API Issues Being Mitigated</h4>
            <div class="trading-log-wrap"><table class="trading-table trading-log"><thead><tr><th>Provider</th><th>Category</th><th>Status</th><th>Mitigation</th><th>Seen</th><th>Retry</th></tr></thead><tbody>${issueRows}</tbody></table></div>` : ""}
            ${logRows ? `
            <h4>Activity Log</h4>
            <div class="trading-log-wrap"><table class="trading-table trading-log"><thead><tr><th>Time</th><th>Level</th><th>Category</th><th>Message</th></tr></thead><tbody>${logRows}</tbody></table></div>` : ""}
            ${s.last_error ? `<p class="trading-error">Last error: ${escapeHtml(s.last_error)}</p>` : ""}
        `;
    }

    async function openGailTradingModal() {
        if (!nodes.gailTradingModalBody) {
            return;
        }
        const seq = ++gailTradingModalRequestSeq;
        setGailTradingModalOpen(true);
        if (nodes.gailTradingModalSubtitle) {
            nodes.gailTradingModalSubtitle.textContent = "Loading trading bridge data…";
        }
        nodes.gailTradingModalBody.innerHTML = `<p class="empty">Loading…</p>`;

        const [statusRes, portfolioRes, historyRes, logsRes, issuesRes] = await Promise.all([
            fetchJson("/gail/trading/status"),
            fetchJson("/gail/trading/portfolio"),
            fetchJson("/gail/trading/history"),
            fetchJson("/gail/trading/logs"),
            fetchJson("/gail/status/api-issues")
        ]);

        if (seq !== gailTradingModalRequestSeq) {
            return;
        }
        if (nodes.gailTradingModalSubtitle) {
            nodes.gailTradingModalSubtitle.textContent = "Gail Trading Bridge";
        }
        renderGailTradingModal(statusRes, portfolioRes, historyRes, logsRes, issuesRes);
    }

    function initializeGailTradingUi() {
        if (nodes.gailTradingModalClose) {
            nodes.gailTradingModalClose.addEventListener("click", closeGailTradingModal);
        }
        if (nodes.openGailTradingBtn) {
            nodes.openGailTradingBtn.addEventListener("click", openGailTradingModal);
        }
        if (nodes.gailTradingCard) {
            nodes.gailTradingCard.addEventListener("click", (e) => {
                if (e.target.closest("button")) {
                    return;
                }
                openGailTradingModal();
            });
        }
        if (nodes.gailTradingPauseBtn) {
            nodes.gailTradingPauseBtn.addEventListener("click", async () => {
                nodes.gailTradingPauseBtn.disabled = true;
                await fetchJson("/gail/trading/pause", { method: "POST", headers: { "Content-Type": "application/json" }, body: "{}" });
                nodes.gailTradingPauseBtn.disabled = false;
                openGailTradingModal();
            });
        }
        if (nodes.gailTradingResumeBtn) {
            nodes.gailTradingResumeBtn.addEventListener("click", async () => {
                nodes.gailTradingResumeBtn.disabled = true;
                await fetchJson("/gail/trading/resume", { method: "POST", headers: { "Content-Type": "application/json" }, body: "{}" });
                nodes.gailTradingResumeBtn.disabled = false;
                openGailTradingModal();
            });
        }
        if (nodes.gailTradingEvaluateBtn) {
            nodes.gailTradingEvaluateBtn.addEventListener("click", async () => {
                nodes.gailTradingEvaluateBtn.disabled = true;
                await fetchJson("/gail/trading/evaluate", { method: "POST", headers: { "Content-Type": "application/json" }, body: "{}" });
                nodes.gailTradingEvaluateBtn.disabled = false;
            });
        }
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
            const response = await fetch(withBase(path), {
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
        syncTraceyUrlState();
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

    function formatSignedPercentValue(value, digits = 0, fallback = "-") {
        const parsed = Number(value);
        if (!Number.isFinite(parsed)) {
            return fallback;
        }
        const prefix = parsed > 0 ? "+" : "";
        return `${prefix}${parsed.toFixed(digits)}%`;
    }

    function formatEndpoint(ip, port, fallback = "-") {
        const host = String(ip || "").trim();
        const parsedPort = Number(port);
        if (!host) {
            return fallback;
        }
        if (Number.isFinite(parsedPort) && parsedPort > 0) {
            return `${host}:${Math.round(parsedPort)}`;
        }
        return host;
    }

    function formatPortList(values, fallback = "-") {
        const ports = (Array.isArray(values) ? values : [])
            .map((value) => parseNumber(value, 0))
            .filter((value) => Number.isFinite(value) && value > 0);
        if (!ports.length) {
            return fallback;
        }
        return ports.map((value) => Math.round(value)).join(", ");
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

    function toneClassFromPressure(value) {
        const parsed = parseNumber(value, 0);
        if (parsed >= 0.85) {
            return "tracey-tone-bad";
        }
        if (parsed >= 0.55) {
            return "tracey-tone-warn";
        }
        if (parsed > 0) {
            return "tracey-tone-neutral";
        }
        return "tracey-tone-ok";
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
                label: `Optimise • ${formatActionLabel(optimize.status || "unknown")}`,
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

    function normalizeTraceyTopologyForecastMode(value) {
        const normalized = String(value || "").trim().toLowerCase();
        if (["current", "projected_5m", "projected_15m", "simulation"].includes(normalized)) {
            return normalized;
        }
        return "current";
    }

    function normalizeTraceyTalkerMode(value) {
        const normalized = String(value || "").trim().toLowerCase();
        if (["applications", "ports", "remotes"].includes(normalized)) {
            return normalized;
        }
        return "applications";
    }

    function traceyTopologyForecastLabel(value) {
        const normalized = normalizeTraceyTopologyForecastMode(value);
        if (normalized === "projected_5m") {
            return "5m";
        }
        if (normalized === "projected_15m") {
            return "15m";
        }
        if (normalized === "simulation") {
            return "Sim";
        }
        return "Current";
    }

    function readTraceyTopologyForecast(node) {
        return normalizeTraceyTopologyForecastMode(node ? node.value : "current");
    }

    function readTraceyTalkerMode(node) {
        return normalizeTraceyTalkerMode(node ? node.value : "applications");
    }

    function clamp(value, minValue, maxValue) {
        const numeric = parseNumber(value, minValue);
        if (!Number.isFinite(numeric)) {
            return minValue;
        }
        return Math.max(minValue, Math.min(maxValue, numeric));
    }

    function normalizeTraceyTopologyTab(value) {
        return String(value || "").trim().toLowerCase() === "expansion"
            ? "expansion"
            : "actual";
    }

    function normalizeTraceySimulationStrategy(value) {
        const normalized = String(value || "").trim().toLowerCase();
        if (["balanced", "throughput", "collective"].includes(normalized)) {
            return normalized;
        }
        return "balanced";
    }

    function traceySimulationScopeConfig(scope) {
        return TRACEY_SIMULATION_SCOPE_CONFIG[String(scope || "").trim().toLowerCase()] || null;
    }

    function readTraceyTopologyTab(scope) {
        const config = traceySimulationScopeConfig(scope);
        if (!config || !config.tabs) {
            return "actual";
        }
        return normalizeTraceyTopologyTab(config.tabs.dataset.activeTab || "actual");
    }

    function readTraceySimulationStrategy(scope) {
        const config = traceySimulationScopeConfig(scope);
        return normalizeTraceySimulationStrategy(
            config && config.inputs && config.inputs.strategy ? config.inputs.strategy.value : "balanced"
        );
    }

    function rerenderTraceySimulationScope(scope) {
        const normalized = String(scope || "").trim().toLowerCase();
        if (normalized === "fleet") {
            renderTraceyFleetNetworkSection(traceyState.fleet);
            return;
        }
        if (normalized === "rack") {
            renderTraceyRackNetworkSection(traceyState.selectedRackDetail);
            return;
        }
        if (normalized === "server") {
            if (traceyState.selectedServerTelemetry) {
                renderTraceyServerNetworkSection(traceyState.selectedServerTelemetry);
            }
        }
    }

    function setTraceyTopologyTab(scope, value, options = {}) {
        const config = traceySimulationScopeConfig(scope);
        if (!config) {
            return;
        }
        const normalized = normalizeTraceyTopologyTab(value);
        if (config.tabs) {
            config.tabs.dataset.activeTab = normalized;
        }
        if (config.actualTab) {
            config.actualTab.classList.toggle("is-active", normalized === "actual");
            config.actualTab.setAttribute("aria-selected", normalized === "actual" ? "true" : "false");
        }
        if (config.expansionTab) {
            config.expansionTab.classList.toggle("is-active", normalized === "expansion");
            config.expansionTab.setAttribute("aria-selected", normalized === "expansion" ? "true" : "false");
        }
        if (config.controls) {
            config.controls.hidden = normalized !== "expansion";
        }
        if (config.forecast) {
            config.forecast.disabled = normalized !== "actual";
        }
        if (options.rerender !== false) {
            rerenderTraceySimulationScope(scope);
        }
    }

    function traceyText(value, fallback = "") {
        const text = String(value || "").trim();
        return text || fallback;
    }

    function traceyJoin(values, separator = " • ") {
        return values
            .map((value) => String(value || "").trim())
            .filter(Boolean)
            .join(separator);
    }

    function traceyTrimLabel(value, limit = 26) {
        const text = traceyText(value, "");
        if (!text || text.length <= limit) {
            return text;
        }
        return `${text.slice(0, Math.max(0, limit - 3))}...`;
    }

    function traceyTonePriority(tone) {
        if (tone === "tracey-tone-bad") {
            return 3;
        }
        if (tone === "tracey-tone-warn") {
            return 2;
        }
        if (tone === "tracey-tone-ok") {
            return 1;
        }
        return 0;
    }

    function strongerTraceyTone(currentTone, candidateTone) {
        return traceyTonePriority(candidateTone) > traceyTonePriority(currentTone)
            ? candidateTone
            : currentTone;
    }

    function traceyToneColors(tone) {
        if (tone === "tracey-tone-bad") {
            return {
                stroke: "#ef4444",
                fill: "rgba(239, 68, 68, 0.14)"
            };
        }
        if (tone === "tracey-tone-warn") {
            return {
                stroke: "#f59e0b",
                fill: "rgba(245, 158, 11, 0.14)"
            };
        }
        if (tone === "tracey-tone-ok") {
            return {
                stroke: "#22c55e",
                fill: "rgba(34, 197, 94, 0.14)"
            };
        }
        return {
            stroke: "#94a3b8",
            fill: "rgba(148, 163, 184, 0.14)"
        };
    }

    function traceyItemTone(item) {
        return toneClassFromStatus(item && item.status ? item.status : "unknown");
    }

    function traceyFlowTone(flow) {
        if (flow && flow.anomaly) {
            return "tracey-tone-bad";
        }
        const severityTone = toneClassFromSeverity(flow && flow.severity ? flow.severity : "normal");
        if (severityTone !== "tracey-tone-neutral") {
            return severityTone;
        }
        if (flow && flow.same_lan) {
            return "tracey-tone-ok";
        }
        if (flow && flow.cross_network) {
            return "tracey-tone-warn";
        }
        return "tracey-tone-neutral";
    }

    function traceyProcessTone(process) {
        return toneClassFromSeverity(process && process.severity ? process.severity : "normal");
    }

    function traceyFlowCurrentBps(flow) {
        const totalBps = parseNumber(flow && flow.total_bps, NaN);
        if (Number.isFinite(totalBps) && totalBps > 0) {
            return totalBps;
        }
        return Math.max(0, parseNumber(flow && flow.rx_bps, 0) + parseNumber(flow && flow.tx_bps, 0));
    }

    function traceyLinkMetric(currentBps, forecastBps, forecastMode) {
        return normalizeTraceyTopologyForecastMode(forecastMode) === "current"
            ? currentBps
            : forecastBps;
    }

    function traceyRemoteIdentity(flow, includePort = false) {
        const remoteIp = traceyText(flow && flow.remote_ip, "");
        const remoteMac = traceyText(flow && flow.remote_mac, "");
        const remotePort = Math.max(0, Math.round(parseNumber(flow && flow.remote_port, 0)));
        const remoteLabel = remoteIp || remoteMac || "unresolved";
        const key = includePort && remoteIp
            ? `${remoteIp}:${remotePort > 0 ? remotePort : 0}`
            : (remoteIp || remoteMac || `unresolved:${traceyText(flow && flow.protocol, "net")}`);
        const label = includePort && remoteIp && remotePort > 0
            ? `${remoteIp}:${remotePort}`
            : remoteLabel;
        return {
            key,
            label,
            ip: remoteIp,
            mac: remoteMac,
            port: remotePort > 0 ? remotePort : 0,
            subtitle: traceyJoin([
                remoteIp && remoteMac ? remoteMac : "",
                remotePort > 0 ? `port ${remotePort}` : "",
                flow && flow.cross_network ? "cross-network" : "",
                flow && flow.same_lan ? "same-lan" : "",
                flow && flow.local_host ? "local" : ""
            ]) || "remote endpoint"
        };
    }

    function traceyPortLabel(protocol, port) {
        const normalizedProto = traceyText(protocol, "net").toLowerCase();
        const normalizedPort = Math.max(0, Math.round(parseNumber(port, 0)));
        return normalizedPort > 0 ? `${normalizedProto}/${normalizedPort}` : normalizedProto;
    }

    function extractTraceyNetworkPayload(source) {
        if (source && source.network && typeof source.network === "object") {
            return source.network;
        }
        if (source && source.server && source.server.network && typeof source.server.network === "object") {
            return source.server.network;
        }
        return {};
    }

    function extractTraceyNetworkSummary(source) {
        const network = extractTraceyNetworkPayload(source);
        if (network.summary && typeof network.summary === "object") {
            return network.summary;
        }
        if (source && source.summary && typeof source.summary === "object") {
            return source.summary;
        }
        return {};
    }

    function extractTraceyResourceForecast(source) {
        if (source && source.resource_forecast && typeof source.resource_forecast === "object") {
            return source.resource_forecast;
        }
        return {};
    }

    function extractTraceySimulation(source) {
        const forecast = extractTraceyResourceForecast(source);
        return forecast.simulation && typeof forecast.simulation === "object"
            ? forecast.simulation
            : {};
    }

    function traceyScenarioGrowthScale(growthPctPerMin, minutes) {
        return clamp(1 + ((parseNumber(growthPctPerMin, 0) / 100) * minutes), 0.4, 8);
    }

    function traceyInferExpansionTopology(nodeCount, gpuCount, crossShare, latencyPressure, queuePressure) {
        if (nodeCount <= 1 && gpuCount <= 8) {
            return "Switch";
        }
        if (nodeCount >= 8 || crossShare >= 0.58 || latencyPressure >= 0.48) {
            return "Switch + DoubleBinaryTree";
        }
        if (gpuCount >= 8 || queuePressure >= 0.4 || crossShare >= 0.32) {
            return "Switch + Ring";
        }
        return "Switch";
    }

    function traceyInferExpansionCollective(nodeCount, gpuCount, crossShare, latencyPressure, queuePressure) {
        if (nodeCount >= 8 || crossShare >= 0.6 || latencyPressure >= 0.5) {
            return "DoubleBinaryTree";
        }
        if (gpuCount >= 8 || crossShare >= 0.35 || queuePressure >= 0.35) {
            return "Ring";
        }
        return "SwitchLocal";
    }

    function buildTraceyFallbackExpansionModel(source, scope = "server", overrides = {}) {
        const network = extractTraceyNetworkPayload(source);
        const summary = extractTraceyNetworkSummary(source);
        const resourceForecast = extractTraceyResourceForecast(source);
        const simulation = extractTraceySimulation(source);
        const server = source && source.server && typeof source.server === "object"
            ? source.server
            : {};
        const gpus = Array.isArray(source && source.gpus)
            ? source.gpus
            : (Array.isArray(server.gpus) ? server.gpus : []);
        const processes = Array.isArray(server.processes) ? server.processes : [];
        const topProcesses = Array.isArray(network.top_processes) ? network.top_processes : [];
        const topFlows = Array.isArray(network.top_flows) ? network.top_flows : [];
        const baselineNodeCount = Math.max(1, Math.round(parseNumber(
            overrides.nodeCount,
            parseNumber(source && source.node_count, scope === "server" ? 1 : 1)
        )));
        const baselineGpuCount = Math.max(1, Math.round(parseNumber(
            overrides.gpuCount,
            parseNumber(source && source.gpu_count, gpus.length || parseNumber(server.gpu_count, 1))
        )));

        let cpuCoreEstimate = parseNumber(overrides.cpuCoreEstimate, NaN);
        if (!Number.isFinite(cpuCoreEstimate) || cpuCoreEstimate <= 0) {
            cpuCoreEstimate = parseNumber(simulation.estimated_cpu_cores, NaN);
        }
        if (!Number.isFinite(cpuCoreEstimate) || cpuCoreEstimate <= 0) {
            cpuCoreEstimate = processes.reduce(
                (sum, process) => sum + Math.max(0, parseNumber(process && process.cpu_pct, 0)) / 100,
                0
            );
        }
        if (!Number.isFinite(cpuCoreEstimate) || cpuCoreEstimate <= 0) {
            cpuCoreEstimate = Math.max(
                parseNumber(server.cpu_usage_pct, 0) / 12.5,
                baselineGpuCount * 1.5
            );
        }
        cpuCoreEstimate = Math.max(1, cpuCoreEstimate);

        const attributedTotalBps = Math.max(
            0,
            parseNumber(
                overrides.attributedTotalBps,
                parseNumber(summary.attributed_total_bps, parseNumber(source && source.attributed_total_bps, 0))
            )
        );
        const crossNetworkBps = Math.max(
            0,
            parseNumber(
                overrides.crossNetworkBps,
                parseNumber(summary.cross_network_bps, parseNumber(source && source.cross_network_bps, 0))
            )
        );
        const activeFlows = Math.max(
            0,
            parseNumber(
                overrides.activeFlows,
                parseNumber(summary.active_flows, parseNumber(source && source.network_active_flows, 0))
            )
        );
        const gpuUtilizationAvgPct = clamp(
            parseNumber(
                overrides.gpuUtilizationAvgPct,
                parseNumber(source && source.gpu_utilization_avg_pct, parseNumber(server.gpu_utilization_avg_pct, 0))
            ),
            0,
            100
        );
        const cpuUsagePct = clamp(
            parseNumber(
                overrides.cpuUsagePct,
                parseNumber(source && source.cpu_usage_pct, parseNumber(server.cpu_usage_pct, 0))
            ),
            0,
            100
        );
        const latencyPressure = clamp(
            parseNumber(overrides.latencyPressure, parseNumber(summary.latency_pressure, 0)),
            0,
            1
        );
        const queuePressure = clamp(
            parseNumber(overrides.queuePressure, parseNumber(summary.queue_pressure, 0)),
            0,
            1
        );
        const trafficGrowthPctPerMin = parseNumber(overrides.trafficGrowthPctPerMin, parseNumber(summary.traffic_growth_pct_per_min, 0));
        const crossGrowthPctPerMin = parseNumber(overrides.crossGrowthPctPerMin, parseNumber(summary.cross_network_growth_pct_per_min, 0));
        const flowGrowthPctPerMin = parseNumber(overrides.flowGrowthPctPerMin, parseNumber(summary.flow_growth_pct_per_min, 0));
        const attributionConfidence = clamp(
            parseNumber(overrides.attributionConfidence, parseNumber(summary.attribution_confidence, 0)),
            0,
            1
        );
        const powerW = Math.max(
            0,
            parseNumber(
                overrides.powerW,
                parseNumber(simulation.estimated_power_w, parseNumber(server.gpu_power_total_w, 0))
            )
        );
        const memoryWorkingSetBytes = Math.max(
            0,
            parseNumber(
                overrides.memoryWorkingSetBytes,
                parseNumber(simulation.estimated_memory_working_set_bytes, parseNumber(server.mem_working_set_bytes, parseNumber(server.mem_used_bytes, 0)))
            )
        );

        const dominantProcess = traceyText(
            simulation.dominant_process,
            traceyText(
                topProcesses.reduce((best, process) => {
                    const totalBps = Math.max(0, parseNumber(process && process.total_bps, 0));
                    if (totalBps > parseNumber(best && best.total_bps, -1)) {
                        return process;
                    }
                    return best;
                }, null)?.name,
                traceyText(topProcesses[0] && (topProcesses[0].name || topProcesses[0].exe_path), "")
            )
        );
        const dominantRemoteIp = traceyText(
            simulation.dominant_remote_ip,
            traceyText(
                topFlows.reduce((best, flow) => {
                    const totalBps = Math.max(
                        0,
                        parseNumber(flow && flow.total_bps, parseNumber(flow && flow.rx_bps, 0) + parseNumber(flow && flow.tx_bps, 0))
                    );
                    if (totalBps > parseNumber(best && best.total_bps, -1)) {
                        return flow;
                    }
                    return best;
                }, null)?.remote_ip,
                traceyText(topFlows[0] && topFlows[0].remote_ip, "")
            )
        );

        const crossNetworkShare = attributedTotalBps > 0
            ? clamp(crossNetworkBps / attributedTotalBps, 0, 1)
            : 0;
        const sampleCount = Math.max(0, Math.round(parseNumber(resourceForecast.sample_count, 0)));
        const nodeScaleExponent = clamp(1.02 + (crossNetworkShare * 0.20) + (latencyPressure * 0.10) + (queuePressure * 0.06), 1.02, 1.42);
        const gpuScaleExponent = clamp(0.80 + (crossNetworkShare * 0.08) + (baselineGpuCount > 8 ? 0.06 : 0), 0.75, 1.12);
        const cpuScaleExponent = clamp(0.68 + (queuePressure * 0.12) + (clamp(cpuUsagePct / 100, 0, 1) * 0.10), 0.68, 1.0);
        const collectiveScaleExponent = clamp(0.16 + (crossNetworkShare * 0.28) + Math.min(0.18, Math.log2(Math.max(2, baselineGpuCount)) / 10), 0.16, 0.90);
        const crossNetworkBias = clamp(0.45 + (crossNetworkShare * 0.75) + (Math.max(0, trafficGrowthPctPerMin) / 100 * 1.50), 0.45, 1.75);
        const confidence = clamp(
            0.18 + (attributionConfidence * 0.52) + (sampleCount > 0 ? 0.10 : 0) + (activeFlows > 0 ? 0.08 : 0) + (dominantProcess ? 0.05 : 0),
            0.10,
            0.98
        );

        const recommendedNodes = Math.max(
            baselineNodeCount,
            Math.min(
                24,
                baselineNodeCount + Math.max(
                    1,
                    Math.ceil(baselineNodeCount * (0.55 + (crossNetworkShare * 0.90) + (Math.max(0, trafficGrowthPctPerMin) / 40)))
                )
            )
        );
        const recommendedGpus = Math.max(
            baselineGpuCount,
            Math.min(
                96,
                baselineGpuCount + Math.max(
                    4,
                    Math.ceil(baselineGpuCount * (crossNetworkShare >= 0.35 ? 0.8 : 1.2))
                )
            )
        );
        const recommendedCpuCores = Math.ceil(cpuCoreEstimate * (1.35 + (queuePressure * 0.50) + (crossNetworkShare * 0.25)));
        const averageGpusPerNode = Math.max(1, Math.round(baselineGpuCount / Math.max(1, baselineNodeCount)));

        const model = {
            version: 1,
            source: "dashboard.heuristic",
            generated_epoch_ms: Date.now(),
            baseline: {
                node_count: baselineNodeCount,
                gpu_count: baselineGpuCount,
                cpu_core_estimate: cpuCoreEstimate,
                attributed_total_bps: attributedTotalBps,
                cross_network_bps: crossNetworkBps,
                cross_network_share: crossNetworkShare,
                active_flows: activeFlows,
                gpu_utilization_avg_pct: gpuUtilizationAvgPct,
                cpu_usage_pct: cpuUsagePct,
                memory_working_set_bytes: memoryWorkingSetBytes,
                power_w: powerW,
                latency_pressure: latencyPressure,
                queue_pressure: queuePressure
            },
            recommended_targets: {
                node_count: recommendedNodes,
                gpu_count: recommendedGpus,
                cpu_core_estimate: recommendedCpuCores
            },
            maximum_targets: {
                node_count: Math.max(recommendedNodes, baselineNodeCount * 12, 24),
                gpu_count: Math.max(recommendedGpus, baselineGpuCount * 6, 96),
                cpu_core_estimate: Math.max(recommendedCpuCores, Math.ceil(cpuCoreEstimate * 6), 96)
            },
            heuristics: {
                inferred_topology: traceyInferExpansionTopology(baselineNodeCount, baselineGpuCount, crossNetworkShare, latencyPressure, queuePressure),
                recommended_collective: traceyInferExpansionCollective(recommendedNodes, recommendedGpus, crossNetworkShare, latencyPressure, queuePressure),
                node_scale_exponent: nodeScaleExponent,
                gpu_scale_exponent: gpuScaleExponent,
                cpu_scale_exponent: cpuScaleExponent,
                collective_scale_exponent: collectiveScaleExponent,
                cross_network_bias: crossNetworkBias,
                latency_penalty: clamp(0.12 + (latencyPressure * 0.88), 0.12, 1),
                queue_penalty: clamp(0.10 + (queuePressure * 0.90), 0.10, 1),
                traffic_growth_pct_per_min: trafficGrowthPctPerMin,
                cross_network_growth_pct_per_min: crossGrowthPctPerMin,
                flow_growth_pct_per_min: flowGrowthPctPerMin,
                attribution_confidence: attributionConfidence,
                confidence,
                dominant_process: dominantProcess,
                dominant_remote_ip: dominantRemoteIp
            },
            dimensions: [
                {
                    name: "intra_node",
                    topology: "Switch",
                    participant_count: Math.max(1, Math.min(averageGpusPerNode, 8)),
                    bandwidth_bias: clamp(1 - (crossNetworkShare * 0.35), 0.55, 1.15),
                    latency_bias: clamp(1 + (latencyPressure * 0.35), 1, 1.45)
                },
                {
                    name: "inter_node",
                    topology: traceyInferExpansionCollective(recommendedNodes, recommendedGpus, crossNetworkShare, latencyPressure, queuePressure) === "DoubleBinaryTree"
                        ? "DoubleBinaryTree"
                        : "Ring",
                    participant_count: recommendedNodes,
                    bandwidth_bias: clamp(0.70 + (crossNetworkShare * 0.55), 0.55, 1.40),
                    latency_bias: clamp(1 + (latencyPressure * 0.80) + (queuePressure * 0.20), 1, 1.95)
                },
                {
                    name: "host_orchestration",
                    topology: "FullyConnected",
                    participant_count: Math.max(1, Math.round(recommendedCpuCores)),
                    bandwidth_bias: clamp(0.35 + (queuePressure * 0.45), 0.30, 1.0),
                    latency_bias: clamp(1 + (queuePressure * 0.75), 1, 1.75)
                }
            ]
        };

        model.recommended_simulation = traceyEstimateExpansionScenario(
            model,
            {
                nodeCount: recommendedNodes,
                gpuCount: recommendedGpus,
                cpuCoreEstimate: recommendedCpuCores
            },
            "balanced"
        );
        return model;
    }

    function extractTraceyExpansionModel(source, scope = "server", overrides = {}) {
        const forecast = extractTraceyResourceForecast(source);
        if (forecast.continuum_expansion && typeof forecast.continuum_expansion === "object") {
            return forecast.continuum_expansion;
        }
        if (forecast.continuumExpansion && typeof forecast.continuumExpansion === "object") {
            return forecast.continuumExpansion;
        }
        return buildTraceyFallbackExpansionModel(source, scope, overrides);
    }

    function buildTraceyAggregateExpansionModel(items, scope = "fleet") {
        const safeItems = Array.isArray(items) ? items : [];
        if (!safeItems.length) {
            return buildTraceyFallbackExpansionModel({}, scope, {
                nodeCount: 1,
                gpuCount: 1,
                cpuCoreEstimate: 4
            });
        }

        let gpuCountTotal = 0;
        let cpuCoreTotal = 0;
        let attributedTotalBps = 0;
        let crossNetworkBps = 0;
        let activeFlows = 0;
        let memoryWorkingSetBytes = 0;
        let powerW = 0;
        let gpuUtilizationWeighted = 0;
        let cpuUsageSum = 0;
        let latencyWeighted = 0;
        let queueWeighted = 0;
        let attributionConfidenceWeighted = 0;
        let trafficGrowthWeighted = 0;
        let crossGrowthWeighted = 0;
        let flowGrowthWeighted = 0;
        let weightSum = 0;
        let modelCount = 0;
        let nodeScaleExponentSum = 0;
        let gpuScaleExponentSum = 0;
        let cpuScaleExponentSum = 0;
        let collectiveScaleExponentSum = 0;
        let crossNetworkBiasSum = 0;
        let latencyPenaltySum = 0;
        let queuePenaltySum = 0;
        let confidenceSum = 0;
        let dominantProcess = "";
        let dominantRemoteIp = "";
        let dominantWeight = -1;

        for (const item of safeItems) {
            const model = extractTraceyExpansionModel(item, "server");
            const base = model && model.baseline && typeof model.baseline === "object" ? model.baseline : {};
            const heuristics = model && model.heuristics && typeof model.heuristics === "object" ? model.heuristics : {};
            const networkWeight = Math.max(
                1,
                parseNumber(base.attributed_total_bps, parseNumber(extractTraceyNetworkSummary(item).attributed_total_bps, 0))
            );
            const itemGpuCount = Math.max(1, Math.round(parseNumber(base.gpu_count, parseNumber(item && item.gpu_count, Array.isArray(item && item.gpus) ? item.gpus.length : 1))));
            const itemCpuUsage = clamp(parseNumber(base.cpu_usage_pct, parseNumber(item && item.cpu_usage_pct, parseNumber(item && item.summary && item.summary.cpu_usage_pct, parseNumber(item && item.server && item.server.cpu_usage_pct, 0)))), 0, 100);
            const itemGpuUtil = clamp(parseNumber(base.gpu_utilization_avg_pct, parseNumber(item && item.gpu_utilization_avg_pct, parseNumber(item && item.summary && item.summary.gpu_utilization_avg_pct, parseNumber(item && item.server && item.server.gpu_utilization_avg_pct, 0)))), 0, 100);

            gpuCountTotal += itemGpuCount;
            cpuCoreTotal += Math.max(1, parseNumber(base.cpu_core_estimate, parseNumber(extractTraceySimulation(item).estimated_cpu_cores, itemGpuCount * 1.5)));
            attributedTotalBps += Math.max(0, parseNumber(base.attributed_total_bps, parseNumber(extractTraceyNetworkSummary(item).attributed_total_bps, 0)));
            crossNetworkBps += Math.max(0, parseNumber(base.cross_network_bps, parseNumber(extractTraceyNetworkSummary(item).cross_network_bps, 0)));
            activeFlows += Math.max(0, parseNumber(base.active_flows, parseNumber(extractTraceyNetworkSummary(item).active_flows, 0)));
            memoryWorkingSetBytes += Math.max(0, parseNumber(base.memory_working_set_bytes, parseNumber(extractTraceySimulation(item).estimated_memory_working_set_bytes, 0)));
            powerW += Math.max(0, parseNumber(base.power_w, parseNumber(extractTraceySimulation(item).estimated_power_w, parseNumber(item && item.server && item.server.gpu_power_total_w, 0))));
            gpuUtilizationWeighted += itemGpuUtil * itemGpuCount;
            cpuUsageSum += itemCpuUsage;
            latencyWeighted += clamp(parseNumber(base.latency_pressure, parseNumber(extractTraceyNetworkSummary(item).latency_pressure, 0)), 0, 1) * networkWeight;
            queueWeighted += clamp(parseNumber(base.queue_pressure, parseNumber(extractTraceyNetworkSummary(item).queue_pressure, 0)), 0, 1) * networkWeight;
            attributionConfidenceWeighted += clamp(parseNumber(heuristics.attribution_confidence, parseNumber(base.attribution_confidence, parseNumber(extractTraceyNetworkSummary(item).attribution_confidence, 0))), 0, 1) * networkWeight;
            trafficGrowthWeighted += parseNumber(heuristics.traffic_growth_pct_per_min, parseNumber(extractTraceyNetworkSummary(item).traffic_growth_pct_per_min, 0)) * networkWeight;
            crossGrowthWeighted += parseNumber(heuristics.cross_network_growth_pct_per_min, parseNumber(extractTraceyNetworkSummary(item).cross_network_growth_pct_per_min, 0)) * networkWeight;
            flowGrowthWeighted += parseNumber(heuristics.flow_growth_pct_per_min, parseNumber(extractTraceyNetworkSummary(item).flow_growth_pct_per_min, 0)) * networkWeight;
            weightSum += networkWeight;
            if (heuristics && Object.keys(heuristics).length) {
                modelCount += 1;
                nodeScaleExponentSum += parseNumber(heuristics.node_scale_exponent, 1.08);
                gpuScaleExponentSum += parseNumber(heuristics.gpu_scale_exponent, 0.82);
                cpuScaleExponentSum += parseNumber(heuristics.cpu_scale_exponent, 0.74);
                collectiveScaleExponentSum += parseNumber(heuristics.collective_scale_exponent, 0.18);
                crossNetworkBiasSum += parseNumber(heuristics.cross_network_bias, 0.70);
                latencyPenaltySum += parseNumber(heuristics.latency_penalty, 0.15);
                queuePenaltySum += parseNumber(heuristics.queue_penalty, 0.15);
                confidenceSum += parseNumber(heuristics.confidence, 0.5);
            }
            if (networkWeight > dominantWeight) {
                dominantWeight = networkWeight;
                dominantProcess = traceyText(heuristics.dominant_process, dominantProcess);
                dominantRemoteIp = traceyText(heuristics.dominant_remote_ip, dominantRemoteIp);
            }
        }

        const aggregateSource = {
            server: {
                cpu_usage_pct: safeItems.length ? cpuUsageSum / safeItems.length : 0,
                gpu_utilization_avg_pct: gpuCountTotal ? gpuUtilizationWeighted / gpuCountTotal : 0,
                gpu_power_total_w: powerW,
                mem_working_set_bytes: memoryWorkingSetBytes,
                network: {
                    summary: {
                        attributed_total_bps: attributedTotalBps,
                        cross_network_bps: crossNetworkBps,
                        active_flows: activeFlows,
                        attribution_confidence: weightSum ? attributionConfidenceWeighted / weightSum : 0,
                        latency_pressure: weightSum ? latencyWeighted / weightSum : 0,
                        queue_pressure: weightSum ? queueWeighted / weightSum : 0,
                        traffic_growth_pct_per_min: weightSum ? trafficGrowthWeighted / weightSum : 0,
                        cross_network_growth_pct_per_min: weightSum ? crossGrowthWeighted / weightSum : 0,
                        flow_growth_pct_per_min: weightSum ? flowGrowthWeighted / weightSum : 0
                    }
                }
            },
            resource_forecast: {
                simulation: {
                    estimated_cpu_cores: cpuCoreTotal,
                    estimated_memory_working_set_bytes: memoryWorkingSetBytes,
                    estimated_power_w: powerW,
                    dominant_process: dominantProcess,
                    dominant_remote_ip: dominantRemoteIp
                }
            },
            gpus: Array.from({ length: Math.max(1, gpuCountTotal) }, () => ({}))
        };
        const model = buildTraceyFallbackExpansionModel(aggregateSource, scope, {
            nodeCount: Math.max(1, safeItems.length),
            gpuCount: gpuCountTotal,
            cpuCoreEstimate: cpuCoreTotal,
            memoryWorkingSetBytes,
            powerW,
            gpuUtilizationAvgPct: gpuCountTotal ? gpuUtilizationWeighted / gpuCountTotal : 0,
            cpuUsagePct: safeItems.length ? cpuUsageSum / safeItems.length : 0,
            attributedTotalBps,
            crossNetworkBps,
            activeFlows,
            latencyPressure: weightSum ? latencyWeighted / weightSum : 0,
            queuePressure: weightSum ? queueWeighted / weightSum : 0,
            attributionConfidence: weightSum ? attributionConfidenceWeighted / weightSum : 0,
            trafficGrowthPctPerMin: weightSum ? trafficGrowthWeighted / weightSum : 0,
            crossGrowthPctPerMin: weightSum ? crossGrowthWeighted / weightSum : 0,
            flowGrowthPctPerMin: weightSum ? flowGrowthWeighted / weightSum : 0
        });

        if (modelCount > 0) {
            model.heuristics.node_scale_exponent = nodeScaleExponentSum / modelCount;
            model.heuristics.gpu_scale_exponent = gpuScaleExponentSum / modelCount;
            model.heuristics.cpu_scale_exponent = cpuScaleExponentSum / modelCount;
            model.heuristics.collective_scale_exponent = collectiveScaleExponentSum / modelCount;
            model.heuristics.cross_network_bias = crossNetworkBiasSum / modelCount;
            model.heuristics.latency_penalty = latencyPenaltySum / modelCount;
            model.heuristics.queue_penalty = queuePenaltySum / modelCount;
            model.heuristics.confidence = confidenceSum / modelCount;
            model.heuristics.dominant_process = dominantProcess;
            model.heuristics.dominant_remote_ip = dominantRemoteIp;
            model.recommended_simulation = traceyEstimateExpansionScenario(
                model,
                {
                    nodeCount: parseNumber(model.recommended_targets && model.recommended_targets.node_count, safeItems.length),
                    gpuCount: parseNumber(model.recommended_targets && model.recommended_targets.gpu_count, gpuCountTotal),
                    cpuCoreEstimate: parseNumber(model.recommended_targets && model.recommended_targets.cpu_core_estimate, cpuCoreTotal)
                },
                "balanced"
            );
        }
        return model;
    }

    function traceyExpansionModelSignature(model) {
        const baseline = model && model.baseline ? model.baseline : {};
        const recommended = model && model.recommended_targets ? model.recommended_targets : {};
        return [
            Math.round(parseNumber(baseline.node_count, 0)),
            Math.round(parseNumber(baseline.gpu_count, 0)),
            Math.round(parseNumber(baseline.cpu_core_estimate, 0)),
            Math.round(parseNumber(recommended.node_count, 0)),
            Math.round(parseNumber(recommended.gpu_count, 0)),
            Math.round(parseNumber(recommended.cpu_core_estimate, 0))
        ].join("|");
    }

    function syncTraceySimulationInputBounds(scope, model, options = {}) {
        const config = traceySimulationScopeConfig(scope);
        if (!config || !model || !config.inputs) {
            return;
        }
        const baseline = model.baseline && typeof model.baseline === "object" ? model.baseline : {};
        const recommended = model.recommended_targets && typeof model.recommended_targets === "object"
            ? model.recommended_targets
            : {};
        const maximum = model.maximum_targets && typeof model.maximum_targets === "object"
            ? model.maximum_targets
            : {};
        const signature = traceyExpansionModelSignature(model);
        const force = options.force === true || !config.controls || config.controls.dataset.modelSignature !== signature;
        if (config.controls) {
            config.controls.dataset.modelSignature = signature;
        }

        const bounds = {
            nodes: {
                min: Math.max(1, Math.round(parseNumber(baseline.node_count, 1))),
                max: Math.max(2, Math.round(parseNumber(maximum.node_count, parseNumber(recommended.node_count, 4)))),
                recommended: Math.max(1, Math.round(parseNumber(recommended.node_count, parseNumber(baseline.node_count, 1))))
            },
            gpus: {
                min: Math.max(1, Math.round(parseNumber(baseline.gpu_count, 1))),
                max: Math.max(2, Math.round(parseNumber(maximum.gpu_count, parseNumber(recommended.gpu_count, 8)))),
                recommended: Math.max(1, Math.round(parseNumber(recommended.gpu_count, parseNumber(baseline.gpu_count, 1))))
            },
            cores: {
                min: Math.max(1, Math.round(parseNumber(baseline.cpu_core_estimate, 1))),
                max: Math.max(2, Math.round(parseNumber(maximum.cpu_core_estimate, parseNumber(recommended.cpu_core_estimate, 16)))),
                recommended: Math.max(1, Math.round(parseNumber(recommended.cpu_core_estimate, parseNumber(baseline.cpu_core_estimate, 1))))
            }
        };

        ["nodes", "gpus", "cores"].forEach((key) => {
            const input = config.inputs[key];
            if (!input) {
                return;
            }
            input.min = String(bounds[key].min);
            input.max = String(bounds[key].max);
            if (force || input.dataset.userTouched !== "true") {
                input.value = String(bounds[key].recommended);
                input.dataset.userTouched = "false";
            } else {
                input.value = String(Math.round(clamp(parseNumber(input.value, bounds[key].recommended), bounds[key].min, bounds[key].max)));
            }
        });
        if (config.inputs.strategy) {
            if (force || config.inputs.strategy.dataset.userTouched !== "true") {
                config.inputs.strategy.value = "balanced";
                config.inputs.strategy.dataset.userTouched = "false";
            }
        }
    }

    function readTraceySimulationTargets(scope, model) {
        const config = traceySimulationScopeConfig(scope);
        const baseline = model && model.baseline ? model.baseline : {};
        const recommended = model && model.recommended_targets ? model.recommended_targets : {};
        const maximum = model && model.maximum_targets ? model.maximum_targets : {};
        const defaultNodes = Math.max(1, Math.round(parseNumber(recommended.node_count, parseNumber(baseline.node_count, 1))));
        const defaultGpus = Math.max(1, Math.round(parseNumber(recommended.gpu_count, parseNumber(baseline.gpu_count, 1))));
        const defaultCores = Math.max(1, Math.round(parseNumber(recommended.cpu_core_estimate, parseNumber(baseline.cpu_core_estimate, 1))));
        return {
            nodeCount: Math.round(clamp(
                parseNumber(config && config.inputs && config.inputs.nodes ? config.inputs.nodes.value : defaultNodes, defaultNodes),
                Math.max(1, Math.round(parseNumber(baseline.node_count, 1))),
                Math.max(defaultNodes, Math.round(parseNumber(maximum.node_count, defaultNodes)))
            )),
            gpuCount: Math.round(clamp(
                parseNumber(config && config.inputs && config.inputs.gpus ? config.inputs.gpus.value : defaultGpus, defaultGpus),
                Math.max(1, Math.round(parseNumber(baseline.gpu_count, 1))),
                Math.max(defaultGpus, Math.round(parseNumber(maximum.gpu_count, defaultGpus)))
            )),
            cpuCoreEstimate: Math.round(clamp(
                parseNumber(config && config.inputs && config.inputs.cores ? config.inputs.cores.value : defaultCores, defaultCores),
                Math.max(1, Math.round(parseNumber(baseline.cpu_core_estimate, 1))),
                Math.max(defaultCores, Math.round(parseNumber(maximum.cpu_core_estimate, defaultCores)))
            ))
        };
    }

    function applyTraceyRecommendedTargets(scope, model) {
        syncTraceySimulationInputBounds(scope, model, { force: true });
    }

    function traceyEstimateExpansionScenario(model, targets, strategy = "balanced") {
        const baseline = model && model.baseline ? model.baseline : {};
        const heuristics = model && model.heuristics ? model.heuristics : {};
        const normalizedStrategy = normalizeTraceySimulationStrategy(strategy);
        const strategyBias = normalizedStrategy === "throughput"
            ? { network: 1.12, cross: 1.08, gpu: 1.08, cpu: 1.05, collective: 1.05 }
            : (normalizedStrategy === "collective"
                ? { network: 0.96, cross: 1.16, gpu: 1.02, cpu: 1.10, collective: 1.18 }
                : { network: 1.0, cross: 1.0, gpu: 1.0, cpu: 1.0, collective: 1.0 });

        const baselineNodes = Math.max(1, Math.round(parseNumber(baseline.node_count, 1)));
        const baselineGpus = Math.max(1, Math.round(parseNumber(baseline.gpu_count, 1)));
        const baselineCpuCores = Math.max(1, parseNumber(baseline.cpu_core_estimate, 1));
        const baselineNetworkBps = Math.max(0, parseNumber(baseline.attributed_total_bps, 0));
        const baselineCrossNetworkBps = Math.max(0, parseNumber(baseline.cross_network_bps, 0));
        const baselineActiveFlows = Math.max(0, parseNumber(baseline.active_flows, 0));
        const baselineGpuUtil = clamp(parseNumber(baseline.gpu_utilization_avg_pct, 0), 0, 100);
        const baselineCpuUsage = clamp(parseNumber(baseline.cpu_usage_pct, 0), 0, 100);
        const baselineMemoryBytes = Math.max(0, parseNumber(baseline.memory_working_set_bytes, 0));
        const baselinePowerW = Math.max(0, parseNumber(baseline.power_w, 0));
        const baselineLatencyPressure = clamp(parseNumber(baseline.latency_pressure, 0), 0, 1);
        const baselineQueuePressure = clamp(parseNumber(baseline.queue_pressure, 0), 0, 1);
        const baseCrossShare = clamp(parseNumber(baseline.cross_network_share, baselineNetworkBps > 0 ? baselineCrossNetworkBps / baselineNetworkBps : 0), 0, 1);
        const growthPctPerMin = parseNumber(heuristics.traffic_growth_pct_per_min, 0);

        const targetNodes = Math.max(baselineNodes, Math.round(parseNumber(targets && targets.nodeCount, baselineNodes)));
        const targetGpus = Math.max(baselineGpus, Math.round(parseNumber(targets && targets.gpuCount, baselineGpus)));
        const targetCpuCores = Math.max(baselineCpuCores, parseNumber(targets && targets.cpuCoreEstimate, baselineCpuCores));
        const nodeRatio = Math.max(1, targetNodes / baselineNodes);
        const gpuRatio = Math.max(1, targetGpus / baselineGpus);
        const cpuRatio = Math.max(1, targetCpuCores / baselineCpuCores);

        const nodeScaleExponent = clamp(parseNumber(heuristics.node_scale_exponent, 1.08) * strategyBias.network, 0.7, 2.0);
        const gpuScaleExponent = clamp(parseNumber(heuristics.gpu_scale_exponent, 0.82) * strategyBias.gpu, 0.6, 1.5);
        const cpuScaleExponent = clamp(parseNumber(heuristics.cpu_scale_exponent, 0.74) * strategyBias.cpu, 0.5, 1.4);
        const collectiveScaleExponent = clamp(parseNumber(heuristics.collective_scale_exponent, 0.18) * strategyBias.collective, 0.05, 1.4);
        const crossNetworkBias = clamp(parseNumber(heuristics.cross_network_bias, 0.70) * strategyBias.cross, 0.05, 2.5);
        const latencyPenalty = clamp(parseNumber(heuristics.latency_penalty, 0.15), 0, 1.5);
        const queuePenalty = clamp(parseNumber(heuristics.queue_penalty, 0.15), 0, 1.5);
        const collectiveFactor = 1 + (collectiveScaleExponent * Math.log2(Math.max(1, nodeRatio)));
        const weightedScale = (0.46 * Math.pow(nodeRatio, nodeScaleExponent)) +
            (0.36 * Math.pow(gpuRatio, gpuScaleExponent)) +
            (0.18 * Math.pow(cpuRatio, cpuScaleExponent));
        const growthScale = traceyScenarioGrowthScale(growthPctPerMin, 12);
        const pressureScale = 1 +
            (latencyPenalty * Math.max(0, nodeRatio - 1) * 0.08) +
            (queuePenalty * Math.max(0, gpuRatio - 1) * 0.05);
        const estimatedNetworkBps = baselineNetworkBps > 0
            ? baselineNetworkBps * weightedScale * collectiveFactor * growthScale * pressureScale * strategyBias.network
            : 0;
        const estimatedCrossShare = clamp(
            baseCrossShare +
            (crossNetworkBias * Math.max(0, nodeRatio - 1) * 0.09) +
            (Math.max(0, gpuRatio - 1) * 0.03),
            0.02,
            0.97
        );
        const estimatedCrossNetworkBps = estimatedNetworkBps * estimatedCrossShare;
        const estimatedActiveFlows = baselineActiveFlows > 0
            ? baselineActiveFlows * ((0.40 * nodeRatio) + (0.35 * gpuRatio) + (0.25 * cpuRatio)) * (1 + (estimatedCrossShare * 0.20))
            : 0;
        const estimatedGpuUtilizationAvgPct = clamp(
            baselineGpuUtil * (0.84 + (Math.max(0, gpuRatio - 1) * 0.12) + ((collectiveFactor - 1) * 0.18)),
            4,
            99
        );
        const estimatedCpuUsagePct = clamp(
            (baselineCpuUsage * (0.78 + (Math.max(0, cpuRatio - 1) * 0.22))) +
            (Math.max(0, nodeRatio - 1) * 6) +
            (Math.max(0, gpuRatio - 1) * 3),
            3,
            99
        );
        const estimatedMemoryWorkingSetBytes = baselineMemoryBytes > 0
            ? baselineMemoryBytes * Math.max(1, (0.55 * gpuRatio) + (0.45 * cpuRatio))
            : 0;
        const estimatedPowerW = baselinePowerW > 0
            ? baselinePowerW * Math.max(
                1,
                (0.48 * gpuRatio) +
                (0.24 * nodeRatio) +
                (0.28 * (estimatedGpuUtilizationAvgPct / Math.max(1, baselineGpuUtil || 1)))
            )
            : 0;
        const estimatedLatencyPressure = clamp(
            baselineLatencyPressure +
            (Math.log2(Math.max(1, nodeRatio)) * 0.12) +
            (estimatedCrossShare * 0.10) +
            (Math.max(0, gpuRatio - 1) * 0.02),
            0,
            1
        );
        const estimatedQueuePressure = clamp(
            baselineQueuePressure +
            (Math.log2(Math.max(1, Math.max(gpuRatio, cpuRatio))) * 0.10) +
            (estimatedCrossShare * 0.06),
            0,
            1
        );
        const topology = traceyInferExpansionTopology(
            targetNodes,
            targetGpus,
            estimatedCrossShare,
            estimatedLatencyPressure,
            estimatedQueuePressure
        );
        const recommendedCollective = normalizedStrategy === "collective"
            ? "DoubleBinaryTree"
            : traceyInferExpansionCollective(
                targetNodes,
                targetGpus,
                estimatedCrossShare,
                estimatedLatencyPressure,
                estimatedQueuePressure
            );
        const confidence = clamp(
            parseNumber(heuristics.confidence, parseNumber(heuristics.attribution_confidence, 0.5)) *
                (normalizedStrategy === "collective" ? 0.96 : 1.0),
            0.10,
            0.99
        );
        const dominantProcess = traceyText(heuristics.dominant_process, "");
        const dominantRemoteIp = traceyText(heuristics.dominant_remote_ip, "");
        const focus = dominantProcess
            ? `${dominantProcess} driving ${topology}`
            : (dominantRemoteIp ? `${dominantRemoteIp} on ${recommendedCollective}` : `${topology} fabric`);

        return {
            targetNodes,
            targetGpus,
            targetCpuCores,
            strategy: normalizedStrategy,
            estimatedNetworkBps,
            estimatedCrossNetworkBps,
            estimatedActiveFlows,
            estimatedGpuUtilizationAvgPct,
            estimatedCpuUsagePct,
            estimatedMemoryWorkingSetBytes,
            estimatedPowerW,
            estimatedLatencyPressure,
            estimatedQueuePressure,
            collectivePenalty: Math.max(0, collectiveFactor - 1),
            crossNetworkShare: estimatedCrossShare,
            topology,
            recommendedCollective,
            confidence,
            dominantProcess,
            dominantRemoteIp,
            simulationFocus: focus,
            extraNetworkBps: Math.max(0, estimatedNetworkBps - baselineNetworkBps),
            extraNodes: Math.max(0, targetNodes - baselineNodes),
            extraGpus: Math.max(0, targetGpus - baselineGpus),
            extraCpuCores: Math.max(0, targetCpuCores - baselineCpuCores)
        };
    }

    function buildTraceySimulationFactRows(model, scenario) {
        if (!model || !scenario) {
            return [];
        }
        const warningTone = toneClassFromPressure(Math.max(
            parseNumber(scenario.estimatedLatencyPressure, 0),
            parseNumber(scenario.estimatedQueuePressure, 0)
        ));
        return [
            {
                label: "Targets",
                value: `${formatCount(scenario.targetNodes, "0")} nodes / ${formatCount(scenario.targetGpus, "0")} GPU`,
                subtitle: `${formatCount(scenario.targetCpuCores, "0")} CPU cores • ${formatActionLabel(scenario.strategy)}`,
                tone: "tracey-tone-neutral"
            },
            {
                label: "Fabric Load",
                value: formatBytesRate(scenario.estimatedNetworkBps),
                subtitle: `cross ${formatBytesRate(scenario.estimatedCrossNetworkBps)} • ${formatCount(scenario.estimatedActiveFlows, "0")} flows`,
                tone: parseNumber(scenario.estimatedNetworkBps, 0) > parseNumber(model.baseline && model.baseline.attributed_total_bps, 0)
                    ? "tracey-tone-warn"
                    : "tracey-tone-neutral"
            },
            {
                label: "Utilization",
                value: `${formatPercentValue(scenario.estimatedGpuUtilizationAvgPct, 0)} GPU`,
                subtitle: `CPU ${formatPercentValue(scenario.estimatedCpuUsagePct, 0)} • ${formatPower(scenario.estimatedPowerW)}`,
                tone: parseNumber(scenario.estimatedGpuUtilizationAvgPct, 0) >= 85 ? "tracey-tone-warn" : "tracey-tone-ok"
            },
            {
                label: "Pressure",
                value: `${formatRatioPercent(scenario.estimatedLatencyPressure, 0, "-")} / ${formatRatioPercent(scenario.estimatedQueuePressure, 0, "-")}`,
                subtitle: `latency / queue • penalty x${formatFixed(1 + parseNumber(scenario.collectivePenalty, 0), 2, "1.00")}`,
                tone: warningTone
            },
            {
                label: "Topology",
                value: traceyText(scenario.topology, "Heuristic"),
                subtitle: `${traceyText(scenario.recommendedCollective, "Ring")} collective • conf ${formatFixed(scenario.confidence, 2, "-")}`,
                tone: "tracey-tone-neutral"
            },
            {
                label: "Focus",
                value: traceyText(scenario.dominantProcess, traceyText(scenario.dominantRemoteIp, "Heuristic fabric")),
                subtitle: traceyText(scenario.simulationFocus, "No dominant talker identified."),
                tone: "tracey-tone-neutral"
            }
        ];
    }

    function buildTraceyActualNetworkFactRows(model) {
        if (!model) {
            return [];
        }
        const baseline = model.baseline && typeof model.baseline === "object" ? model.baseline : {};
        const heuristics = model.heuristics && typeof model.heuristics === "object" ? model.heuristics : {};
        const pressureTone = toneClassFromPressure(Math.max(
            parseNumber(baseline.latency_pressure, 0),
            parseNumber(baseline.queue_pressure, 0)
        ));
        return [
            {
                label: "Live Fabric",
                value: formatBytesRate(baseline.attributed_total_bps),
                subtitle: `cross ${formatBytesRate(baseline.cross_network_bps)} • ${formatCount(baseline.active_flows, "0")} flows`,
                tone: parseNumber(baseline.attributed_total_bps, 0) > 0 ? "tracey-tone-neutral" : "tracey-tone-warn"
            },
            {
                label: "Utilization",
                value: `${formatPercentValue(baseline.gpu_utilization_avg_pct, 0)} GPU`,
                subtitle: `CPU ${formatPercentValue(baseline.cpu_usage_pct, 0)} • ${formatPower(baseline.power_w)}`,
                tone: parseNumber(baseline.gpu_utilization_avg_pct, 0) >= 85 ? "tracey-tone-warn" : "tracey-tone-ok"
            },
            {
                label: "Capacity",
                value: `${formatCount(baseline.node_count, "0")} nodes / ${formatCount(baseline.gpu_count, "0")} GPU`,
                subtitle: `${formatCount(baseline.cpu_core_estimate, "0")} CPU cores • ${formatBytes(baseline.memory_working_set_bytes)}`,
                tone: "tracey-tone-neutral"
            },
            {
                label: "Pressure",
                value: `${formatRatioPercent(baseline.latency_pressure, 0, "-")} / ${formatRatioPercent(baseline.queue_pressure, 0, "-")}`,
                subtitle: `latency / queue • cross share ${formatRatioPercent(baseline.cross_network_share, 0, "-")}`,
                tone: pressureTone
            },
            {
                label: "Topology",
                value: traceyText(heuristics.inferred_topology, "Observed"),
                subtitle: `${traceyText(heuristics.recommended_collective, "Ring")} collective • conf ${formatFixed(heuristics.confidence, 2, "-")}`,
                tone: "tracey-tone-neutral"
            },
            {
                label: "Focus",
                value: traceyText(heuristics.dominant_process, traceyText(heuristics.dominant_remote_ip, "No dominant talker")),
                subtitle: traceyText(heuristics.dominant_remote_ip, "Live Tracey network attribution"),
                tone: "tracey-tone-neutral"
            }
        ];
    }

    // Per-flow projections stay client-side by scaling current flow records with the host forecast ratio.
    function traceyForecastScale(source, forecastMode) {
        const mode = normalizeTraceyTopologyForecastMode(forecastMode);
        if (mode === "current") {
            return 1;
        }
        const summary = extractTraceyNetworkSummary(source);
        const forecast = extractTraceyResourceForecast(source);
        const simulation = extractTraceySimulation(source);
        const baseline = Math.max(0, parseNumber(
            summary.attributed_total_bps,
            parseNumber(source && source.attributed_total_bps, 0)
        ));
        let target = baseline;
        if (mode === "projected_5m") {
            target = Math.max(0, parseNumber(forecast.projected_total_bps_5m, baseline));
        } else if (mode === "projected_15m") {
            target = Math.max(0, parseNumber(forecast.projected_total_bps_15m, baseline));
        } else if (mode === "simulation") {
            target = Math.max(0, parseNumber(simulation.estimated_network_bps, baseline));
        }
        if (baseline > 0 && target >= 0) {
            return Math.max(0.15, Math.min(12, target / baseline));
        }
        return 1;
    }

    function buildTraceyScopedTopology(items, forecastMode, options = {}) {
        const mode = normalizeTraceyTopologyForecastMode(forecastMode);
        const linksByKey = new Map();
        const backendCounts = new Map();
        const allLocalKeys = new Set();
        const allRemoteKeys = new Set();
        const stats = {
            currentTotal: 0,
            forecastTotal: 0,
            avgConfidenceSum: 0,
            confidenceCount: 0,
            activeFlows: 0,
            estimatedFlows: 0,
            udpDrops: 0,
            scaleMax: 1
        };

        for (const item of Array.isArray(items) ? items : []) {
            const network = extractTraceyNetworkPayload(item);
            const summary = extractTraceyNetworkSummary(item);
            const flows = Array.isArray(network.top_flows) ? network.top_flows : [];
            const scale = traceyForecastScale(item, mode);
            const localKey = traceyText(options.localKey ? options.localKey(item) : "", "local");
            const localLabel = traceyText(options.localLabel ? options.localLabel(item) : localKey, localKey);
            const localSubtitle = traceyText(options.localSubtitle ? options.localSubtitle(item) : "", "");
            const localTone = traceyText(options.localTone ? options.localTone(item) : traceyItemTone(item), "tracey-tone-neutral");
            const confidence = parseNumber(
                summary.attribution_confidence,
                parseNumber(item && item.network_attribution_confidence, NaN)
            );
            const backend = traceyText(
                summary.collector_backend,
                traceyText(item && item.network_collector_backend, "")
            );

            allLocalKeys.add(localKey);
            stats.scaleMax = Math.max(stats.scaleMax, scale);
            stats.activeFlows += Math.max(0, parseNumber(summary.active_flows, parseNumber(item && item.network_active_flows, 0)));
            stats.estimatedFlows += Math.max(0, parseNumber(summary.estimated_flows, parseNumber(item && item.network_estimated_flows, 0)));
            stats.udpDrops += Math.max(0, parseNumber(summary.udp_drop_delta, parseNumber(item && item.network_udp_drop_delta, 0)));
            if (Number.isFinite(confidence) && confidence >= 0) {
                stats.avgConfidenceSum += confidence;
                stats.confidenceCount += 1;
            }
            if (backend) {
                backendCounts.set(backend, (backendCounts.get(backend) || 0) + 1);
            }

            for (const flow of flows) {
                const currentBps = traceyFlowCurrentBps(flow);
                if (currentBps <= 0) {
                    continue;
                }
                const forecastBps = currentBps * scale;
                const remote = traceyRemoteIdentity(flow, Boolean(options.includeRemotePort));
                const linkKey = `${localKey}=>${remote.key}`;
                const flowTone = traceyFlowTone(flow);
                allRemoteKeys.add(remote.key);
                stats.currentTotal += currentBps;
                stats.forecastTotal += forecastBps;
                if (!linksByKey.has(linkKey)) {
                    linksByKey.set(linkKey, {
                        id: linkKey,
                        localId: localKey,
                        localLabel,
                        localSubtitle,
                        localTone,
                        remoteId: remote.key,
                        remoteLabel: remote.label,
                        remoteIp: remote.ip,
                        remotePort: remote.port,
                        remoteMac: remote.mac,
                        remoteSubtitle: remote.subtitle,
                        remoteTone: flowTone,
                        currentBps: 0,
                        forecastBps: 0,
                        flowCount: 0,
                        tone: flowTone,
                        crossNetwork: false,
                        sameLan: false,
                        localHost: false,
                        localPorts: new Set(),
                        remotePorts: new Set(),
                        localMacs: new Set(),
                        remoteMacs: new Set()
                    });
                }
                const entry = linksByKey.get(linkKey);
                entry.currentBps += currentBps;
                entry.forecastBps += forecastBps;
                entry.flowCount += 1;
                entry.crossNetwork = entry.crossNetwork || Boolean(flow && flow.cross_network);
                entry.sameLan = entry.sameLan || Boolean(flow && flow.same_lan);
                entry.localHost = entry.localHost || Boolean(flow && flow.local_host);
                entry.localTone = strongerTraceyTone(entry.localTone, localTone);
                entry.remoteTone = strongerTraceyTone(entry.remoteTone, flowTone);
                entry.tone = strongerTraceyTone(entry.tone, flowTone);
                if (parseNumber(flow && flow.local_port, 0) > 0) {
                    entry.localPorts.add(traceyPortLabel(flow.protocol, flow.local_port));
                }
                if (parseNumber(flow && flow.remote_port, 0) > 0) {
                    entry.remotePorts.add(traceyPortLabel(flow.protocol, flow.remote_port));
                }
                if (traceyText(flow && flow.local_mac, "")) {
                    entry.localMacs.add(traceyText(flow.local_mac, ""));
                }
                if (traceyText(flow && flow.remote_mac, "")) {
                    entry.remoteMacs.add(traceyText(flow.remote_mac, ""));
                }
            }
        }

        const allLinks = Array.from(linksByKey.values())
            .map((link) => ({
                ...link,
                localPorts: Array.from(link.localPorts.values()),
                remotePorts: Array.from(link.remotePorts.values()),
                localMacs: Array.from(link.localMacs.values()),
                remoteMacs: Array.from(link.remoteMacs.values())
            }))
            .sort((left, right) => traceyLinkMetric(right.currentBps, right.forecastBps, mode) - traceyLinkMetric(left.currentBps, left.forecastBps, mode));
        const maxLinks = Math.max(1, parseNumber(options.maxLinks, 18));
        const maxNodesPerSide = Math.max(2, parseNumber(options.maxNodesPerSide, 7));
        const localMetrics = new Map();
        const remoteMetrics = new Map();
        for (const link of allLinks) {
            const metric = traceyLinkMetric(link.currentBps, link.forecastBps, mode);
            localMetrics.set(link.localId, (localMetrics.get(link.localId) || 0) + metric);
            remoteMetrics.set(link.remoteId, (remoteMetrics.get(link.remoteId) || 0) + metric);
        }
        const visibleLocalKeys = new Set(
            Array.from(localMetrics.entries())
                .sort((left, right) => right[1] - left[1])
                .slice(0, maxNodesPerSide)
                .map(([key]) => key)
        );
        const visibleRemoteKeys = new Set(
            Array.from(remoteMetrics.entries())
                .sort((left, right) => right[1] - left[1])
                .slice(0, maxNodesPerSide)
                .map(([key]) => key)
        );
        const visibleLinks = allLinks
            .filter((link) => visibleLocalKeys.has(link.localId) && visibleRemoteKeys.has(link.remoteId))
            .slice(0, maxLinks);
        if (!visibleLinks.length && allLinks.length) {
            visibleLinks.push(...allLinks.slice(0, maxLinks));
        }
        const localNodes = new Map();
        const remoteNodes = new Map();
        for (const link of visibleLinks) {
            if (!localNodes.has(link.localId)) {
                localNodes.set(link.localId, {
                        id: link.localId,
                        label: link.localLabel,
                        subtitle: link.localSubtitle,
                        tone: link.localTone,
                        currentBps: 0,
                        forecastBps: 0,
                        flowCount: 0
                    });
                }
                if (!remoteNodes.has(link.remoteId)) {
                    remoteNodes.set(link.remoteId, {
                        id: link.remoteId,
                        label: link.remoteLabel,
                        subtitle: traceyJoin([
                            link.remoteSubtitle,
                            link.remoteMacs.length ? link.remoteMacs[0] : ""
                        ]),
                        remoteIp: traceyText(link.remoteIp, ""),
                        remotePort: Math.max(0, parseNumber(link.remotePort, 0)),
                        remoteMac: traceyText(link.remoteMac, link.remoteMacs.length ? link.remoteMacs[0] : ""),
                        tone: link.remoteTone,
                        currentBps: 0,
                        forecastBps: 0,
                        flowCount: 0
                    });
            }
            const localNode = localNodes.get(link.localId);
            localNode.currentBps += link.currentBps;
            localNode.forecastBps += link.forecastBps;
            localNode.flowCount += link.flowCount;
            localNode.tone = strongerTraceyTone(localNode.tone, link.localTone);
            const remoteNode = remoteNodes.get(link.remoteId);
            remoteNode.currentBps += link.currentBps;
            remoteNode.forecastBps += link.forecastBps;
            remoteNode.flowCount += link.flowCount;
            remoteNode.tone = strongerTraceyTone(remoteNode.tone, link.remoteTone);
        }

        let dominantBackend = "";
        let dominantBackendCount = -1;
        for (const [backend, count] of backendCounts.entries()) {
            if (count > dominantBackendCount) {
                dominantBackend = backend;
                dominantBackendCount = count;
            }
        }

        return {
            forecastMode: mode,
            localNodes: Array.from(localNodes.values()).sort((left, right) => traceyLinkMetric(right.currentBps, right.forecastBps, mode) - traceyLinkMetric(left.currentBps, left.forecastBps, mode)),
            remoteNodes: Array.from(remoteNodes.values()).sort((left, right) => traceyLinkMetric(right.currentBps, right.forecastBps, mode) - traceyLinkMetric(left.currentBps, left.forecastBps, mode)),
            links: visibleLinks,
            stats: {
                currentTotal: stats.currentTotal,
                forecastTotal: stats.forecastTotal,
                activeFlows: stats.activeFlows,
                estimatedFlows: stats.estimatedFlows,
                udpDrops: stats.udpDrops,
                avgConfidence: stats.confidenceCount > 0 ? stats.avgConfidenceSum / stats.confidenceCount : NaN,
                hiddenLinks: Math.max(0, allLinks.length - visibleLinks.length),
                hiddenLocalNodes: Math.max(0, allLocalKeys.size - localNodes.size),
                hiddenRemoteNodes: Math.max(0, allRemoteKeys.size - remoteNodes.size),
                backend: dominantBackend,
                scaleMax: stats.scaleMax
            }
        };
    }

    function buildTraceyServerTopology(data, forecastMode, talkerMode) {
        const mode = normalizeTraceyTopologyForecastMode(forecastMode);
        const network = extractTraceyNetworkPayload(data);
        const summary = extractTraceyNetworkSummary(data);
        const resourceForecast = extractTraceyResourceForecast(data);
        const simulation = extractTraceySimulation(data);
        const hostLabel = traceyText(data && (data.host || data.agent_id), "server");
        const hostSubtitle = traceyJoin([
            traceyText(data && data.rack, ""),
            traceyText(data && data.zone, "")
        ]);
        const linksByKey = new Map();
        const allLocalKeys = new Set();
        const allRemoteKeys = new Set();
        const scale = traceyForecastScale(data, mode);
        const normalizedTalkerMode = normalizeTraceyTalkerMode(talkerMode);

        for (const flow of Array.isArray(network.top_flows) ? network.top_flows : []) {
            const currentBps = traceyFlowCurrentBps(flow);
            if (currentBps <= 0) {
                continue;
            }
            const forecastBps = currentBps * scale;
            const remote = traceyRemoteIdentity(flow, true);
            let localId = `server:${hostLabel}`;
            let localLabel = hostLabel;
            let localSubtitle = hostSubtitle;
            let localKind = "server";
            let localProcess = "";
            let localPid = 0;
            let localProtocol = traceyText(flow && flow.protocol, "");
            let localPort = Math.max(0, Math.round(parseNumber(flow && flow.local_port, 0)));
            if (normalizedTalkerMode === "applications") {
                localId = `proc:${parseNumber(flow.pid, 0)}:${traceyText(flow.process, "unknown")}`;
                localLabel = traceyText(flow.process, "unknown");
                localSubtitle = traceyJoin([
                    parseNumber(flow.pid, 0) > 0 ? `pid ${Math.round(parseNumber(flow.pid, 0))}` : "",
                    parseNumber(flow.local_port, 0) > 0 ? traceyPortLabel(flow.protocol, flow.local_port) : ""
                ]);
                localKind = "process";
                localProcess = traceyText(flow.process, "");
                localPid = Math.max(0, Math.round(parseNumber(flow.pid, 0)));
            } else if (normalizedTalkerMode === "ports") {
                localId = `port:${traceyPortLabel(flow.protocol, flow.local_port)}`;
                localLabel = traceyPortLabel(flow.protocol, flow.local_port);
                localSubtitle = traceyText(flow.process, hostLabel);
                localKind = "port";
                localProcess = traceyText(flow.process, "");
            }
            const linkKey = `${localId}=>${remote.key}`;
            const flowTone = traceyFlowTone(flow);
            allLocalKeys.add(localId);
            allRemoteKeys.add(remote.key);
            if (!linksByKey.has(linkKey)) {
                linksByKey.set(linkKey, {
                        id: linkKey,
                        localId,
                        localLabel,
                        localSubtitle,
                        localKind,
                        localProcess,
                        localPid,
                        localProtocol,
                        localPort,
                        localTone: strongerTraceyTone(traceyItemTone(data), flowTone),
                        remoteId: remote.key,
                        remoteLabel: remote.label,
                        remoteIp: remote.ip,
                        remotePort: remote.port,
                        remoteMac: remote.mac,
                        remoteSubtitle: traceyJoin([
                            remote.subtitle,
                            traceyText(flow && flow.remote_mac, "")
                        ]),
                    remoteTone: flowTone,
                    currentBps: 0,
                    forecastBps: 0,
                    flowCount: 0,
                    tone: flowTone,
                    crossNetwork: false,
                    sameLan: false,
                    localHost: false
                });
            }
            const entry = linksByKey.get(linkKey);
            entry.currentBps += currentBps;
            entry.forecastBps += forecastBps;
            entry.flowCount += 1;
            entry.crossNetwork = entry.crossNetwork || Boolean(flow && flow.cross_network);
            entry.sameLan = entry.sameLan || Boolean(flow && flow.same_lan);
            entry.localHost = entry.localHost || Boolean(flow && flow.local_host);
            entry.localTone = strongerTraceyTone(entry.localTone, flowTone);
            entry.remoteTone = strongerTraceyTone(entry.remoteTone, flowTone);
            entry.tone = strongerTraceyTone(entry.tone, flowTone);
        }

        const allLinks = Array.from(linksByKey.values())
            .sort((left, right) => traceyLinkMetric(right.currentBps, right.forecastBps, mode) - traceyLinkMetric(left.currentBps, left.forecastBps, mode));
        const localMetrics = new Map();
        const remoteMetrics = new Map();
        for (const link of allLinks) {
            const metric = traceyLinkMetric(link.currentBps, link.forecastBps, mode);
            localMetrics.set(link.localId, (localMetrics.get(link.localId) || 0) + metric);
            remoteMetrics.set(link.remoteId, (remoteMetrics.get(link.remoteId) || 0) + metric);
        }
        const visibleLocalKeys = new Set(
            Array.from(localMetrics.entries())
                .sort((left, right) => right[1] - left[1])
                .slice(0, 6)
                .map(([key]) => key)
        );
        const visibleRemoteKeys = new Set(
            Array.from(remoteMetrics.entries())
                .sort((left, right) => right[1] - left[1])
                .slice(0, 7)
                .map(([key]) => key)
        );
        const visibleLinks = allLinks
            .filter((link) => visibleLocalKeys.has(link.localId) && visibleRemoteKeys.has(link.remoteId))
            .slice(0, 18);
        if (!visibleLinks.length && allLinks.length) {
            visibleLinks.push(...allLinks.slice(0, 18));
        }
        const localNodes = new Map();
        const remoteNodes = new Map();
        for (const link of visibleLinks) {
            if (!localNodes.has(link.localId)) {
                localNodes.set(link.localId, {
                    id: link.localId,
                    label: link.localLabel,
                    subtitle: link.localSubtitle,
                    localKind: link.localKind,
                    localProcess: link.localProcess,
                    localPid: link.localPid,
                    localProtocol: link.localProtocol,
                    localPort: link.localPort,
                    tone: link.localTone,
                    currentBps: 0,
                    forecastBps: 0,
                    flowCount: 0
                });
            }
            if (!remoteNodes.has(link.remoteId)) {
                remoteNodes.set(link.remoteId, {
                    id: link.remoteId,
                    label: link.remoteLabel,
                    subtitle: link.remoteSubtitle,
                    remoteIp: traceyText(link.remoteIp, ""),
                    remotePort: Math.max(0, parseNumber(link.remotePort, 0)),
                    remoteMac: traceyText(link.remoteMac, ""),
                    tone: link.remoteTone,
                    currentBps: 0,
                    forecastBps: 0,
                    flowCount: 0
                });
            }
            const localNode = localNodes.get(link.localId);
            localNode.currentBps += link.currentBps;
            localNode.forecastBps += link.forecastBps;
            localNode.flowCount += link.flowCount;
            const remoteNode = remoteNodes.get(link.remoteId);
            remoteNode.currentBps += link.currentBps;
            remoteNode.forecastBps += link.forecastBps;
            remoteNode.flowCount += link.flowCount;
        }

        return {
            forecastMode: mode,
            localNodes: Array.from(localNodes.values()).sort((left, right) => traceyLinkMetric(right.currentBps, right.forecastBps, mode) - traceyLinkMetric(left.currentBps, left.forecastBps, mode)),
            remoteNodes: Array.from(remoteNodes.values()).sort((left, right) => traceyLinkMetric(right.currentBps, right.forecastBps, mode) - traceyLinkMetric(left.currentBps, left.forecastBps, mode)),
            links: visibleLinks,
            stats: {
                currentTotal: Math.max(0, parseNumber(summary.attributed_total_bps, 0)),
                forecastTotal: mode === "current"
                    ? Math.max(0, parseNumber(summary.attributed_total_bps, 0))
                    : (mode === "projected_5m"
                        ? Math.max(0, parseNumber(resourceForecast.projected_total_bps_5m, parseNumber(summary.attributed_total_bps, 0)))
                        : (mode === "projected_15m"
                            ? Math.max(0, parseNumber(resourceForecast.projected_total_bps_15m, parseNumber(summary.attributed_total_bps, 0)))
                            : Math.max(0, parseNumber(simulation.estimated_network_bps, parseNumber(summary.attributed_total_bps, 0))))),
                activeFlows: Math.max(0, parseNumber(summary.active_flows, 0)),
                estimatedFlows: Math.max(0, parseNumber(summary.estimated_flows, 0)),
                udpDrops: Math.max(0, parseNumber(summary.udp_drop_delta, 0)),
                avgConfidence: parseNumber(summary.attribution_confidence, NaN),
                hiddenLinks: Math.max(0, allLinks.length - visibleLinks.length),
                hiddenLocalNodes: Math.max(0, allLocalKeys.size - localNodes.size),
                hiddenRemoteNodes: Math.max(0, allRemoteKeys.size - remoteNodes.size),
                backend: traceyText(summary.collector_backend, ""),
                scaleMax: scale,
                simulationFocus: traceyText(simulation.dominant_process, traceyText(simulation.dominant_remote_ip, ""))
            }
        };
    }

    function buildTraceyScopedTalkers(items, forecastMode, talkerMode, options = {}) {
        const mode = normalizeTraceyTopologyForecastMode(forecastMode);
        const normalizedTalkerMode = normalizeTraceyTalkerMode(talkerMode);
        const talkers = new Map();
        for (const item of Array.isArray(items) ? items : []) {
            const network = extractTraceyNetworkPayload(item);
            const scale = traceyForecastScale(item, mode);
            const scopeLabel = traceyText(options.scopeLabel ? options.scopeLabel(item) : "", traceyText(item && (item.host || item.agent_id || item.rack), "node"));
            if (normalizedTalkerMode === "applications") {
                for (const process of Array.isArray(network.top_processes) ? network.top_processes : []) {
                    const label = traceyText(process.name, traceyText(process.exe_path, "unknown"));
                    const currentBps = Math.max(0, parseNumber(process.total_bps, 0));
                    if (!label || currentBps <= 0) {
                        continue;
                    }
                    const key = `app:${label.toLowerCase()}`;
                    if (!talkers.has(key)) {
                        talkers.set(key, {
                            key,
                            label,
                            detail: "",
                            tone: traceyProcessTone(process),
                            currentBps: 0,
                            forecastBps: 0,
                            flowCount: 0,
                            listenerCount: 0,
                            scopeNodes: new Set(),
                            remotes: new Set()
                        });
                    }
                    const entry = talkers.get(key);
                    entry.currentBps += currentBps;
                    entry.forecastBps += currentBps * scale;
                    entry.flowCount += Math.max(0, parseNumber(process.flow_count, 0));
                    entry.listenerCount += Math.max(0, parseNumber(process.listener_count, 0));
                    entry.scopeNodes.add(scopeLabel);
                    if (traceyText(process.dominant_remote_ip, "")) {
                        entry.remotes.add(traceyText(process.dominant_remote_ip, ""));
                    }
                    entry.tone = strongerTraceyTone(entry.tone, traceyProcessTone(process));
                    entry.detail = traceyJoin([
                        entry.remotes.size ? Array.from(entry.remotes.values()).slice(0, 2).join(", ") : "",
                        process.cgroup ? `cg ${process.cgroup}` : ""
                    ]);
                }
            } else {
                for (const flow of Array.isArray(network.top_flows) ? network.top_flows : []) {
                    const currentBps = traceyFlowCurrentBps(flow);
                    if (currentBps <= 0) {
                        continue;
                    }
                    const remote = traceyRemoteIdentity(flow, true);
                    const key = normalizedTalkerMode === "ports"
                        ? `port:${traceyPortLabel(flow.protocol, flow.local_port)}`
                        : `remote:${remote.key}`;
                    if (!talkers.has(key)) {
                        talkers.set(key, {
                            key,
                            label: normalizedTalkerMode === "ports" ? traceyPortLabel(flow.protocol, flow.local_port) : remote.label,
                            detail: "",
                            tone: traceyFlowTone(flow),
                            currentBps: 0,
                            forecastBps: 0,
                            flowCount: 0,
                            listenerCount: 0,
                            scopeNodes: new Set(),
                            remotes: new Set(),
                            processes: new Set()
                        });
                    }
                    const entry = talkers.get(key);
                    entry.currentBps += currentBps;
                    entry.forecastBps += currentBps * scale;
                    entry.flowCount += 1;
                    entry.scopeNodes.add(scopeLabel);
                    if (traceyText(flow.process, "")) {
                        entry.processes.add(traceyText(flow.process, ""));
                    }
                    if (remote.label) {
                        entry.remotes.add(remote.label);
                    }
                    entry.tone = strongerTraceyTone(entry.tone, traceyFlowTone(flow));
                    entry.detail = normalizedTalkerMode === "ports"
                        ? traceyJoin([
                            entry.processes.size ? Array.from(entry.processes.values()).slice(0, 2).join(", ") : "",
                            entry.remotes.size ? `${entry.remotes.size} remotes` : ""
                        ])
                        : traceyJoin([
                            entry.processes.size ? Array.from(entry.processes.values()).slice(0, 2).join(", ") : "",
                            remote.subtitle
                        ]);
                }
            }
        }

        return Array.from(talkers.values())
            .map((entry) => ({
                ...entry,
                scopeLabel: `${entry.scopeNodes.size} nodes • ${formatCount(entry.flowCount, "0")} flows`
            }))
            .sort((left, right) => traceyLinkMetric(right.currentBps, right.forecastBps, mode) - traceyLinkMetric(left.currentBps, left.forecastBps, mode));
    }

    function buildTraceySimulationTopologyFromDataset(baseDataset, model, scenario, options = {}) {
        const base = baseDataset && typeof baseDataset === "object"
            ? baseDataset
            : { localNodes: [], remoteNodes: [], links: [], stats: {} };
        const baseline = model && model.baseline ? model.baseline : {};
        const baselineTotal = Math.max(0, parseNumber(base.stats && base.stats.currentTotal, 0));
        const baselineFlows = Math.max(0, parseNumber(base.stats && base.stats.activeFlows, 0));
        const scale = baselineTotal > 0
            ? clamp(parseNumber(scenario && scenario.estimatedNetworkBps, baselineTotal) / baselineTotal, 0.15, 24)
            : 1;
        const mode = "simulation";
        const allLinks = Array.isArray(base.links)
            ? base.links.map((link) => ({
                ...link,
                currentBps: Math.max(0, parseNumber(link && link.currentBps, 0)),
                forecastBps: Math.max(0, parseNumber(link && link.currentBps, 0) * scale),
                flowCount: Math.max(0, parseNumber(link && link.flowCount, 0)),
                localPorts: Array.isArray(link && link.localPorts) ? link.localPorts.slice() : [],
                remotePorts: Array.isArray(link && link.remotePorts) ? link.remotePorts.slice() : [],
                localMacs: Array.isArray(link && link.localMacs) ? link.localMacs.slice() : [],
                remoteMacs: Array.isArray(link && link.remoteMacs) ? link.remoteMacs.slice() : []
            }))
            : [];

        const targetNodes = Math.max(1, Math.round(parseNumber(scenario && scenario.targetNodes, parseNumber(baseline.node_count, 1))));
        const targetGpus = Math.max(1, Math.round(parseNumber(scenario && scenario.targetGpus, parseNumber(baseline.gpu_count, 1))));
        const baselineNodes = Math.max(1, Math.round(parseNumber(baseline.node_count, Math.max(1, parseNumber(base.localNodes && base.localNodes.length, 1)))));
        const baselineGpus = Math.max(1, Math.round(parseNumber(baseline.gpu_count, 1)));
        const extraNetworkBps = Math.max(0, parseNumber(scenario && scenario.extraNetworkBps, 0));
        const extraNodeCount = Math.max(0, targetNodes - baselineNodes);
        const extraGpuCount = Math.max(0, targetGpus - baselineGpus);

        if (extraNetworkBps > 0) {
            const remoteSeeds = Array.isArray(base.remoteNodes)
                ? base.remoteNodes
                    .slice()
                    .sort((left, right) => traceyLinkMetric(right.currentBps, right.forecastBps, base.forecastMode || "current") - traceyLinkMetric(left.currentBps, left.forecastBps, base.forecastMode || "current"))
                    .slice(0, 3)
                : [];
            if (!remoteSeeds.length) {
                remoteSeeds.push({
                    id: `synthetic-remote:${traceyText(options.scopeKey, "scope")}`,
                    label: "Collective Fabric",
                    subtitle: traceyText(scenario && scenario.recommendedCollective, "Heuristic"),
                    remoteIp: "",
                    remotePort: 0,
                    remoteMac: "",
                    tone: "tracey-tone-neutral",
                    currentBps: 0,
                    forecastBps: 0,
                    flowCount: 0
                });
            }
            const cohortCount = Math.max(
                1,
                Math.min(
                    3,
                    extraNodeCount > 0
                        ? extraNodeCount
                        : Math.ceil(extraGpuCount / Math.max(1, Math.round(baselineGpus / baselineNodes)))
                )
            );
            const nodesPerCohort = extraNodeCount > 0 ? Math.max(1, Math.ceil(extraNodeCount / cohortCount)) : 1;
            const gpusPerCohort = extraGpuCount > 0
                ? Math.max(1, Math.ceil(extraGpuCount / cohortCount))
                : Math.max(1, Math.round(targetGpus / Math.max(1, targetNodes)));
            const remoteWeightTotal = remoteSeeds.reduce(
                (sum, node) => sum + Math.max(1, parseNumber(node && node.currentBps, 0)),
                0
            );
            for (let index = 0; index < cohortCount; index += 1) {
                const localId = `synthetic-local:${traceyText(options.scopeKey, "scope")}:${index}`;
                const cohortMeta = {
                    nodes: nodesPerCohort,
                    gpus: gpusPerCohort
                };
                const localLabel = typeof options.syntheticLocalLabel === "function"
                    ? options.syntheticLocalLabel(index, cohortMeta, scenario)
                    : `Expansion +${nodesPerCohort}`;
                const localSubtitle = typeof options.syntheticLocalSubtitle === "function"
                    ? options.syntheticLocalSubtitle(index, cohortMeta, scenario)
                    : `${gpusPerCohort} GPUs • ${traceyText(scenario && scenario.topology, "Heuristic")}`;
                remoteSeeds.forEach((remoteNode, remoteIndex) => {
                    const weight = remoteWeightTotal > 0
                        ? Math.max(1, parseNumber(remoteNode && remoteNode.currentBps, 0)) / remoteWeightTotal
                        : (1 / remoteSeeds.length);
                    const linkForecastBps = (extraNetworkBps / cohortCount) * weight;
                    const linkFlowCount = Math.max(
                        1,
                        Math.round(
                            Math.max(1, parseNumber(scenario && scenario.estimatedActiveFlows, 0) - baselineFlows) /
                            cohortCount *
                            weight
                        )
                    );
                    allLinks.push({
                        id: `${localId}=>${traceyText(remoteNode && remoteNode.id, `synthetic-remote:${remoteIndex}`)}:simulation`,
                        localId,
                        localLabel,
                        localSubtitle,
                        localKind: traceyText(options.syntheticLocalKind, "expansion"),
                        localProcess: "",
                        localPid: 0,
                        localProtocol: "",
                        localPort: 0,
                        localTone: "tracey-tone-neutral",
                        remoteId: traceyText(remoteNode && remoteNode.id, `synthetic-remote:${remoteIndex}`),
                        remoteLabel: traceyText(remoteNode && remoteNode.label, "Collective Fabric"),
                        remoteIp: traceyText(remoteNode && remoteNode.remoteIp, ""),
                        remotePort: Math.max(0, parseNumber(remoteNode && remoteNode.remotePort, 0)),
                        remoteMac: traceyText(remoteNode && remoteNode.remoteMac, ""),
                        remoteSubtitle: traceyText(remoteNode && remoteNode.subtitle, traceyText(scenario && scenario.recommendedCollective, "")),
                        remoteTone: traceyText(remoteNode && remoteNode.tone, "tracey-tone-neutral"),
                        currentBps: 0,
                        forecastBps: linkForecastBps,
                        flowCount: linkFlowCount,
                        tone: "tracey-tone-neutral",
                        crossNetwork: true,
                        sameLan: false,
                        localHost: false,
                        localPorts: [],
                        remotePorts: [],
                        localMacs: [],
                        remoteMacs: [],
                        synthetic: true
                    });
                });
            }
        }

        allLinks.sort((left, right) => traceyLinkMetric(right.currentBps, right.forecastBps, mode) - traceyLinkMetric(left.currentBps, left.forecastBps, mode));
        const maxLinks = Math.max(1, parseNumber(options.maxLinks, 20));
        const maxNodesPerSide = Math.max(2, parseNumber(options.maxNodesPerSide, 7));
        const localMetrics = new Map();
        const remoteMetrics = new Map();
        for (const link of allLinks) {
            const metric = traceyLinkMetric(link.currentBps, link.forecastBps, mode);
            localMetrics.set(link.localId, (localMetrics.get(link.localId) || 0) + metric);
            remoteMetrics.set(link.remoteId, (remoteMetrics.get(link.remoteId) || 0) + metric);
        }
        const visibleLocalKeys = new Set(
            Array.from(localMetrics.entries())
                .sort((left, right) => right[1] - left[1])
                .slice(0, maxNodesPerSide)
                .map(([key]) => key)
        );
        const visibleRemoteKeys = new Set(
            Array.from(remoteMetrics.entries())
                .sort((left, right) => right[1] - left[1])
                .slice(0, maxNodesPerSide)
                .map(([key]) => key)
        );
        const visibleLinks = allLinks
            .filter((link) => visibleLocalKeys.has(link.localId) && visibleRemoteKeys.has(link.remoteId))
            .slice(0, maxLinks);
        if (!visibleLinks.length && allLinks.length) {
            visibleLinks.push(...allLinks.slice(0, maxLinks));
        }

        const localNodes = new Map();
        const remoteNodes = new Map();
        for (const link of visibleLinks) {
            if (!localNodes.has(link.localId)) {
                localNodes.set(link.localId, {
                    id: link.localId,
                    label: link.localLabel,
                    subtitle: link.localSubtitle,
                    localKind: traceyText(link.localKind, ""),
                    localProcess: traceyText(link.localProcess, ""),
                    localPid: Math.max(0, parseNumber(link.localPid, 0)),
                    localProtocol: traceyText(link.localProtocol, ""),
                    localPort: Math.max(0, parseNumber(link.localPort, 0)),
                    tone: traceyText(link.localTone, "tracey-tone-neutral"),
                    currentBps: 0,
                    forecastBps: 0,
                    flowCount: 0
                });
            }
            if (!remoteNodes.has(link.remoteId)) {
                remoteNodes.set(link.remoteId, {
                    id: link.remoteId,
                    label: link.remoteLabel,
                    subtitle: link.remoteSubtitle,
                    remoteIp: traceyText(link.remoteIp, ""),
                    remotePort: Math.max(0, parseNumber(link.remotePort, 0)),
                    remoteMac: traceyText(link.remoteMac, ""),
                    tone: traceyText(link.remoteTone, "tracey-tone-neutral"),
                    currentBps: 0,
                    forecastBps: 0,
                    flowCount: 0
                });
            }
            const localNode = localNodes.get(link.localId);
            localNode.currentBps += Math.max(0, parseNumber(link.currentBps, 0));
            localNode.forecastBps += Math.max(0, parseNumber(link.forecastBps, 0));
            localNode.flowCount += Math.max(0, parseNumber(link.flowCount, 0));
            localNode.tone = strongerTraceyTone(localNode.tone, traceyText(link.localTone, localNode.tone));

            const remoteNode = remoteNodes.get(link.remoteId);
            remoteNode.currentBps += Math.max(0, parseNumber(link.currentBps, 0));
            remoteNode.forecastBps += Math.max(0, parseNumber(link.forecastBps, 0));
            remoteNode.flowCount += Math.max(0, parseNumber(link.flowCount, 0));
            remoteNode.tone = strongerTraceyTone(remoteNode.tone, traceyText(link.remoteTone, remoteNode.tone));
        }

        return {
            forecastMode: mode,
            localCaption: traceyText(options.localCaption, "Current + Expansion"),
            remoteCaption: traceyText(options.remoteCaption, "Remote / Fabric"),
            localNodes: Array.from(localNodes.values()).sort((left, right) => traceyLinkMetric(right.currentBps, right.forecastBps, mode) - traceyLinkMetric(left.currentBps, left.forecastBps, mode)),
            remoteNodes: Array.from(remoteNodes.values()).sort((left, right) => traceyLinkMetric(right.currentBps, right.forecastBps, mode) - traceyLinkMetric(left.currentBps, left.forecastBps, mode)),
            links: visibleLinks,
            stats: {
                currentTotal: baselineTotal,
                forecastTotal: Math.max(0, parseNumber(scenario && scenario.estimatedNetworkBps, baselineTotal)),
                activeFlows: Math.max(0, parseNumber(scenario && scenario.estimatedActiveFlows, baselineFlows)),
                estimatedFlows: Math.max(0, parseNumber(scenario && scenario.estimatedActiveFlows, baselineFlows)),
                udpDrops: Math.max(0, Math.round(parseNumber(base.stats && base.stats.udpDrops, 0) * (1 + parseNumber(scenario && scenario.estimatedQueuePressure, 0)))),
                avgConfidence: parseNumber(scenario && scenario.confidence, parseNumber(base.stats && base.stats.avgConfidence, NaN)),
                hiddenLinks: Math.max(0, allLinks.length - visibleLinks.length),
                hiddenLocalNodes: Math.max(0, localMetrics.size - localNodes.size),
                hiddenRemoteNodes: Math.max(0, remoteMetrics.size - remoteNodes.size),
                backend: traceyText(base.stats && base.stats.backend, traceyText(model && model.source, "")),
                scaleMax: scale,
                simulationFocus: traceyText(scenario && scenario.simulationFocus, ""),
                targetNodes: Math.max(1, Math.round(parseNumber(scenario && scenario.targetNodes, baselineNodes))),
                targetGpus: Math.max(1, Math.round(parseNumber(scenario && scenario.targetGpus, baselineGpus))),
                targetCpuCores: Math.max(1, Math.round(parseNumber(scenario && scenario.targetCpuCores, parseNumber(baseline.cpu_core_estimate, 1)))),
                topology: traceyText(scenario && scenario.topology, ""),
                collective: traceyText(scenario && scenario.recommendedCollective, ""),
                estimatedGpuUtilizationPct: parseNumber(scenario && scenario.estimatedGpuUtilizationAvgPct, 0),
                estimatedCpuUsagePct: parseNumber(scenario && scenario.estimatedCpuUsagePct, 0),
                latencyPressure: parseNumber(scenario && scenario.estimatedLatencyPressure, 0),
                queuePressure: parseNumber(scenario && scenario.estimatedQueuePressure, 0),
                confidence: parseNumber(scenario && scenario.confidence, NaN)
            }
        };
    }

    function buildTraceyScopedSimulationTopology(items, model, scenario, options = {}) {
        const baseDataset = buildTraceyScopedTopology(items, "current", options);
        return buildTraceySimulationTopologyFromDataset(baseDataset, model, scenario, {
            ...options,
            scopeKey: traceyText(options.scopeKey, "scope")
        });
    }

    function buildTraceyServerSimulationTopology(data, model, scenario, talkerMode) {
        const baseDataset = buildTraceyServerTopology(data, "current", talkerMode);
        return buildTraceySimulationTopologyFromDataset(baseDataset, model, scenario, {
            scopeKey: "server",
            maxLinks: 18,
            maxNodesPerSide: 7,
            localCaption: "Processes + Cohorts",
            remoteCaption: "Remote / Fabric",
            syntheticLocalKind: "expansion",
            syntheticLocalLabel: (_index, cohortMeta, scenarioMeta) => {
                if (parseNumber(scenarioMeta && scenarioMeta.extraNodes, 0) > 0) {
                    return `Node Cohort +${formatCount(cohortMeta.nodes, "0")}`;
                }
                return `GPU Cohort +${formatCount(cohortMeta.gpus, "0")}`;
            },
            syntheticLocalSubtitle: (_index, cohortMeta, scenarioMeta) => `${formatCount(cohortMeta.gpus, "0")} GPUs • ${traceyText(scenarioMeta && scenarioMeta.topology, "Heuristic")}`
        });
    }

    function buildTraceyScenarioTalkers(items, scenario, talkerMode, options = {}) {
        const talkers = buildTraceyScopedTalkers(items, "current", talkerMode, options);
        const baselineTotal = talkers.reduce((sum, talker) => sum + Math.max(0, parseNumber(talker && talker.currentBps, 0)), 0);
        const baselineFlows = talkers.reduce((sum, talker) => sum + Math.max(0, parseNumber(talker && talker.flowCount, 0)), 0);
        const scale = baselineTotal > 0
            ? clamp(parseNumber(scenario && scenario.estimatedNetworkBps, baselineTotal) / baselineTotal, 0.15, 24)
            : 1;
        const projectedTalkers = talkers.map((talker) => ({
            ...talker,
            forecastBps: Math.max(0, parseNumber(talker && talker.currentBps, 0) * scale)
        }));
        const extraNetworkBps = Math.max(0, parseNumber(scenario && scenario.extraNetworkBps, 0));
        if (extraNetworkBps > 0) {
            projectedTalkers.unshift({
                key: "simulation-expansion",
                label: "Expansion Fabric",
                detail: `${traceyText(scenario && scenario.topology, "Heuristic")} • ${traceyText(scenario && scenario.recommendedCollective, "Ring")}`,
                tone: toneClassFromPressure(Math.max(
                    parseNumber(scenario && scenario.estimatedLatencyPressure, 0),
                    parseNumber(scenario && scenario.estimatedQueuePressure, 0)
                )),
                currentBps: 0,
                forecastBps: extraNetworkBps,
                flowCount: Math.max(1, Math.round(Math.max(1, parseNumber(scenario && scenario.estimatedActiveFlows, 0) - baselineFlows))),
                listenerCount: 0,
                scopeNodes: new Set(),
                remotes: new Set(),
                scopeLabel: `${formatCount(parseNumber(scenario && scenario.targetNodes, 0), "0")} nodes • ${formatCount(parseNumber(scenario && scenario.targetGpus, 0), "0")} GPUs`
            });
        }
        return projectedTalkers
            .sort((left, right) => traceyLinkMetric(right.currentBps, right.forecastBps, "simulation") - traceyLinkMetric(left.currentBps, left.forecastBps, "simulation"));
    }

    function buildTraceyTopologyLegendEntries(dataset, forecastMode, extraEntries = []) {
        const stats = dataset && dataset.stats ? dataset.stats : {};
        const entries = [
            { label: "Current", color: "#38bdf8", value: formatBytesRate(stats.currentTotal) },
            { label: traceyTopologyForecastLabel(forecastMode), color: "#f59e0b", value: formatBytesRate(stats.forecastTotal) },
            { label: "Active Flows", color: "#22c55e", value: formatCount(stats.activeFlows, "0") },
            { label: "Estimated", color: "#06b6d4", value: formatCount(stats.estimatedFlows, "0") },
            { label: "Confidence", color: "#94a3b8", value: formatRatioPercent(stats.avgConfidence, 0, "-") },
            { label: "UDP Drops", color: "#ef4444", value: formatCount(stats.udpDrops, "0") }
        ];
        if (stats.backend) {
            entries.push({ label: "Backend", color: "#94a3b8", value: stats.backend });
        }
        if (stats.hiddenLinks > 0 || stats.hiddenRemoteNodes > 0 || stats.hiddenLocalNodes > 0) {
            entries.push({
                label: "Hidden",
                color: "#94a3b8",
                value: `${formatCount(stats.hiddenLinks, "0")} links • ${formatCount(stats.hiddenRemoteNodes, "0")} remotes`
            });
        }
        return entries.concat(extraEntries).slice(0, 7);
    }

    // The topology stays deterministic and light-weight by laying visible local and remote nodes in two columns.
    function renderTraceyTopologySvg(svg, dataset, emptyMessage = "No network map available.", resolver = {}) {
        if (!svg) {
            return;
        }
        clearSvg(svg);
        const width = 900;
        const height = 280;
        svg.setAttribute("viewBox", `0 0 ${width} ${height}`);
        if (!dataset || !dataset.links.length || !dataset.localNodes.length || !dataset.remoteNodes.length) {
            svg.appendChild(createSvgNode("text", {
                x: width / 2,
                y: height / 2,
                "text-anchor": "middle",
                class: "tracey-chart-empty"
            })).textContent = emptyMessage;
            return;
        }

        const localNodes = dataset.localNodes;
        const remoteNodes = dataset.remoteNodes;
        const visibleCount = Math.max(localNodes.length, remoteNodes.length);
        const boxWidth = 210;
        const boxHeight = Math.max(22, Math.min(48, ((height - 54) / Math.max(visibleCount, 1)) - 8));
        const leftX = 40;
        const rightX = width - boxWidth - 40;
        const topY = 30;
        const contentHeight = height - topY - 18;
        const positionMap = new Map();

        const assignPositions = (nodesToPlace, x) => {
            const step = nodesToPlace.length > 1
                ? (contentHeight - boxHeight) / (nodesToPlace.length - 1)
                : 0;
            nodesToPlace.forEach((node, index) => {
                const y = nodesToPlace.length > 1
                    ? topY + (step * index)
                    : topY + ((contentHeight - boxHeight) / 2);
                positionMap.set(node.id, { x, y });
            });
        };

        assignPositions(localNodes, leftX);
        assignPositions(remoteNodes, rightX);

        svg.appendChild(createSvgNode("text", {
            x: leftX,
            y: 16,
            class: "tracey-topology-caption"
        })).textContent = traceyText(dataset && dataset.localCaption, "Observed");
        svg.appendChild(createSvgNode("text", {
            x: rightX + boxWidth,
            y: 16,
            "text-anchor": "end",
            class: "tracey-topology-caption"
        })).textContent = traceyText(dataset && dataset.remoteCaption, "Remote");

        const maxMetric = Math.max(1, ...dataset.links.map((link) => traceyLinkMetric(link.currentBps, link.forecastBps, dataset.forecastMode)));
        for (const link of dataset.links) {
            const from = positionMap.get(link.localId);
            const to = positionMap.get(link.remoteId);
            if (!from || !to) {
                continue;
            }
            const metric = traceyLinkMetric(link.currentBps, link.forecastBps, dataset.forecastMode);
            const strokeWidth = 1.5 + (metric / maxMetric) * 6;
            const colors = traceyToneColors(link.tone);
            const interaction = typeof resolver.resolveLinkInteraction === "function"
                ? resolver.resolveLinkInteraction(link)
                : null;
            const interactiveClass = interaction ? " is-interactive" : "";
            const activeClass = typeof resolver.isLinkActive === "function" && resolver.isLinkActive(link) ? " is-active" : "";
            const pathAttrs = {
                d: `M${from.x + boxWidth},${from.y + boxHeight / 2} C ${from.x + boxWidth + 90},${from.y + boxHeight / 2} ${to.x - 90},${to.y + boxHeight / 2} ${to.x},${to.y + boxHeight / 2}`,
                class: `tracey-topology-link${interactiveClass}${activeClass}`,
                stroke: colors.stroke,
                "stroke-width": strokeWidth
            };
            if (interaction) {
                pathAttrs["data-tracey-topology-interaction"] = JSON.stringify(interaction);
                pathAttrs.tabindex = 0;
                pathAttrs.role = "button";
                pathAttrs["focusable"] = "true";
                pathAttrs["aria-label"] = traceyText(interaction.ariaLabel, `${link.localLabel} to ${link.remoteLabel}`);
            }
            const path = createSvgNode("path", pathAttrs);
            path.appendChild(createSvgNode("title")).textContent = traceyJoin([
                `${link.localLabel} -> ${link.remoteLabel}`,
                `Now ${formatBytesRate(link.currentBps)}`,
                `${traceyTopologyForecastLabel(dataset.forecastMode)} ${formatBytesRate(link.forecastBps)}`,
                `${formatCount(link.flowCount, "0")} flows`,
                link.localPorts.length ? `Local ${link.localPorts.join(", ")}` : "",
                link.remotePorts.length ? `Remote ${link.remotePorts.join(", ")}` : ""
            ], "\n");
            svg.appendChild(path);
        }

        const drawNode = (node, side) => {
            const position = positionMap.get(node.id);
            if (!position) {
                return;
            }
            const palette = traceyToneColors(node.tone);
            const interaction = typeof resolver.resolveNodeInteraction === "function"
                ? resolver.resolveNodeInteraction(node, side)
                : null;
            const interactiveClass = interaction ? " is-interactive" : "";
            const activeClass = typeof resolver.isNodeActive === "function" && resolver.isNodeActive(node, side) ? " is-active" : "";
            const groupAttrs = {
                class: `tracey-topology-node tracey-topology-node-${side}${interactiveClass}${activeClass}`
            };
            if (interaction) {
                groupAttrs["data-tracey-topology-interaction"] = JSON.stringify(interaction);
                groupAttrs.tabindex = 0;
                groupAttrs.role = "button";
                groupAttrs["focusable"] = "true";
                groupAttrs["aria-label"] = traceyText(interaction.ariaLabel, node.label);
            }
            const group = createSvgNode("g", groupAttrs);
            const rect = createSvgNode("rect", {
                x: position.x,
                y: position.y,
                width: boxWidth,
                height: boxHeight,
                rx: 12,
                fill: palette.fill,
                stroke: palette.stroke,
                "stroke-width": 1.2
            });
            group.appendChild(rect);
            const title = createSvgNode("title");
            title.textContent = traceyJoin([
                node.label,
                node.subtitle,
                `Now ${formatBytesRate(node.currentBps)}`,
                `${traceyTopologyForecastLabel(dataset.forecastMode)} ${formatBytesRate(node.forecastBps)}`,
                `${formatCount(node.flowCount, "0")} flows`
            ], "\n");
            group.appendChild(title);
            const compactNode = boxHeight < 34;
            const labelY = position.y + (compactNode ? (boxHeight / 2) + 4 : 17);
            const label = createSvgNode("text", {
                x: position.x + 12,
                y: labelY,
                class: "tracey-topology-label"
            });
            label.textContent = traceyTrimLabel(node.label, 26);
            group.appendChild(label);
            const metric = createSvgNode("text", {
                x: position.x + boxWidth - 12,
                y: labelY,
                "text-anchor": "end",
                class: "tracey-topology-metric"
            });
            metric.textContent = traceyTrimLabel(formatBytesRate(traceyLinkMetric(node.currentBps, node.forecastBps, dataset.forecastMode)), 16);
            group.appendChild(metric);
            if (!compactNode) {
                const subtitle = createSvgNode("text", {
                    x: position.x + 12,
                    y: position.y + boxHeight - 10,
                    class: "tracey-topology-subtitle"
                });
                subtitle.textContent = traceyTrimLabel(node.subtitle || `${formatCount(node.flowCount, "0")} flows`, 30);
                group.appendChild(subtitle);
            }
            svg.appendChild(group);
        };

        localNodes.forEach((node) => {
            drawNode(node, "local");
        });
        remoteNodes.forEach((node) => {
            drawNode(node, "remote");
        });
    }

    function renderTraceyTalkerTable(tbody, talkers, forecastMode, emptyMessage) {
        const rows = Array.isArray(talkers) ? talkers.slice(0, 12).map((talker) => {
            const forecastLabel = traceyTopologyForecastLabel(forecastMode);
            const scaleRatio = talker.currentBps > 0 ? talker.forecastBps / talker.currentBps : 1;
            return `<tr><td><div class="tracey-cell-stack ${escapeHtml(talker.tone)}"><strong>${escapeHtml(talker.label)}</strong><div class="empty">${escapeHtml(talker.detail || "No detail")}</div></div></td><td>${escapeHtml(talker.scopeLabel)}</td><td><div class="tracey-cell-stack"><strong>${escapeHtml(formatBytesRate(talker.currentBps))}</strong><div class="empty">${escapeHtml(`${formatCount(talker.flowCount, "0")} flows`)}</div></div></td><td><div class="tracey-cell-stack"><strong>${escapeHtml(`${forecastLabel} ${formatBytesRate(talker.forecastBps)}`)}</strong><div class="empty">${escapeHtml(`x${formatFixed(scaleRatio, 2, "1.00")}`)}</div></div></td></tr>`;
        }) : [];
        renderRows(tbody, rows, emptyMessage, 4);
    }

    function renderTraceyTalkerBars(node, talkers, forecastMode, emptyMessage) {
        if (!node) {
            return;
        }
        const visibleTalkers = Array.isArray(talkers) ? talkers.slice(0, 8) : [];
        if (!visibleTalkers.length) {
            node.innerHTML = `<p class="empty">${escapeHtml(emptyMessage)}</p>`;
            return;
        }
        const maxMetric = Math.max(1, ...visibleTalkers.map((talker) => Math.max(talker.currentBps, talker.forecastBps)));
        const forecastLabel = traceyTopologyForecastLabel(forecastMode);
        node.innerHTML = visibleTalkers.map((talker) => {
            const currentWidth = Math.max(4, (talker.currentBps / maxMetric) * 100);
            const forecastWidth = Math.max(4, (talker.forecastBps / maxMetric) * 100);
            return `<div class="tracey-talker-row ${escapeHtml(talker.tone)}"><div class="tracey-talker-copy"><strong>${escapeHtml(talker.label)}</strong><span>${escapeHtml(talker.detail || talker.scopeLabel)}</span></div><div class="tracey-talker-meters"><div class="tracey-talker-meter"><span class="tracey-talker-fill tracey-talker-fill-current" style="width:${escapeHtml(formatFixed(currentWidth, 1, "0"))}%"></span></div><div class="tracey-talker-meter tracey-talker-meter-forecast"><span class="tracey-talker-fill tracey-talker-fill-forecast" style="width:${escapeHtml(formatFixed(forecastWidth, 1, "0"))}%"></span></div></div><div class="tracey-talker-values"><span>Now ${escapeHtml(formatBytesRate(talker.currentBps))}</span><strong>${escapeHtml(`${forecastLabel} ${formatBytesRate(talker.forecastBps)}`)}</strong></div></div>`;
        }).join("");
    }

    function renderTraceyNetworkTimeline(svg, legendNode, rows, seriesDefs, legendEntries) {
        renderLineChart(svg, Array.isArray(rows) ? rows : [], seriesDefs);
        renderLegend(legendNode, legendEntries);
    }

    function traceyTextEquals(left, right) {
        return traceyText(left, "").toLowerCase() === traceyText(right, "").toLowerCase();
    }

    function buildTraceyServerTopologyFocus(spec = {}) {
        const pid = Math.max(0, Math.round(parseNumber(spec.pid, 0)));
        const localPort = Math.max(0, Math.round(parseNumber(spec.localPort, 0)));
        const remotePort = Math.max(0, Math.round(parseNumber(spec.remotePort, 0)));
        const process = traceyText(spec.process, "");
        const protocol = traceyText(spec.protocol, "").toLowerCase();
        const remoteIp = traceyText(spec.remoteIp, "");
        const kind = traceyText(spec.kind, "");
        if (!kind && pid <= 0 && !process && localPort <= 0 && !remoteIp) {
            return null;
        }
        return {
            kind: kind || (remoteIp ? "remote" : (localPort > 0 ? "port" : "process")),
            label: traceyText(spec.label, "Network focus"),
            subtitle: traceyText(spec.subtitle, ""),
            process,
            pid,
            localPort,
            protocol,
            remoteIp,
            remotePort,
            activeLocalId: traceyText(spec.activeLocalId, ""),
            activeRemoteId: traceyText(spec.activeRemoteId, ""),
            activeLinkId: traceyText(spec.activeLinkId, "")
        };
    }

    function traceyServerTopologyFocusKey(focus) {
        if (!focus) {
            return "";
        }
        return [
            traceyText(focus.kind, ""),
            Math.max(0, parseNumber(focus.pid, 0)),
            traceyText(focus.process, "").toLowerCase(),
            Math.max(0, parseNumber(focus.localPort, 0)),
            traceyText(focus.protocol, "").toLowerCase(),
            traceyText(focus.remoteIp, "").toLowerCase(),
            Math.max(0, parseNumber(focus.remotePort, 0)),
            traceyText(focus.activeLocalId, ""),
            traceyText(focus.activeRemoteId, ""),
            traceyText(focus.activeLinkId, "")
        ].join("|");
    }

    function sameTraceyServerTopologyFocus(left, right) {
        return traceyServerTopologyFocusKey(left) === traceyServerTopologyFocusKey(right);
    }

    function renderTraceyServerTopologyFocusBar(focus, summary = null) {
        if (!nodes.traceyServerTopologyFocus) {
            return;
        }
        if (!focus) {
            nodes.traceyServerTopologyFocus.innerHTML = `<span class="empty">Click topology nodes or links to select racks, servers, or filter network tables.</span>`;
            if (nodes.traceyServerTopologyClear) {
                nodes.traceyServerTopologyClear.disabled = true;
            }
            return;
        }
        const summaryParts = summary && typeof summary === "object"
            ? [
                `${formatCount(summary.flowCount, "0")} flows`,
                `${formatCount(summary.listenerCount, "0")} listeners`,
                `${formatCount(summary.processCount, "0")} mappings`
            ]
            : [];
        nodes.traceyServerTopologyFocus.innerHTML = `<strong>${escapeHtml(focus.label)}</strong><span>${escapeHtml(traceyJoin([
            focus.subtitle,
            summaryParts.join(" • ")
        ]))}</span>`;
        if (nodes.traceyServerTopologyClear) {
            nodes.traceyServerTopologyClear.disabled = false;
        }
    }

    function clearTraceyServerTopologyFocus(options = {}) {
        traceyState.serverTopologyFocus = null;
        if (options.rerender === false) {
            renderTraceyServerTopologyFocusBar(null);
            syncTraceyUrlState();
            return;
        }
        if (traceyState.selectedServerTelemetry) {
            renderTraceyServerTelemetry(traceyState.selectedServerTelemetry);
        } else {
            renderTraceyServerTopologyFocusBar(null);
        }
        syncTraceyUrlState();
    }

    function setTraceyServerTopologyFocus(focus, options = {}) {
        const normalized = buildTraceyServerTopologyFocus(focus);
        if (!normalized) {
            clearTraceyServerTopologyFocus(options);
            return;
        }
        const toggle = options.toggle !== false;
        if (toggle && sameTraceyServerTopologyFocus(traceyState.serverTopologyFocus, normalized)) {
            clearTraceyServerTopologyFocus(options);
            return;
        }
        traceyState.serverTopologyFocus = normalized;
        if (options.rerender === false) {
            renderTraceyServerTopologyFocusBar(normalized);
            syncTraceyUrlState();
            return;
        }
        if (traceyState.selectedServerTelemetry) {
            renderTraceyServerTelemetry(traceyState.selectedServerTelemetry);
        } else {
            renderTraceyServerTopologyFocusBar(normalized);
        }
        syncTraceyUrlState();
    }

    function flowMatchesTraceyServerTopologyFocus(flow, focus) {
        if (!focus) {
            return true;
        }
        if (focus.pid > 0 && Math.round(parseNumber(flow && flow.pid, 0)) !== focus.pid) {
            return false;
        }
        if (focus.process && !traceyTextEquals(flow && flow.process, focus.process)) {
            return false;
        }
        if (focus.localPort > 0 && Math.round(parseNumber(flow && flow.local_port, 0)) !== focus.localPort) {
            return false;
        }
        if (focus.protocol && !traceyTextEquals(flow && flow.protocol, focus.protocol)) {
            return false;
        }
        if (focus.remoteIp && !traceyTextEquals(flow && flow.remote_ip, focus.remoteIp)) {
            return false;
        }
        if (focus.remotePort > 0 && Math.round(parseNumber(flow && flow.remote_port, 0)) !== focus.remotePort) {
            return false;
        }
        return true;
    }

    function listenerMatchesTraceyServerTopologyFocus(listener, focus) {
        if (!focus) {
            return true;
        }
        if (focus.remoteIp && focus.pid <= 0 && !focus.process && focus.localPort <= 0) {
            return false;
        }
        if (focus.pid > 0 && Math.round(parseNumber(listener && listener.pid, 0)) !== focus.pid) {
            return false;
        }
        if (focus.process && !traceyTextEquals(listener && listener.process, focus.process)) {
            return false;
        }
        if (focus.localPort > 0 && Math.round(parseNumber(listener && listener.local_port, 0)) !== focus.localPort) {
            return false;
        }
        if (focus.protocol && !traceyTextEquals(listener && listener.protocol, focus.protocol)) {
            return false;
        }
        return true;
    }

    function processMatchesTraceyServerTopologyFocus(process, focus, matchingFlowPids) {
        if (!focus) {
            return true;
        }
        const pid = Math.round(parseNumber(process && process.pid, 0));
        if (focus.pid > 0 && pid !== focus.pid) {
            return false;
        }
        if (focus.process && !traceyTextEquals(process && process.name, focus.process)) {
            return false;
        }
        if (focus.localPort > 0) {
            const localPorts = Array.isArray(process && process.local_ports) ? process.local_ports : [];
            const hasLocalPort = localPorts.some((port) => Math.round(parseNumber(port, 0)) === focus.localPort);
            if (!hasLocalPort) {
                return false;
            }
        }
        if (focus.remoteIp) {
            if (matchingFlowPids && matchingFlowPids.size > 0) {
                return matchingFlowPids.has(pid);
            }
            return traceyTextEquals(process && process.dominant_remote_ip, focus.remoteIp);
        }
        return true;
    }

    function traceyServerTopologyEmptyMessage(focus, subject, fallback) {
        if (!focus) {
            return fallback;
        }
        return `No ${subject} match ${traceyText(focus.label, "the active topology focus")}.`;
    }

    function buildTraceyServerFocusFromLocalNode(node) {
        const localKind = traceyText(node && node.localKind, "server");
        if (localKind === "process") {
            return buildTraceyServerTopologyFocus({
                kind: "process",
                label: `Application • ${traceyText(node && node.label, "unknown")}`,
                subtitle: traceyText(node && node.subtitle, ""),
                pid: parseNumber(node && node.localPid, 0),
                process: traceyText(node && (node.localProcess || node.label), ""),
                activeLocalId: traceyText(node && node.id, "")
            });
        }
        if (localKind === "port") {
            return buildTraceyServerTopologyFocus({
                kind: "port",
                label: `Port • ${traceyText(node && node.label, "port")}`,
                subtitle: traceyText(node && node.subtitle, ""),
                localPort: parseNumber(node && node.localPort, 0),
                protocol: traceyText(node && node.localProtocol, ""),
                process: traceyText(node && node.localProcess, ""),
                activeLocalId: traceyText(node && node.id, "")
            });
        }
        return null;
    }

    function buildTraceyServerFocusFromRemoteNode(node) {
        return buildTraceyServerTopologyFocus({
            kind: "remote",
            label: `Remote • ${traceyText(node && node.label, "endpoint")}`,
            subtitle: traceyText(node && node.subtitle, ""),
            remoteIp: traceyText(node && node.remoteIp, traceyText(node && node.label, "")),
            remotePort: parseNumber(node && node.remotePort, 0),
            activeRemoteId: traceyText(node && node.id, "")
        });
    }

    function buildTraceyServerFocusFromLink(link) {
        return buildTraceyServerTopologyFocus({
            kind: "link",
            label: `Path • ${traceyText(link && link.localLabel, "local")} -> ${traceyText(link && link.remoteLabel, "remote")}`,
            subtitle: traceyJoin([
                `${formatCount(link && link.flowCount, "0")} flows`,
                `now ${formatBytesRate(link && link.currentBps)}`
            ]),
            pid: traceyText(link && link.localKind, "") === "process" ? parseNumber(link && link.localPid, 0) : 0,
            process: traceyText(link && link.localKind, "") === "process"
                ? traceyText(link && (link.localProcess || link.localLabel), "")
                : "",
            localPort: parseNumber(link && link.localPort, 0),
            protocol: traceyText(link && link.localProtocol, ""),
            remoteIp: traceyText(link && link.remoteIp, ""),
            remotePort: parseNumber(link && link.remotePort, 0),
            activeLocalId: traceyText(link && link.localId, ""),
            activeRemoteId: traceyText(link && link.remoteId, ""),
            activeLinkId: traceyText(link && link.id, "")
        });
    }

    function traceyServerTopologyNodeIsActive(node, side) {
        const focus = traceyState.serverTopologyFocus;
        if (!focus) {
            return false;
        }
        if (side === "remote") {
            return focus.activeRemoteId && focus.activeRemoteId === traceyText(node && node.id, "");
        }
        return focus.activeLocalId && focus.activeLocalId === traceyText(node && node.id, "");
    }

    function traceyServerTopologyLinkIsActive(link) {
        const focus = traceyState.serverTopologyFocus;
        return Boolean(focus && focus.activeLinkId && focus.activeLinkId === traceyText(link && link.id, ""));
    }

    function parseTraceyTopologyInteractionEventTarget(target) {
        if (!target || typeof target.closest !== "function") {
            return null;
        }
        const trigger = target.closest("[data-tracey-topology-interaction]");
        if (!trigger) {
            return null;
        }
        try {
            const raw = String(trigger.getAttribute("data-tracey-topology-interaction") || "").trim();
            return raw ? JSON.parse(raw) : null;
        } catch (_error) {
            return null;
        }
    }

    function findBestTraceyFlowMatch(items, focus, predicate = null) {
        let best = null;
        for (const item of Array.isArray(items) ? items : []) {
            if (predicate && !predicate(item)) {
                continue;
            }
            const flows = Array.isArray(extractTraceyNetworkPayload(item).top_flows)
                ? extractTraceyNetworkPayload(item).top_flows
                : [];
            for (const flow of flows) {
                if (!flowMatchesTraceyServerTopologyFocus(flow, focus)) {
                    continue;
                }
                const score = traceyFlowCurrentBps(flow);
                if (!best || score > best.score) {
                    best = { item, flow, score };
                }
            }
        }
        return best;
    }

    async function handleTraceyTopologyInteraction(interaction) {
        if (!interaction || typeof interaction !== "object") {
            return;
        }
        const scope = traceyText(interaction.scope, "");
        const type = traceyText(interaction.type, "");
        if (scope === "fleet") {
            if (type === "rack" && interaction.rackId) {
                clearTraceyServerTopologyFocus({ rerender: false });
                await fetchTraceyRackDetail(interaction.rackId);
                return;
            }
            const remoteFocus = buildTraceyServerTopologyFocus({
                kind: "remote",
                label: `Remote • ${traceyText(interaction.remoteLabel, "endpoint")}`,
                subtitle: traceyText(interaction.remoteSubtitle, ""),
                remoteIp: traceyText(interaction.remoteIp, ""),
                remotePort: parseNumber(interaction.remotePort, 0)
            });
            const bestMatch = findBestTraceyFlowMatch(
                traceyState.fleet && Array.isArray(traceyState.fleet.agents) ? traceyState.fleet.agents : [],
                remoteFocus,
                (item) => {
                    if (type === "link" && interaction.rackId) {
                        return traceyTextEquals(item && item.rack, interaction.rackId);
                    }
                    return true;
                }
            );
            if (bestMatch && bestMatch.item && bestMatch.item.agent_id) {
                await selectTraceyAgent(bestMatch.item.agent_id);
                setTraceyServerTopologyFocus(remoteFocus);
            }
            return;
        }
        if (scope === "rack") {
            if (type === "server" && interaction.agentId) {
                clearTraceyServerTopologyFocus({ rerender: false });
                await selectTraceyAgent(interaction.agentId);
                return;
            }
            const remoteFocus = buildTraceyServerTopologyFocus({
                kind: "remote",
                label: `Remote • ${traceyText(interaction.remoteLabel, "endpoint")}`,
                subtitle: traceyText(interaction.remoteSubtitle, ""),
                remoteIp: traceyText(interaction.remoteIp, ""),
                remotePort: parseNumber(interaction.remotePort, 0)
            });
            const servers = traceyState.selectedRackDetail && Array.isArray(traceyState.selectedRackDetail.servers)
                ? traceyState.selectedRackDetail.servers
                : [];
            const bestMatch = findBestTraceyFlowMatch(
                servers,
                remoteFocus,
                (item) => {
                    if (type === "link" && interaction.agentId) {
                        return traceyTextEquals(item && item.agent_id, interaction.agentId);
                    }
                    return true;
                }
            );
            const targetAgentId = bestMatch && bestMatch.item
                ? traceyText(bestMatch.item.agent_id, "")
                : traceyText(interaction.agentId, "");
            if (targetAgentId) {
                await selectTraceyAgent(targetAgentId);
                setTraceyServerTopologyFocus(remoteFocus);
            }
            return;
        }
        if (scope === "server") {
            if (type === "clear") {
                clearTraceyServerTopologyFocus();
                return;
            }
            if (type === "local_node") {
                setTraceyServerTopologyFocus(buildTraceyServerFocusFromLocalNode(interaction.node || {}));
                return;
            }
            if (type === "remote_node") {
                setTraceyServerTopologyFocus(buildTraceyServerFocusFromRemoteNode(interaction.node || {}));
                return;
            }
            if (type === "link") {
                setTraceyServerTopologyFocus(buildTraceyServerFocusFromLink(interaction.link || {}));
            }
        }
    }

    // Delegate events from the SVG root so rerendered nodes and links remain interactive.
    function bindTraceyTopologyInteractions(node) {
        if (!node) {
            return;
        }
        const invokeInteraction = (event) => {
            const interaction = parseTraceyTopologyInteractionEventTarget(event.target);
            if (!interaction) {
                return;
            }
            event.preventDefault();
            void handleTraceyTopologyInteraction(interaction);
        };
        node.addEventListener("click", invokeInteraction);
        node.addEventListener("keydown", (event) => {
            if (event.key !== "Enter" && event.key !== " " && event.key !== "Spacebar") {
                return;
            }
            invokeInteraction(event);
        });
    }

    function currentTraceySimulationModel(scope) {
        const normalized = String(scope || "").trim().toLowerCase();
        if (normalized === "fleet") {
            return buildTraceyAggregateExpansionModel(
                traceyState.fleet && Array.isArray(traceyState.fleet.agents) ? traceyState.fleet.agents : [],
                normalized
            );
        }
        if (normalized === "rack") {
            return buildTraceyAggregateExpansionModel(
                traceyState.selectedRackDetail && Array.isArray(traceyState.selectedRackDetail.servers) ? traceyState.selectedRackDetail.servers : [],
                normalized
            );
        }
        if (normalized === "server") {
            return traceyState.selectedServerTelemetry
                ? extractTraceyExpansionModel(traceyState.selectedServerTelemetry, normalized)
                : buildTraceyFallbackExpansionModel({}, normalized, { nodeCount: 1, gpuCount: 1, cpuCoreEstimate: 4 });
        }
        return buildTraceyFallbackExpansionModel({}, normalized, { nodeCount: 1, gpuCount: 1, cpuCoreEstimate: 4 });
    }

    function bindTraceySimulationControls(scope) {
        const config = traceySimulationScopeConfig(scope);
        if (!config) {
            return;
        }
        if (config.actualTab) {
            config.actualTab.addEventListener("click", () => {
                setTraceyTopologyTab(scope, "actual");
            });
        }
        if (config.expansionTab) {
            config.expansionTab.addEventListener("click", () => {
                setTraceyTopologyTab(scope, "expansion");
            });
        }
        if (config.recommendedButton) {
            config.recommendedButton.addEventListener("click", () => {
                const model = currentTraceySimulationModel(scope);
                applyTraceyRecommendedTargets(scope, model);
                setTraceyTopologyTab(scope, "expansion", { rerender: false });
                rerenderTraceySimulationScope(scope);
            });
        }
        ["nodes", "gpus", "cores"].forEach((key) => {
            const input = config.inputs && config.inputs[key];
            if (!input) {
                return;
            }
            input.addEventListener("input", () => {
                input.dataset.userTouched = "true";
                rerenderTraceySimulationScope(scope);
            });
        });
        if (config.inputs && config.inputs.strategy) {
            config.inputs.strategy.addEventListener("change", () => {
                config.inputs.strategy.dataset.userTouched = "true";
                rerenderTraceySimulationScope(scope);
            });
        }
        setTraceyTopologyTab(scope, "actual", { rerender: false });
    }

    function renderTraceyFleetNetworkSection(data) {
        const items = Array.isArray(data && data.agents) ? data.agents : [];
        const scope = "fleet";
        const simulationModel = buildTraceyAggregateExpansionModel(items, scope);
        syncTraceySimulationInputBounds(scope, simulationModel);
        const talkerMode = readTraceyTalkerMode(nodes.traceyFleetTalkerMode);
        const topologyOptions = {
            scopeKey: "fleet",
            localKey: (item) => traceyText(item && item.rack, "unassigned"),
            localLabel: (item) => traceyText(item && item.rack, "unassigned"),
            localSubtitle: (item) => traceyText(item && item.zone, "unassigned"),
            localTone: (item) => traceyItemTone(item),
            maxLinks: 18,
            maxNodesPerSide: 6,
            syntheticLocalKind: "rack_cohort",
            syntheticLocalLabel: (_index, cohortMeta) => `Rack Cohort +${formatCount(cohortMeta.nodes, "0")}`,
            syntheticLocalSubtitle: (_index, cohortMeta, scenario) => `${formatCount(cohortMeta.gpus, "0")} GPUs • ${traceyText(scenario && scenario.topology, "Heuristic")}`,
            localCaption: "Racks + Expansion",
            remoteCaption: "Remote / Fabric"
        };
        const topologyResolver = {
            resolveNodeInteraction: (node, side) => {
                if (String(node && node.id || "").startsWith("synthetic-")) {
                    return null;
                }
                if (side === "local") {
                    return {
                        scope: "fleet",
                        type: "rack",
                        rackId: traceyText(node && node.id, ""),
                        ariaLabel: `Open rack ${traceyText(node && node.label, "rack")}`
                    };
                }
                return {
                    scope: "fleet",
                    type: "remote",
                    remoteIp: traceyText(node && node.remoteIp, ""),
                    remotePort: parseNumber(node && node.remotePort, 0),
                    remoteLabel: traceyText(node && node.label, ""),
                    remoteSubtitle: traceyText(node && node.subtitle, ""),
                    ariaLabel: `Select busiest server for remote ${traceyText(node && node.label, "endpoint")}`
                };
            },
            resolveLinkInteraction: (link) => String(link && link.id || "").includes(":simulation")
                ? null
                : ({
                    scope: "fleet",
                    type: "link",
                    rackId: traceyText(link && link.localId, ""),
                    remoteIp: traceyText(link && link.remoteIp, ""),
                    remotePort: parseNumber(link && link.remotePort, 0),
                    remoteLabel: traceyText(link && link.remoteLabel, ""),
                    remoteSubtitle: traceyText(link && link.remoteSubtitle, ""),
                    ariaLabel: `Open rack ${traceyText(link && link.localLabel, "rack")} and filter remote ${traceyText(link && link.remoteLabel, "endpoint")}`
                })
        };

        if (readTraceyTopologyTab(scope) === "expansion") {
            const scenario = traceyEstimateExpansionScenario(
                simulationModel,
                readTraceySimulationTargets(scope, simulationModel),
                readTraceySimulationStrategy(scope)
            );
            const dataset = buildTraceyScopedSimulationTopology(items, simulationModel, scenario, topologyOptions);
            renderTraceyTopologySvg(nodes.traceyFleetTopologyChart, dataset, "No fleet network map available.", topologyResolver);
            renderLegend(nodes.traceyFleetTopologyLegend, buildTraceyTopologyLegendEntries(dataset, "simulation", [
                { label: "Targets", color: "#06b6d4", value: `${formatCount(scenario.targetNodes, "0")}n / ${formatCount(scenario.targetGpus, "0")}g` },
                { label: "Topology", color: "#94a3b8", value: traceyTrimLabel(scenario.topology, 18) },
                { label: "Collective", color: "#f97316", value: traceyTrimLabel(scenario.recommendedCollective, 16) }
            ]));
            renderMetricList(
                nodes.traceyFleetSimulationFacts,
                buildTraceySimulationFactRows(simulationModel, scenario),
                "No fleet expansion model available."
            );
            renderTraceyTalkerTable(
                nodes.traceyFleetTalkerRows,
                buildTraceyScenarioTalkers(items, scenario, talkerMode, {
                    scopeLabel: (item) => traceyText(item && (item.host || item.agent_id), "agent")
                }),
                "simulation",
                "No fleet talkers visible in the simulated expansion window."
            );
            return;
        }

        const forecastMode = readTraceyTopologyForecast(nodes.traceyFleetTopologyForecast);
        const dataset = buildTraceyScopedTopology(items, forecastMode, topologyOptions);
        renderTraceyTopologySvg(nodes.traceyFleetTopologyChart, dataset, "No fleet network map available.", topologyResolver);
        renderLegend(nodes.traceyFleetTopologyLegend, buildTraceyTopologyLegendEntries(dataset, forecastMode));
        renderMetricList(nodes.traceyFleetSimulationFacts, buildTraceyActualNetworkFactRows(simulationModel), "No fleet network telemetry available.");
        renderTraceyTalkerTable(
            nodes.traceyFleetTalkerRows,
            buildTraceyScopedTalkers(items, forecastMode, talkerMode, {
                scopeLabel: (item) => traceyText(item && (item.host || item.agent_id), "agent")
            }),
            forecastMode,
            "No fleet talkers visible in the current telemetry window."
        );
    }

    function renderTraceyRackNetworkSection(detail) {
        const items = Array.isArray(detail && detail.servers) ? detail.servers : [];
        const scope = "rack";
        const simulationModel = buildTraceyAggregateExpansionModel(items, scope);
        syncTraceySimulationInputBounds(scope, simulationModel);
        const talkerMode = readTraceyTalkerMode(nodes.traceyRackTalkerMode);
        const topologyOptions = {
            scopeKey: "rack",
            localKey: (item) => traceyText(item && (item.agent_id || item.host), "server"),
            localLabel: (item) => traceyText(item && (item.host || item.agent_id), "server"),
            localSubtitle: (item) => traceyJoin([
                traceyText(item && item.status, ""),
                traceyText(item && item.agent_id, "")
            ]),
            localTone: (item) => traceyItemTone(item),
            maxLinks: 20,
            maxNodesPerSide: 7,
            syntheticLocalKind: "node_cohort",
            syntheticLocalLabel: (_index, cohortMeta) => `Node Cohort +${formatCount(cohortMeta.nodes, "0")}`,
            syntheticLocalSubtitle: (_index, cohortMeta, scenario) => `${formatCount(cohortMeta.gpus, "0")} GPUs • ${traceyText(scenario && scenario.topology, "Heuristic")}`,
            localCaption: "Servers + Expansion",
            remoteCaption: "Remote / Fabric"
        };
        const topologyResolver = {
            resolveNodeInteraction: (node, side) => {
                if (String(node && node.id || "").startsWith("synthetic-")) {
                    return null;
                }
                if (side === "local") {
                    return {
                        scope: "rack",
                        type: "server",
                        agentId: traceyText(node && node.id, ""),
                        ariaLabel: `Open server ${traceyText(node && node.label, "server")}`
                    };
                }
                return {
                    scope: "rack",
                    type: "remote",
                    remoteIp: traceyText(node && node.remoteIp, ""),
                    remotePort: parseNumber(node && node.remotePort, 0),
                    remoteLabel: traceyText(node && node.label, ""),
                    remoteSubtitle: traceyText(node && node.subtitle, ""),
                    ariaLabel: `Select busiest server for remote ${traceyText(node && node.label, "endpoint")}`
                };
            },
            resolveLinkInteraction: (link) => String(link && link.id || "").includes(":simulation")
                ? null
                : ({
                    scope: "rack",
                    type: "link",
                    agentId: traceyText(link && link.localId, ""),
                    remoteIp: traceyText(link && link.remoteIp, ""),
                    remotePort: parseNumber(link && link.remotePort, 0),
                    remoteLabel: traceyText(link && link.remoteLabel, ""),
                    remoteSubtitle: traceyText(link && link.remoteSubtitle, ""),
                    ariaLabel: `Open server ${traceyText(link && link.localLabel, "server")} and filter remote ${traceyText(link && link.remoteLabel, "endpoint")}`
                })
        };

        if (readTraceyTopologyTab(scope) === "expansion") {
            const scenario = traceyEstimateExpansionScenario(
                simulationModel,
                readTraceySimulationTargets(scope, simulationModel),
                readTraceySimulationStrategy(scope)
            );
            const dataset = buildTraceyScopedSimulationTopology(items, simulationModel, scenario, topologyOptions);
            renderTraceyTopologySvg(nodes.traceyRackTopologyChart, dataset, "Select a rack to load its network map.", topologyResolver);
            renderLegend(nodes.traceyRackTopologyLegend, buildTraceyTopologyLegendEntries(dataset, "simulation", [
                { label: "Targets", color: "#06b6d4", value: `${formatCount(scenario.targetNodes, "0")}n / ${formatCount(scenario.targetGpus, "0")}g` },
                { label: "Topology", color: "#94a3b8", value: traceyTrimLabel(scenario.topology, 18) },
                { label: "Collective", color: "#f97316", value: traceyTrimLabel(scenario.recommendedCollective, 16) }
            ]));
            renderMetricList(
                nodes.traceyRackSimulationFacts,
                buildTraceySimulationFactRows(simulationModel, scenario),
                "No rack expansion model available."
            );
            renderTraceyTalkerTable(
                nodes.traceyRackTalkerRows,
                buildTraceyScenarioTalkers(items, scenario, talkerMode, {
                    scopeLabel: (item) => traceyText(item && (item.host || item.agent_id), "server")
                }),
                "simulation",
                "No rack talkers visible in the simulated expansion window."
            );
            return;
        }

        const forecastMode = readTraceyTopologyForecast(nodes.traceyRackTopologyForecast);
        const dataset = buildTraceyScopedTopology(items, forecastMode, topologyOptions);
        renderTraceyTopologySvg(nodes.traceyRackTopologyChart, dataset, "Select a rack to load its network map.", topologyResolver);
        renderLegend(nodes.traceyRackTopologyLegend, buildTraceyTopologyLegendEntries(dataset, forecastMode));
        renderMetricList(nodes.traceyRackSimulationFacts, buildTraceyActualNetworkFactRows(simulationModel), "No rack network telemetry available.");
        renderTraceyTalkerTable(
            nodes.traceyRackTalkerRows,
            buildTraceyScopedTalkers(items, forecastMode, talkerMode, {
                scopeLabel: (item) => traceyText(item && (item.host || item.agent_id), "server")
            }),
            forecastMode,
            "No rack talkers visible in the current telemetry window."
        );
    }

    function renderTraceyServerNetworkSection(data) {
        const scope = "server";
        const simulationModel = extractTraceyExpansionModel(data, scope);
        syncTraceySimulationInputBounds(scope, simulationModel);
        const talkerMode = readTraceyTalkerMode(nodes.traceyServerTalkerMode);
        const resolver = {
            resolveNodeInteraction: (node, side) => {
                if (String(node && node.id || "").startsWith("synthetic-")) {
                    return null;
                }
                if (side === "remote") {
                    return {
                        scope: "server",
                        type: "remote_node",
                        node,
                        ariaLabel: `Filter server tables by remote ${traceyText(node && node.label, "endpoint")}`
                    };
                }
                return {
                    scope: "server",
                    type: buildTraceyServerFocusFromLocalNode(node) ? "local_node" : "clear",
                    node,
                    ariaLabel: buildTraceyServerFocusFromLocalNode(node)
                        ? `Filter server tables by ${traceyText(node && node.label, "local node")}`
                        : "Clear server network focus"
                };
            },
            resolveLinkInteraction: (link) => String(link && link.id || "").includes(":simulation")
                ? null
                : ({
                    scope: "server",
                    type: "link",
                    link,
                    ariaLabel: `Filter server tables by path ${traceyText(link && link.localLabel, "local")} to ${traceyText(link && link.remoteLabel, "remote")}`
                }),
            isNodeActive: (node, side) => traceyServerTopologyNodeIsActive(node, side),
            isLinkActive: (link) => traceyServerTopologyLinkIsActive(link)
        };

        if (readTraceyTopologyTab(scope) === "expansion") {
            const scenario = traceyEstimateExpansionScenario(
                simulationModel,
                readTraceySimulationTargets(scope, simulationModel),
                readTraceySimulationStrategy(scope)
            );
            const dataset = buildTraceyServerSimulationTopology(data, simulationModel, scenario, talkerMode);
            renderTraceyTopologySvg(nodes.traceyServerTopologyChart, dataset, "No server topology is available for the selected talker mode.", resolver);
            renderLegend(nodes.traceyServerTopologyLegend, buildTraceyTopologyLegendEntries(dataset, "simulation", [
                { label: "Targets", color: "#06b6d4", value: `${formatCount(scenario.targetNodes, "0")}n / ${formatCount(scenario.targetGpus, "0")}g` },
                { label: "Topology", color: "#94a3b8", value: traceyTrimLabel(scenario.topology, 18) },
                { label: "Collective", color: "#f97316", value: traceyTrimLabel(scenario.recommendedCollective, 16) }
            ]));
            renderMetricList(
                nodes.traceyServerSimulationFacts,
                buildTraceySimulationFactRows(simulationModel, scenario),
                "No server expansion model available."
            );
            renderTraceyTalkerBars(
                nodes.traceyServerTalkerBars,
                buildTraceyScenarioTalkers([data], scenario, talkerMode, {
                    scopeLabel: () => traceyText(data && (data.host || data.agent_id), "server")
                }),
                "simulation",
                "No server talkers available for the simulated expansion window."
            );
            return;
        }

        const forecastMode = readTraceyTopologyForecast(nodes.traceyServerTopologyForecast);
        const dataset = buildTraceyServerTopology(data, forecastMode, talkerMode);
        const extraLegendEntries = [];
        if (dataset && dataset.stats && dataset.stats.simulationFocus) {
            extraLegendEntries.push({
                label: "Sim Focus",
                color: "#06b6d4",
                value: traceyTrimLabel(dataset.stats.simulationFocus, 18)
            });
        }
        renderTraceyTopologySvg(nodes.traceyServerTopologyChart, dataset, "No server topology is available for the selected talker mode.", resolver);
        renderLegend(nodes.traceyServerTopologyLegend, buildTraceyTopologyLegendEntries(dataset, forecastMode, extraLegendEntries));
        renderMetricList(nodes.traceyServerSimulationFacts, buildTraceyActualNetworkFactRows(simulationModel), "No server network telemetry available.");
        renderTraceyTalkerBars(
            nodes.traceyServerTalkerBars,
            buildTraceyScopedTalkers([data], forecastMode, talkerMode, {
                scopeLabel: () => traceyText(data && (data.host || data.agent_id), "server")
            }),
            forecastMode,
            "No server talkers available for the current filters."
        );
    }

    function renderTraceyAnalyticsOverview(data) {
        if (!data) {
            renderRows(nodes.traceyAnalyticsAgentRows, [], "No Tracey analytics data available.", 6);
            renderLineChart(nodes.traceyStatusChart, [], []);
            renderLineChart(nodes.traceyNetworkChart, [], []);
            renderLineChart(nodes.traceyLogChart, [], []);
            renderLegend(nodes.traceyStatusLegend, []);
            renderLegend(nodes.traceyNetworkLegend, []);
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

        const latestNetwork = timeline.length ? timeline[timeline.length - 1] : {};
        renderTraceyNetworkTimeline(nodes.traceyNetworkChart, nodes.traceyNetworkLegend, timeline, [
            { key: "network_attributed_total_bps", color: "#38bdf8" },
            { key: "network_cross_network_bps", color: "#22c55e" },
            { key: "network_projected_total_bps_15m", color: "#f59e0b" },
            { key: "network_projected_cross_network_bps_15m", color: "#ef4444" }
        ], [
            { label: "Attributed", color: "#38bdf8", value: formatBytesRate(latestNetwork.network_attributed_total_bps) },
            { label: "Cross", color: "#22c55e", value: formatBytesRate(latestNetwork.network_cross_network_bps) },
            { label: "15m", color: "#f59e0b", value: formatBytesRate(latestNetwork.network_projected_total_bps_15m) },
            { label: "Flows", color: "#06b6d4", value: formatCount(latestNetwork.network_active_flows, "0") },
            { label: "Confidence", color: "#94a3b8", value: formatRatioPercent(latestNetwork.avg_network_attribution_confidence, 0, "-") },
            { label: "UDP Drops", color: "#ef4444", value: formatCount(latestNetwork.network_udp_drop_delta, "0") }
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
        const probe_watch = current.probe_watch && typeof current.probe_watch === "object" ? current.probe_watch : {};
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
            { key: "Probe Alerts", value: String(parseNumber(probe_watch.total_alerts, 0)) },
            { key: "Probe High Alerts", value: String(parseNumber(probe_watch.high_severity_alerts, 0)) },
            { key: "Probe Surfaces", value: String(parseNumber(probe_watch.alerted_surface_count, 0)) },
            { key: "Probe Latest", value: `${String(probe_watch.latest_surface || "-")} • ${String(probe_watch.latest_classification || "-")}` },
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
            renderLineChart(nodes.traceyAgentNetworkChart, [], []);
            renderLegend(nodes.traceyAgentStatusLegend, []);
            renderLegend(nodes.traceyAgentNetworkLegend, []);
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
        renderTraceyNetworkTimeline(nodes.traceyAgentNetworkChart, nodes.traceyAgentNetworkLegend, series, [
            { key: "avg_network_attributed_total_bps", color: "#38bdf8" },
            { key: "avg_network_cross_network_bps", color: "#22c55e" },
            { key: "avg_projected_total_bps_15m", color: "#f59e0b" },
            { key: "avg_projected_cross_network_bps_15m", color: "#ef4444" }
        ], [
            { label: "Attributed", color: "#38bdf8", value: formatBytesRate(latest.avg_network_attributed_total_bps) },
            { label: "Cross", color: "#22c55e", value: formatBytesRate(latest.avg_network_cross_network_bps) },
            { label: "15m", color: "#f59e0b", value: formatBytesRate(latest.avg_projected_total_bps_15m) },
            { label: "Flows", color: "#06b6d4", value: formatCount(latest.avg_network_active_flows, "0") },
            { label: "Confidence", color: "#94a3b8", value: formatRatioPercent(latest.avg_network_attribution_confidence, 0, "-") },
            { label: "UDP Drops", color: "#ef4444", value: formatCount(latest.avg_network_udp_drop_delta, "0") }
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
                    label: "Attributed Traffic",
                    value: formatBytesRate(summary.attributed_total_bps),
                    meta: `cross ${formatBytesRate(summary.cross_network_bps)} • ${formatCount(summary.network_active_flows, "0")} flows`,
                    tone: parseNumber(summary.cross_network_bps, 0) > 0 ? "tracey-tone-neutral" : "tracey-tone-ok"
                },
                {
                    label: "15m Projection",
                    value: formatBytesRate(summary.projected_total_bps_15m),
                    meta: `${formatCount(summary.forecast_agents, "0")} forecast agents • ${formatSignedPercentValue(summary.network_traffic_growth_pct_per_min_max, 0)} / min`,
                    tone: parseNumber(summary.projected_total_bps_15m, 0) > parseNumber(summary.attributed_total_bps, 0)
                        ? "tracey-tone-warn"
                        : "tracey-tone-neutral"
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
                    label: "Security Probes",
                    value: `${formatCount(summary.probe_alerts, "0")} / ${formatCount(summary.probe_high_alerts, "0")}`,
                    meta: `${formatCount(summary.probe_alerted_surfaces, "0")} alerted surfaces`,
                    tone: parseNumber(summary.probe_high_alerts, 0) > 0
                        ? "tracey-tone-warn"
                        : (parseNumber(summary.probe_alerts, 0) > 0 ? "tracey-tone-neutral" : "tracey-tone-ok")
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
        renderTraceyFleetNetworkSection(fleetData);
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
            renderTraceyRackNetworkSection(null);
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
        renderTraceyRackNetworkSection(detail);
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
            renderMetricList(nodes.traceyServerNetworkFacts, [], "No attributed network telemetry loaded.");
            renderMetricList(nodes.traceyServerForecastFacts, [], "No growth forecast loaded.");
            renderMetricList(nodes.traceyServerForecastAdvice, [], "No AI advisory available.");
            renderMetricList(nodes.traceyServerGuardFacts, [], "No TraceyGuard summary loaded.");
            renderTraceyServerTopologyFocusBar(null);
            renderRows(nodes.traceyServerFlowRows, [], "No attributed flows loaded.", 5);
            renderRows(nodes.traceyServerListenerRows, [], "No listeners loaded.", 5);
            renderRows(nodes.traceyServerPortRows, [], "No process-port map loaded.", 5);
            renderMetricList(nodes.traceyServerAssessmentFacts, [], "No assessment snapshot loaded.");
            renderTraceyServerNetworkSection(null);
            if (nodes.traceyServerGpuTiles) {
                nodes.traceyServerGpuTiles.innerHTML = `<p class="empty">No GPU inventory available.</p>`;
            }
            return;
        }

        const server = data.server && typeof data.server === "object" ? data.server : {};
        const ecc = server.ecc && typeof server.ecc === "object" ? server.ecc : {};
        const network = server.network && typeof server.network === "object" ? server.network : {};
        const networkSummary = network.summary && typeof network.summary === "object" ? network.summary : {};
        const resourceForecast = data.resource_forecast && typeof data.resource_forecast === "object"
            ? data.resource_forecast
            : {};
        const forecastSimulation = resourceForecast.simulation && typeof resourceForecast.simulation === "object"
            ? resourceForecast.simulation
            : {};
        const forecastAdvice = resourceForecast.ai_advice && typeof resourceForecast.ai_advice === "object"
            ? resourceForecast.ai_advice
            : {};
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
            {
                label: "Attributed Traffic",
                value: formatBytesRate(networkSummary.attributed_total_bps),
                meta: `cross ${formatBytesRate(networkSummary.cross_network_bps)} • ${formatCount(networkSummary.active_flows, "0")} flows`,
                tone: parseNumber(networkSummary.cross_network_flows, 0) > 0 ? "tracey-tone-neutral" : "tracey-tone-ok"
            },
            {
                label: "Network Pressure",
                value: formatRatioPercent(networkSummary.latency_pressure, 0, "-"),
                meta: `queue ${formatRatioPercent(networkSummary.queue_pressure, 0, "-")} • ${formatCount(networkSummary.listeners, "0")} listeners`,
                tone: toneClassFromPressure(Math.max(
                    parseNumber(networkSummary.latency_pressure, 0),
                    parseNumber(networkSummary.queue_pressure, 0)
                ))
            },
            { label: "GPU Util", value: formatPercentValue(server.gpu_utilization_avg_pct, 0), meta: `${formatCount(gpus.length, "0")} GPUs`, tone: "tracey-tone-ok" },
            { label: "Max Temp", value: formatTemperature(server.gpu_temperature_max_c), meta: formatPower(server.gpu_power_total_w), tone: parseNumber(server.gpu_temperature_max_c, 0) >= 80 ? "tracey-tone-warn" : "tracey-tone-neutral" },
            { label: "ECC", value: `${formatCount(ecc.corrected_total, "0")} / ${formatCount(ecc.uncorrected_total, "0")}`, meta: "corr / uncorr", tone: parseNumber(ecc.uncorrected_total, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-neutral" },
            { label: "Autonomy", value: formatRatioPercent(server.autonomy_risk, 0), meta: server.autonomy_action ? formatActionLabel(server.autonomy_action) : "No action", tone: parseNumber(server.autonomy_risk, 0) >= 0.5 ? "tracey-tone-warn" : "tracey-tone-ok" },
            { label: "Guard", value: formatCount(guard.quarantined_devices, "0"), meta: `${formatCount(guard.total_failures, "0")} fail / ${formatCount(guard.total_errors, "0")} err`, tone: parseNumber(guard.quarantined_devices, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-neutral" }
        ];
        if (Object.keys(resourceForecast).length) {
            summaryCards.push(
                {
                    label: "Forecast 15m",
                    value: formatBytesRate(resourceForecast.projected_total_bps_15m),
                    meta: `${formatCount(resourceForecast.projected_active_flows_15m, "0")} flows • ${String(resourceForecast.status || "disabled")}`,
                    tone: parseNumber(resourceForecast.projected_total_bps_15m, 0) > parseNumber(networkSummary.attributed_total_bps, 0)
                        ? "tracey-tone-warn"
                        : "tracey-tone-neutral"
                },
                {
                    label: "Simulation Fit",
                    value: `${formatFixed(forecastSimulation.estimated_cpu_cores, 1, "-")} cores`,
                    meta: `${formatBytes(forecastSimulation.estimated_memory_working_set_bytes)} • ${formatPower(forecastSimulation.estimated_power_w)}`,
                    tone: "tracey-tone-neutral"
                }
            );
        }
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

        renderMetricList(nodes.traceyServerNetworkFacts, [
            {
                label: "Attributed Total",
                value: formatBytesRate(networkSummary.attributed_total_bps),
                subtitle: `rx ${formatBytesRate(networkSummary.attributed_rx_bps)} • tx ${formatBytesRate(networkSummary.attributed_tx_bps)}`,
                tone: "tracey-tone-neutral"
            },
            {
                label: "Cross-Network",
                value: formatBytesRate(networkSummary.cross_network_bps),
                subtitle: `${formatCount(networkSummary.cross_network_flows, "0")} cross • ${formatCount(networkSummary.lan_flows, "0")} lan • ${formatCount(networkSummary.local_host_flows, "0")} local`,
                tone: parseNumber(networkSummary.cross_network_flows, 0) > 0 ? "tracey-tone-neutral" : "tracey-tone-ok"
            },
            {
                label: "Flow State",
                value: `${formatCount(networkSummary.active_flows, "0")} active / ${formatCount(networkSummary.established_flows, "0")} est`,
                subtitle: `${formatCount(networkSummary.listeners, "0")} listeners • ${formatCount(networkSummary.remote_endpoints, "0")} remotes`,
                tone: "tracey-tone-neutral"
            },
            {
                label: "Pressure",
                value: `${formatRatioPercent(networkSummary.latency_pressure, 0, "-")} / ${formatRatioPercent(networkSummary.queue_pressure, 0, "-")}`,
                subtitle: `latency / queue • rtt ${formatDurationMs(networkSummary.rtt_ms_max)} • q ${formatBytes(networkSummary.queue_bytes)}`,
                tone: toneClassFromPressure(Math.max(
                    parseNumber(networkSummary.latency_pressure, 0),
                    parseNumber(networkSummary.queue_pressure, 0)
                ))
            },
            {
                label: "Growth",
                value: `${formatSignedPercentValue(networkSummary.traffic_growth_pct_per_min, 0)} / min`,
                subtitle: `cross ${formatSignedPercentValue(networkSummary.cross_network_growth_pct_per_min, 0)} • flows ${formatSignedPercentValue(networkSummary.flow_growth_pct_per_min, 0)}`,
                tone: parseNumber(networkSummary.traffic_growth_pct_per_min, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-neutral"
            },
            {
                label: "Attribution Gaps",
                value: `${formatCount(networkSummary.owner_misses, "0")} owner misses`,
                subtitle: `${formatCount(networkSummary.unknown_remote_mac_flows, "0")} unresolved MACs • ${formatCount(networkSummary.window_ms, "0")} ms window`,
                tone: parseNumber(networkSummary.owner_misses, 0) > 0 || parseNumber(networkSummary.unknown_remote_mac_flows, 0) > 0
                    ? "tracey-tone-warn"
                    : "tracey-tone-ok"
            }
        ], "No attributed network telemetry loaded.");

        renderMetricList(nodes.traceyServerForecastFacts, [
            {
                label: "Status",
                value: String(resourceForecast.status || "disabled"),
                subtitle: `${formatCount(resourceForecast.sample_count, "0")} samples • ${formatEpochMs(resourceForecast.ts_ms)}`,
                tone: String(resourceForecast.enabled || "").toLowerCase() === "true" || resourceForecast.enabled === true
                    ? "tracey-tone-ok"
                    : "tracey-tone-neutral"
            },
            {
                label: "5m Projection",
                value: formatBytesRate(resourceForecast.projected_total_bps_5m),
                subtitle: `cross ${formatBytesRate(resourceForecast.projected_cross_network_bps_5m)} • ${formatCount(resourceForecast.projected_active_flows_5m, "0")} flows`,
                tone: "tracey-tone-neutral"
            },
            {
                label: "15m Projection",
                value: formatBytesRate(resourceForecast.projected_total_bps_15m),
                subtitle: `cross ${formatBytesRate(resourceForecast.projected_cross_network_bps_15m)} • ${formatCount(resourceForecast.projected_active_flows_15m, "0")} flows`,
                tone: parseNumber(resourceForecast.projected_total_bps_15m, 0) > parseNumber(networkSummary.attributed_total_bps, 0)
                    ? "tracey-tone-warn"
                    : "tracey-tone-neutral"
            },
            {
                label: "CPU Estimate",
                value: `${formatFixed(forecastSimulation.estimated_cpu_cores, 1, "-")} cores`,
                subtitle: `x${formatFixed(forecastSimulation.cpu_scale_factor, 2, "-")} scale • latency ${formatRatioPercent(forecastSimulation.latency_pressure, 0, "-")}`,
                tone: "tracey-tone-neutral"
            },
            {
                label: "Memory Estimate",
                value: formatBytes(forecastSimulation.estimated_memory_working_set_bytes),
                subtitle: `x${formatFixed(forecastSimulation.memory_scale_factor, 2, "-")} scale • queue ${formatRatioPercent(forecastSimulation.queue_pressure, 0, "-")}`,
                tone: "tracey-tone-neutral"
            },
            {
                label: "Network / Power",
                value: `${formatBytesRate(forecastSimulation.estimated_network_bps)} • ${formatPower(forecastSimulation.estimated_power_w)}`,
                subtitle: `x${formatFixed(forecastSimulation.power_scale_factor, 2, "-")} power • ${formatCount(forecastSimulation.cross_network_flows, "0")} cross flows`,
                tone: "tracey-tone-neutral"
            },
            {
                label: "Simulation Focus",
                value: String(forecastSimulation.dominant_process || "-"),
                subtitle: `remote ${String(forecastSimulation.dominant_remote_ip || "-")}`,
                tone: "tracey-tone-neutral"
            }
        ], "No growth forecast loaded.");

        renderMetricList(nodes.traceyServerGuardFacts, [
            { label: "Enabled", value: guard.enabled === true ? "yes" : "no", subtitle: `deep dive ${guard.deep_dive === true ? "on" : "off"}`, tone: guard.enabled === true ? "tracey-tone-ok" : "tracey-tone-neutral" },
            { label: "Quarantined", value: formatCount(guard.quarantined_devices, "0"), subtitle: `${formatCount(guard.healthy_devices, "0")} healthy / ${formatCount(guard.suspect_devices, "0")} suspect`, tone: parseNumber(guard.quarantined_devices, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-neutral" },
            { label: "Failures", value: formatCount(guard.total_failures, "0"), subtitle: `${formatCount(guard.total_errors, "0")} errors / ${formatCount(guard.total_timeouts, "0")} timeouts`, tone: parseNumber(guard.total_failures, 0) > 0 || parseNumber(guard.total_errors, 0) > 0 || parseNumber(guard.total_timeouts, 0) > 0 ? "tracey-tone-warn" : "tracey-tone-neutral" },
            { label: "Remote Support", value: formatCount(guard.remote_fault_support, "0"), subtitle: `${formatFixed(guard.overhead_budget_pct, 1, "-")}% budget`, tone: "tracey-tone-neutral" }
        ], "No TraceyGuard summary loaded.");

        const adviceRows = [];
        if (String(forecastAdvice.summary || "").trim()) {
            adviceRows.push({
                label: "Summary",
                value: String(forecastAdvice.summary || ""),
                subtitle: [forecastAdvice.provider, forecastAdvice.model].filter(Boolean).join(" • ") || "Local heuristic baseline",
                tone: String(forecastAdvice.last_error || "").trim() ? "tracey-tone-warn" : "tracey-tone-neutral"
            });
        }
        if (String(forecastAdvice.latency_focus || "").trim()) {
            adviceRows.push({ label: "Latency", value: String(forecastAdvice.latency_focus || ""), subtitle: "Tail-latency reduction focus", tone: "tracey-tone-neutral" });
        }
        if (String(forecastAdvice.cpu_focus || "").trim()) {
            adviceRows.push({ label: "CPU", value: String(forecastAdvice.cpu_focus || ""), subtitle: "Host scheduling focus", tone: "tracey-tone-neutral" });
        }
        if (String(forecastAdvice.memory_focus || "").trim()) {
            adviceRows.push({ label: "Memory", value: String(forecastAdvice.memory_focus || ""), subtitle: "Working-set focus", tone: "tracey-tone-neutral" });
        }
        if (String(forecastAdvice.power_focus || "").trim()) {
            adviceRows.push({ label: "Power", value: String(forecastAdvice.power_focus || ""), subtitle: "Power-efficiency focus", tone: "tracey-tone-neutral" });
        }
        if (String(forecastAdvice.simulation_focus || "").trim()) {
            adviceRows.push({ label: "Simulation", value: String(forecastAdvice.simulation_focus || ""), subtitle: `confidence ${formatFixed(forecastAdvice.confidence, 2, "-")}`, tone: "tracey-tone-neutral" });
        }
        if (!adviceRows.length && String(forecastAdvice.last_error || "").trim()) {
            adviceRows.push({
                label: "AI Status",
                value: "Advisory unavailable",
                subtitle: String(forecastAdvice.last_error || ""),
                tone: "tracey-tone-warn"
            });
        }
        renderMetricList(nodes.traceyServerForecastAdvice, adviceRows, "No AI advisory available.");

        const topologyFocus = traceyState.serverTopologyFocus;
        const filteredFlows = (Array.isArray(network.top_flows) ? network.top_flows : [])
            .filter((flow) => flowMatchesTraceyServerTopologyFocus(flow, topologyFocus));
        const matchingFlowPids = new Set(
            filteredFlows
                .map((flow) => Math.round(parseNumber(flow && flow.pid, 0)))
                .filter((pid) => pid > 0)
        );
        const filteredListeners = (Array.isArray(network.top_listeners) ? network.top_listeners : [])
            .filter((listener) => listenerMatchesTraceyServerTopologyFocus(listener, topologyFocus));
        const filteredProcesses = (Array.isArray(network.top_processes) ? network.top_processes : [])
            .filter((process) => processMatchesTraceyServerTopologyFocus(process, topologyFocus, matchingFlowPids));
        renderTraceyServerTopologyFocusBar(topologyFocus, {
            flowCount: filteredFlows.length,
            listenerCount: filteredListeners.length,
            processCount: filteredProcesses.length
        });

        const flowRows = filteredFlows.map((flow) => {
            const severity = String(flow.severity || (flow.anomaly ? "warning" : "ok"));
            const remote = formatEndpoint(flow.remote_ip, flow.remote_port, "-");
            const topology = flow.cross_network ? "cross-network" : (flow.same_lan ? "same-lan" : (flow.local_host ? "localhost" : "mixed"));
            const macLine = [flow.local_mac, flow.remote_mac].filter(Boolean).join(" / ") || "-";
            return `<tr><td>${statusBadge(severity)}<div><strong>${escapeHtml(String(flow.process || "unknown"))}</strong><div>pid ${escapeHtml(String(flow.pid || "-"))}</div></div></td><td><strong>${escapeHtml(formatEndpoint(flow.local_ip, flow.local_port, "-"))}</strong><div>${escapeHtml(remote)}</div></td><td><strong>${escapeHtml(formatBytesRate(flow.total_bps))}</strong><div>rx ${escapeHtml(formatBytesRate(flow.rx_bps))} • tx ${escapeHtml(formatBytesRate(flow.tx_bps))}</div></td><td><strong>${escapeHtml(topology)}</strong><div>${escapeHtml(String(flow.iface || "-"))} • ${escapeHtml(macLine)}</div></td><td><strong>${escapeHtml(formatDurationMs(flow.rtt_ms))}</strong><div>${escapeHtml(`queue ${formatBytes(flow.queue_bytes)} • ${flow.anomaly ? "anomaly" : "normal"}`)}</div></td></tr>`;
        });
        renderRows(
            nodes.traceyServerFlowRows,
            flowRows,
            traceyServerTopologyEmptyMessage(topologyFocus, "attributed flows", "No attributed flows loaded."),
            5
        );

        const listenerRows = filteredListeners.map((listener) => {
            return `<tr><td><strong>${escapeHtml(String(listener.process || "unknown"))}</strong><div>pid ${escapeHtml(String(listener.pid || "-"))} • ${escapeHtml(String(listener.socket_state || "-"))}</div></td><td>${escapeHtml(String(listener.protocol || "-"))}</td><td><strong>${escapeHtml(formatEndpoint(listener.local_ip, listener.local_port, "-"))}</strong><div>${escapeHtml(String(listener.iface || "-"))}</div></td><td><strong>${escapeHtml(formatBytes(listener.queue_bytes))}</strong><div>${escapeHtml(String(listener.severity || "low"))}</div></td><td>${escapeHtml(String(listener.local_mac || "-"))}</td></tr>`;
        });
        renderRows(
            nodes.traceyServerListenerRows,
            listenerRows,
            traceyServerTopologyEmptyMessage(topologyFocus, "listeners", "No listeners loaded."),
            5
        );

        const processPortRows = filteredProcesses.map((process) => {
            const signals = [
                `${formatCount(process.flow_count, "0")} flows`,
                `${formatCount(process.listener_count, "0")} listeners`,
                `${formatCount(process.cross_network_flows, "0")} cross`
            ].join(" • ");
            const remoteFocus = [
                String(process.dominant_remote_ip || "-"),
                process.cgroup ? `cg ${process.cgroup}` : ""
            ].filter(Boolean).join(" • ");
            return `<tr><td>${statusBadge(process.severity || "low")}<div><strong>${escapeHtml(String(process.name || "unknown"))}</strong><div>pid ${escapeHtml(String(process.pid || "-"))}</div></div></td><td><strong>${escapeHtml(formatPortList(process.local_ports))}</strong><div>remote ${escapeHtml(formatPortList(process.remote_ports))}</div></td><td>${escapeHtml(remoteFocus || "-")}</td><td><strong>${escapeHtml(formatBytesRate(process.total_bps))}</strong><div>rx ${escapeHtml(formatBytesRate(process.rx_bps))} • tx ${escapeHtml(formatBytesRate(process.tx_bps))}</div></td><td><strong>${escapeHtml(signals)}</strong><div>rtt ${escapeHtml(formatDurationMs(process.max_rtt_ms))} • q ${escapeHtml(formatBytes(process.queue_bytes))}</div></td></tr>`;
        });
        renderRows(
            nodes.traceyServerPortRows,
            processPortRows,
            traceyServerTopologyEmptyMessage(topologyFocus, "process-port mappings", "No process-port map loaded."),
            5
        );

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
        renderTraceyServerNetworkSection(data);
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
            syncTraceyUrlState();
            return;
        }

        traceyState.selectedRack = normalizedRack;
        syncTraceyUrlState();
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
            syncTraceyUrlState();
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
            syncTraceyUrlState();
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
            syncTraceyUrlState();
            return;
        }

        const currentServerId = traceyText(traceyState.selectedServerTelemetry && traceyState.selectedServerTelemetry.agent_id, "");
        if (currentServerId && !traceyTextEquals(currentServerId, normalizedId)) {
            clearTraceyServerTopologyFocus({ rerender: false });
        }
        traceyState.selectedAgentId = normalizedId;
        syncTraceyUrlState();
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
            syncTraceyUrlState();
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
        syncTraceyUrlState();

        const gpus = Array.isArray(data.gpus) ? data.gpus : [];
        if (!gpus.length) {
            traceyState.selectedGpuId = "";
            traceyState.selectedGpuTelemetry = null;
            renderTraceyGpuTelemetry(null);
            syncTraceyUrlState();
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
            syncTraceyUrlState();
            return;
        }

        traceyState.selectedGpuId = normalizedGpuId;
        syncTraceyUrlState();
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
            syncTraceyUrlState();
            return;
        }

        const data = responseData(response.payload) || {};
        traceyState.selectedGpuTelemetry = data;
        renderTraceyGpuTelemetry(data);
        syncTraceyUrlState();
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
        if (!hasTraceyVisibility()) {
            closeTraceyInsightsModal();
            return;
        }
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
        if (!canControlTracey()) {
            if (nodes.traceyControlStatus) {
                nodes.traceyControlStatus.textContent = "Tracey access is read-only.";
                nodes.traceyControlStatus.style.borderColor = "#9ca3af";
            }
            return;
        }
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
        if (!hasTraceyVisibility()) {
            return;
        }
        closeClusterDetailsModal();
        closeOpenShiftDetailsModal();
        if (preselectedAgentId) {
            traceyState.selectedAgentId = String(preselectedAgentId).trim();
        }
        syncControlAgentInput();
        setTraceyInsightsModalOpen(true);
        syncTraceyUrlState();
        await refreshTraceyInsights();
        syncTraceyUrlState();
    }

    function initializeTraceyInsightsUi() {
        const rerenderFleetAssessment = () => {
            renderTraceyFleetAssessmentRows();
        };
        const rerenderFleetNetwork = () => {
            renderTraceyFleetNetworkSection(traceyState.fleet);
        };
        const rerenderRackAssessment = () => {
            renderTraceyRackDetail(traceyState.selectedRackDetail);
        };
        const rerenderRackNetwork = () => {
            renderTraceyRackNetworkSection(traceyState.selectedRackDetail);
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
        const rerenderServerNetwork = () => {
            renderTraceyServerNetworkSection(traceyState.selectedServerTelemetry);
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
                syncTraceyUrlState();
                refreshTraceyInsights();
            });
        }
        if (nodes.traceyBucketSelect) {
            nodes.traceyBucketSelect.addEventListener("change", () => {
                syncTraceyUrlState();
                refreshTraceyInsights();
            });
        }
        if (nodes.traceyFleetTopologyForecast) {
            nodes.traceyFleetTopologyForecast.value = normalizeTraceyTopologyForecastMode(nodes.traceyFleetTopologyForecast.value);
            nodes.traceyFleetTopologyForecast.addEventListener("change", rerenderFleetNetwork);
        }
        if (nodes.traceyFleetTalkerMode) {
            nodes.traceyFleetTalkerMode.value = normalizeTraceyTalkerMode(nodes.traceyFleetTalkerMode.value);
            nodes.traceyFleetTalkerMode.addEventListener("change", rerenderFleetNetwork);
        }
        if (nodes.traceyRackTopologyForecast) {
            nodes.traceyRackTopologyForecast.value = normalizeTraceyTopologyForecastMode(nodes.traceyRackTopologyForecast.value);
            nodes.traceyRackTopologyForecast.addEventListener("change", rerenderRackNetwork);
        }
        if (nodes.traceyRackTalkerMode) {
            nodes.traceyRackTalkerMode.value = normalizeTraceyTalkerMode(nodes.traceyRackTalkerMode.value);
            nodes.traceyRackTalkerMode.addEventListener("change", rerenderRackNetwork);
        }
        if (nodes.traceyServerTopologyForecast) {
            nodes.traceyServerTopologyForecast.value = normalizeTraceyTopologyForecastMode(nodes.traceyServerTopologyForecast.value);
            nodes.traceyServerTopologyForecast.addEventListener("change", rerenderServerNetwork);
        }
        if (nodes.traceyServerTalkerMode) {
            nodes.traceyServerTalkerMode.value = normalizeTraceyTalkerMode(nodes.traceyServerTalkerMode.value);
            nodes.traceyServerTalkerMode.addEventListener("change", rerenderServerNetwork);
        }
        bindTraceySimulationControls("fleet");
        bindTraceySimulationControls("rack");
        bindTraceySimulationControls("server");
        bindTraceyTopologyInteractions(nodes.traceyFleetTopologyChart);
        bindTraceyTopologyInteractions(nodes.traceyRackTopologyChart);
        bindTraceyTopologyInteractions(nodes.traceyServerTopologyChart);
        if (nodes.traceyServerTopologyClear) {
            nodes.traceyServerTopologyClear.disabled = true;
            nodes.traceyServerTopologyClear.addEventListener("click", () => {
                clearTraceyServerTopologyFocus();
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
        window.addEventListener("popstate", () => {
            const state = readTraceyUrlState();
            if (!state) {
                if (isTraceyInsightsModalOpen()) {
                    closeTraceyInsightsModal();
                }
                return;
            }
            void applyTraceyUrlState(state);
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
            setPill(nodes.authPill, "Auth", "Unauthorised", "rgba(239,68,68,0.85)");
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
        const traceyVisible = hasTraceyVisibility();
        const tradingVisible = hasGailTradingVisibility();

        const [
            k8sListRes,
            vclusterListRes,
            openshiftClustersRes,
            openstackClustersRes,
            proxmoxClustersRes,
            vmListRes,
            traceyAgentsRes,
            aiLabRes,
            traceyAdaptiveRes,
            k8sHealthRes,
            connectionRes,
            openshiftResourcesRes,
            openstackResourcesRes,
            proxmoxResourcesRes,
            gailTradingStatusRes,
            gailApiIssuesRes
        ] = await Promise.all([
            fetchJson("/k8s/list"),
            fetchJson("/vcluster/list"),
            fetchJson("/openshift/clusters"),
            fetchJson("/openstack/clusters"),
            fetchJson("/proxmox/clusters"),
            fetchJson("/vm/list"),
            traceyVisible
                ? fetchJson("/tracey/agents")
                : Promise.resolve({
                    ok: true,
                    status: 200,
                    payload: {
                        data: {
                            agents: [],
                            summary: {},
                            probe_watch_summary: {},
                            tracey_guard_summary: {},
                            loader_threat_summary: {},
                            requirement_summary: {}
                        }
                    }
                }),
            traceyVisible
                ? fetchJson("/tracey/ai-lab")
                : Promise.resolve({
                    ok: true,
                    status: 200,
                    payload: {
                        data: {
                            summary: {},
                            reports: []
                        }
                    }
                }),
            traceyVisible
                ? fetchJson(buildTraceyAdaptivePath())
                : Promise.resolve({ ok: true, status: 200, payload: {} }),
            fetchJson("/k8s/healthz"),
            fetchJson("/connections/status"),
            fetchJson("/openshift/resources"),
            fetchJson("/openstack/resources"),
            fetchJson("/proxmox/resources"),
            tradingVisible
                ? fetchJson("/gail/trading/status")
                : Promise.resolve({ ok: true, status: 200, payload: null }),
            tradingVisible
                ? fetchJson("/gail/status/api-issues")
                : Promise.resolve({ ok: true, status: 200, payload: null })
        ]);

        const allResponses = [
            k8sListRes,
            vclusterListRes,
            openshiftClustersRes,
            openstackClustersRes,
            proxmoxClustersRes,
            vmListRes,
            traceyAgentsRes,
            aiLabRes,
            traceyAdaptiveRes,
            k8sHealthRes,
            connectionRes,
            openshiftResourcesRes,
            openstackResourcesRes,
            proxmoxResourcesRes,
            gailTradingStatusRes,
            gailApiIssuesRes
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
        const probeWatchSummary = traceyData.probe_watch_summary || {};
        const tracey_guardSummary = traceyData.tracey_guard_summary || {};
        const loaderThreatSummary = traceyData.loader_threat_summary || {};
        const requirementSummary = traceyData.requirement_summary || {};
        if (traceyVisible) {
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

            const aiLabData = responseData(aiLabRes.payload) || {};
            const aiLabSummary = aiLabData.summary || {};
            const aiLabReports = Array.isArray(aiLabData.reports) ? aiLabData.reports : [];
            if (nodes.aiLabMetric) {
                nodes.aiLabMetric.textContent = `${Number(aiLabSummary.reports || 0)} / ${Number(aiLabSummary.policy_stopped || 0)}`;
            }
            const aiLabRows = aiLabReports.slice(0, 8).map((report) => {
                const scenarioId = String(report.scenario_id || "unknown");
                const scenarioName = String(report.scenario_name || scenarioId);
                const evidence = String(report.evidence_root || "-");
                return `<tr><td><strong>${escapeHtml(scenarioName)}</strong><br><span class="muted">${escapeHtml(scenarioId)}</span></td><td>${statusBadge(report.status || "unknown")}</td><td>${Number(report.actions_executed || 0)} / ${Number(report.actions_proposed || 0)}</td><td>${Number(report.policy_rejections || 0)} rejected</td><td>${Number(report.events_emitted || 0)}</td><td><code>${escapeHtml(evidence)}</code></td></tr>`;
            });
            renderRows(nodes.aiLabRows, aiLabRows, "No AI lab reports received yet.", 6);
        } else {
            nodes.traceyMetric.textContent = "--";
            if (nodes.aiLabMetric) {
                nodes.aiLabMetric.textContent = "--";
            }
            renderRows(nodes.traceyRows, [], "Tracey access is not granted.", 5);
            renderRows(nodes.traceyNetworkRows, [], "Tracey access is not granted.", 7);
            renderRows(nodes.aiLabRows, [], "Tracey access is not granted.", 6);
        }

        const adaptiveData = traceyVisible && traceyAdaptiveRes.ok ? (responseData(traceyAdaptiveRes.payload) || {}) : null;
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
        const probeAlerts = Number(probeWatchSummary.total_alerts || 0);
        const probeHighAlerts = Number(probeWatchSummary.high_severity_alerts || 0);
        const probeAlertAgents = Number(probeWatchSummary.agents_with_alerts || 0);
        const blockedLoaderProviders = Number(loaderThreatSummary.blocked_provider_count || 0);
        const blockedLoaderArtifacts = Number(loaderThreatSummary.blocked_artifact_count || 0);
        const traceyTone = !traceyVisible
            ? "#9ca3af"
            : !traceyAgentsRes.ok
            ? "#ef4444"
            : (nonCompliantResources > 0 || probeHighAlerts > 0 || tracey_guardQuarantined > 0 || tracey_guardFailures > 0 || blockedLoaderProviders > 0 || blockedLoaderArtifacts > 0 ? "#f59e0b" : "#22c55e");
        setCheck(
            nodes.traceyStatus,
            !traceyVisible
                ? "Not granted"
                : traceyAgentsRes.ok
                ? `${traceyAgents.length} detected (${reachableTracey} reachable, ${discoveredTracey} discovered, ${nonCompliantResources}/${managedResources} non-compliant, ${probeAlerts} probe alerts on ${probeAlertAgents} agents, ${tracey_guardQuarantined} quarantined, ${tracey_guardFailures} probe failures, ${blockedLoaderProviders} blocked providers, ${blockedLoaderArtifacts} blocked artifacts)`
                : "Unavailable",
            traceyTone
        );

        if (nodes.traceyInsightsModal && !nodes.traceyInsightsModal.hidden) {
            refreshTraceyInsights();
        }

        // --- Gail Trading card update ---
        if (tradingVisible && nodes.gailTradingMetric) {
            const ts = gailTradingStatusRes.ok ? gailTradingData(gailTradingStatusRes, null) : null;
            if (ts) {
                const stateLabel = ts.paused ? "Paused" : (ts.enabled ? "Active" : "Disabled");
                const trades = ts.trade_count != null ? ts.trade_count : 0;
                nodes.gailTradingMetric.textContent = `${stateLabel} · ${trades} trades`;
            } else {
                nodes.gailTradingMetric.textContent = gailTradingStatusRes.ok ? "No data" : "Unavailable";
            }
        }
        if (tradingVisible) {
            const ts = gailTradingStatusRes.ok ? gailTradingData(gailTradingStatusRes, null) : null;
            const apiIssues = gailApiIssuesData(gailApiIssuesRes);
            const issueSummary = apiIssues && apiIssues.summary ? apiIssues.summary : {};
            const activeIssues = Number(issueSummary.active || 0);
            const stateLabel = ts
                ? (ts.paused ? "Paused" : (ts.enabled ? "Active" : "Disabled"))
                : (gailTradingStatusRes.ok ? "No data" : "Unavailable");
            setCheck(nodes.gailTradingPanelStatus, stateLabel, ts && ts.enabled && !ts.paused ? "#22c55e" : (ts ? "#f59e0b" : "#ef4444"));
            setCheck(
                nodes.gailTradingPanelIssues,
                gailApiIssuesRes.ok ? `${activeIssues} active` : "Unavailable",
                !gailApiIssuesRes.ok ? "#ef4444" : (activeIssues > 0 ? "#f59e0b" : "#22c55e")
            );
            setCheck(nodes.gailTradingPanelEvaluation, ts ? formatGailTradingTimestamp(ts.last_evaluation_at) : "--", ts ? "#22c55e" : "#9ca3af");
            setCheck(nodes.gailTradingPanelTrade, ts ? formatGailTradingTimestamp(ts.last_trade_at) : "--", ts ? "#22c55e" : "#9ca3af");
        } else {
            setCheck(nodes.gailTradingPanelStatus, "Not granted", "#9ca3af");
            setCheck(nodes.gailTradingPanelIssues, "--", "#9ca3af");
            setCheck(nodes.gailTradingPanelEvaluation, "--", "#9ca3af");
            setCheck(nodes.gailTradingPanelTrade, "--", "#9ca3af");
        }

        if (nodes.gailTradingModal && !nodes.gailTradingModal.hidden) {
            openGailTradingModal();
        }

        setPill(nodes.refreshPill, "Refresh", `Updated ${nowIsoTime()}`, "rgba(216,210,202,0.45)");
    }

    function initializeSessionUi() {
        updateSessionLabel();
        applyTraceyAccessUi();
        applyGailTradingAccessUi();
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
    initializeGailTradingUi();
    renderTraceyAdaptiveOverview(null);
    renderTraceyFleetOverview(null);
    renderTraceyRackDetail(null);
    renderTraceyServerTelemetry(null);
    renderTraceyGpuTelemetry(null);
    renderTraceyAgentDrilldown(null);
    renderTraceyDeepDive(null);
    void (async () => {
        if (authToken && !authIdentity) {
            await refreshAuthIdentity();
        }
        applyTraceyAccessUi();
        applyGailTradingAccessUi();
        await refreshDashboard();
        await applyTraceyUrlState();
    })();
    window.setInterval(refreshDashboard, REFRESH_INTERVAL_MS);
})();
