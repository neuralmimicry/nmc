(() => {
    const STORAGE_KEY = "nmc_dashboard_token";
    const params = new URLSearchParams(window.location.search);
    const form = document.getElementById("authForm");
    if (!form) {
        return;
    }

    const tokenEl = document.getElementById("token");
    const errorEl = document.getElementById("authError");
    const submitBtn = form.querySelector('button[type="submit"]');
    const continueWithoutTokenBtn = document.getElementById("continueWithoutToken");

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
        }
        return trimmed;
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
            return "/";
        }
        if (!rawNext.startsWith("/")) {
            return "/";
        }
        if (rawNext.startsWith("//")) {
            return "/";
        }
        if (rawNext.startsWith("/login")) {
            return "/";
        }
        return rawNext;
    }

    async function validateToken(token) {
        try {
            const response = await fetch("/k8s/healthz", {
                headers: authHeaders(token),
                cache: "no-store"
            });
            return response.status;
        } catch (error) {
            return 0;
        }
    }

    async function signIn(token) {
        const status = await validateToken(token);
        if (status === 0) {
            setError("Unable to reach Continuum API. Please try again.");
            return false;
        }
        if (status === 401) {
            setError("Authentication failed. Check your token and try again.");
            return false;
        }
        setError("");
        window.location.href = nextPathFromQuery();
        return true;
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
