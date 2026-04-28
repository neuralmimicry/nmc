(() => {
    const STORAGE_KEY = "nmc_dashboard_token";
    const IDENTITY_STORAGE_KEY = "nmc_dashboard_identity";
    const params = new URLSearchParams(window.location.search);
    const form = document.getElementById("authForm");
    if (!form) {
        return;
    }

    const tokenEl = document.getElementById("token");
    const usernameEl = document.getElementById("username");
    const passwordEl = document.getElementById("password");
    const errorEl = document.getElementById("authError");
    const submitBtn = form.querySelector('button[type="submit"]');
    const continueWithoutTokenBtn = document.getElementById("continueWithoutToken");
    const monitoringPrefix = "/services/health/monitoring";
    const basePath = window.location.pathname.startsWith(monitoringPrefix)
        ? monitoringPrefix
        : "";
    const serviceAccess = window.NMCServiceAccess || {
        getServiceAccessMap: () => ({})
    };

    function withBase(path) {
        if (!basePath) {
            return path;
        }
        if (!path.startsWith("/")) {
            return `${basePath}/${path}`;
        }
        return `${basePath}${path}`;
    }

    function homePath() {
        return withBase("/");
    }

    function setError(message) {
        if (!errorEl) {
            return;
        }
        errorEl.textContent = message || "";
        errorEl.hidden = !message;
    }

    function readStoredToken() {
        return (localStorage.getItem(STORAGE_KEY) || "").trim();
    }

    function persistToken(token) {
        const trimmed = (token || "").trim();
        if (trimmed) {
            localStorage.setItem(STORAGE_KEY, trimmed);
        } else {
            localStorage.removeItem(STORAGE_KEY);
            localStorage.removeItem(IDENTITY_STORAGE_KEY);
        }
        return trimmed;
    }

    function persistIdentity(payload) {
        const normalizedServiceAccess = serviceAccess.getServiceAccessMap(payload || {});
        const visibleServices = Object.values(normalizedServiceAccess)
            .filter((entry) => entry && entry.visible)
            .map((entry) => entry.service_key);
        const identity = payload && typeof payload === "object"
            ? {
                user: (payload.user || "").trim(),
                role: (payload.role || "").trim(),
                groups: Array.isArray(payload.groups) ? payload.groups : [],
                is_admin: Boolean(payload.is_admin),
                active_team: payload.active_team && typeof payload.active_team === "object" ? payload.active_team : null,
                team_count: Number.isFinite(payload.team_count) ? payload.team_count : Number(payload.team_count || 0),
                pending_invitation_count: Number.isFinite(payload.pending_invitation_count)
                    ? payload.pending_invitation_count
                    : Number(payload.pending_invitation_count || 0),
                group_memberships: Array.isArray(payload.group_memberships) ? payload.group_memberships : [],
                manageable_groups: Array.isArray(payload.manageable_groups) ? payload.manageable_groups : [],
                visible_groups: Array.isArray(payload.visible_groups) ? payload.visible_groups : [],
                can_manage_access: Boolean(payload.can_manage_access),
                service_access: normalizedServiceAccess,
                visible_services: visibleServices
            }
            : null;
        if (!identity || !identity.user) {
            localStorage.removeItem(IDENTITY_STORAGE_KEY);
            return null;
        }
        localStorage.setItem(IDENTITY_STORAGE_KEY, JSON.stringify(identity));
        return identity;
    }

    function authHeaders(token) {
        const headers = { "Accept": "application/json" };
        if (token) {
            headers.Authorization = `Bearer ${token}`;
            headers["X-NMC-Token"] = token;
        }
        return headers;
    }

    function nextPathFromQuery() {
        const rawNext = (params.get("next") || "").trim();
        if (!rawNext) {
            return homePath();
        }
        if (!rawNext.startsWith("/")) {
            return homePath();
        }
        if (rawNext.startsWith("//")) {
            return homePath();
        }
        if ((basePath && rawNext.startsWith(`${basePath}/login`)) || (!basePath && rawNext.startsWith("/login"))) {
            return homePath();
        }
        return rawNext;
    }

    async function validateToken(token) {
        try {
            const response = await fetch(withBase("/k8s/healthz"), {
                headers: authHeaders(token),
                cache: "no-store"
            });
            return response.status;
        } catch (error) {
            return 0;
        }
    }

    async function loginWithPassword(username, password) {
        try {
            const response = await fetch(withBase("/auth/login"), {
                method: "POST",
                headers: {
                    "Accept": "application/json",
                    "Content-Type": "application/json"
                },
                cache: "no-store",
                body: JSON.stringify({
                    username: (username || "").trim(),
                    password: password || ""
                })
            });
            const payload = await response.json().catch(() => ({}));
            return { ok: response.ok, status: response.status, payload };
        } catch (error) {
            return { ok: false, status: 0, payload: { error: "network_error" } };
        }
    }

    async function fetchAuthSession(token) {
        try {
            const response = await fetch(withBase("/auth/session"), {
                headers: authHeaders(token),
                cache: "no-store"
            });
            if (!response.ok) {
                return null;
            }
            return response.json().catch(() => null);
        } catch (_error) {
            return null;
        }
    }

    async function signIn(token) {
        const status = await validateToken(token);
        if (status === 0) {
            setError("Unable to reach Continuum API. Please try again.");
            return false;
        }
        if (status === 401) {
            setError("Authentication failed. Check your credentials or token and try again.");
            return false;
        }
        const sessionPayload = await fetchAuthSession(token);
        persistIdentity(sessionPayload);
        setError("");
        window.location.href = nextPathFromQuery();
        return true;
    }

    async function signInWithPassword(username, password) {
        const result = await loginWithPassword(username, password);
        if (result.status === 0) {
            setError("Unable to reach central authentication. Please try again.");
            return false;
        }
        if (!result.ok) {
            const message = result.payload?.details || result.payload?.error || "Authentication failed.";
            setError(message);
            return false;
        }
        const token = persistToken(result.payload?.access_token || result.payload?.token || "");
        if (!token) {
            setError("Authentication succeeded, but no access token was returned.");
            return false;
        }
        if (tokenEl) {
            tokenEl.value = token;
        }
        persistIdentity(result.payload);
        return signIn(token);
    }

    function queryReason() {
        const reason = (params.get("reason") || "").toLowerCase();
        if (reason === "unauthorized") {
            setError("Your session has expired or token is missing. Sign in again.");
        } else if (reason === "logout") {
            setError("You have been signed out.");
        }
    }

    form.addEventListener("submit", async (event) => {
        event.preventDefault();
        if (submitBtn) {
            submitBtn.disabled = true;
        }
        const username = usernameEl ? usernameEl.value.trim() : "";
        const password = passwordEl ? passwordEl.value : "";
        if (username || password) {
            if (!username || !password) {
                setError("Enter both username and password.");
                if (submitBtn) {
                    submitBtn.disabled = false;
                }
                return;
            }
            await signInWithPassword(username, password);
            if (passwordEl) {
                passwordEl.value = "";
            }
            if (submitBtn) {
                submitBtn.disabled = false;
            }
            return;
        }
        const token = persistToken(tokenEl ? tokenEl.value : "");
        if (tokenEl) {
            tokenEl.value = token;
        }
        await signIn(token);
        if (submitBtn) {
            submitBtn.disabled = false;
        }
    });

    if (continueWithoutTokenBtn) {
        continueWithoutTokenBtn.addEventListener("click", async () => {
            continueWithoutTokenBtn.disabled = true;
            persistToken("");
            await signIn("");
            continueWithoutTokenBtn.disabled = false;
        });
    }

    const stored = readStoredToken();
    if (tokenEl && stored) {
        tokenEl.value = stored;
    }
    queryReason();
})();
