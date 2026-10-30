/* Shared by the guarded metadata endpoint and the compatible docs-only update.
   Nothing here restarts workloads or equates restart eligibility with health. */
(function (global) {
    'use strict';
    const deploymentProducts = new Set(['cora', 'foresight', 'verity', 'relay']);
    const dnsLabel = /^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$/;
    function observation(result) {
        if (!result || !result.ok || result.payload?.success === false) return 'Observation unavailable';
        const state = result.payload?.data || result.payload;
        if (!state?.uid || !Number.isInteger(state.desired_replicas)) return 'Not observed';
        const desired = state.desired_replicas;
        if (desired === 0) return 'Suspended: zero desired replicas';
        const available = Number.isInteger(state.available_replicas) ? state.available_replicas : 0;
        const ready = Number.isInteger(state.ready_replicas) ? state.ready_replicas : 0;
        const current = Number.isInteger(state.generation) && Number.isInteger(state.observed_generation) && state.observed_generation >= state.generation;
        return desired > 0 && available >= desired && ready >= desired && current
            ? `Available: ${available}/${desired} replicas`
            : `Degraded or updating: ${available}/${desired} available`;
    }
    function statusPath(product, catalogue) {
        if (!deploymentProducts.has(product.id) || product.runtime_target?.namespace !== product.id || product.runtime_target?.deployment !== product.id) return null;
        if (typeof product.status_path === 'string') {
            const url = new URL(product.status_path, 'https://continuum.invalid');
            if (url.origin === 'https://continuum.invalid' && url.pathname === '/k8s/deployment/recovery-status'
                && dnsLabel.test(url.searchParams.get('cluster_id') || '') && url.searchParams.get('namespace') === product.id && url.searchParams.get('deployment') === product.id) return url.pathname + url.search;
            return null;
        }
        // Older reviewed servers already serve docs JSON and guarded workload
        // observations. Ansible supplies only the existing non-secret cluster ID.
        if (!dnsLabel.test(catalogue.runtime_cluster_id || '')) return null;
        return `/k8s/deployment/recovery-status?${new URLSearchParams({cluster_id:catalogue.runtime_cluster_id,namespace:product.id,deployment:product.id})}`;
    }
    async function refresh(root, status, fetchJson) {
        try {
            let result = await fetchJson('/platform/products');
            if (result.status === 404) result = await fetchJson('/services/health/monitoring/public/products.json');
            const catalogue = result.payload?.data || result.payload;
            if (!result.ok || catalogue?.schema_version !== 1 || !Array.isArray(catalogue.products)) throw Error('unavailable');
            if (catalogue.products.length > 32 || new Set(catalogue.products.map(product => product.id)).size !== catalogue.products.length) throw Error('invalid catalogue');
            const observations = await Promise.all(catalogue.products.map(async product => {
                const path = statusPath(product, catalogue);
                return path ? observation(await fetchJson(path)) : 'Catalogued; runtime not observed';
            }));
            root.replaceChildren();
            catalogue.products.forEach((product, index) => {
                const card = document.createElement('article');
                card.className = 'platform-product-card';
                const heading = document.createElement('h4'); heading.textContent = product.name;
                const description = document.createElement('p'); description.textContent = product.description;
                const runtime = document.createElement('p'); runtime.className = 'platform-product-observation'; runtime.textContent = observations[index];
                const owners = document.createElement('p'); owners.textContent = 'Change approval: Conductor · Release and operations: Continuum';
                const link = document.createElement('a'); link.href = `https://neuralmimicry.ai/${encodeURIComponent(product.id)}`;
                if (product.id === 'aarnn') link.href = 'https://neuralmimicry.ai/aarnn-neuroscience';
                link.textContent = `About ${product.name}`;
                card.append(heading, description, runtime, owners, link); root.append(card);
            });
            status.textContent = `${catalogue.products.length} declared products. Availability is a deployment observation, not business or model qualification.`;
        } catch (_) {
            root.replaceChildren();
            status.textContent = 'Product catalogue or observation unavailable. No healthy state is inferred.';
        }
    }
    const api = { observation, statusPath, refresh };
    if (typeof module !== 'undefined' && module.exports) module.exports = api;
    global.NMCProductCatalog = api;
})(typeof window === 'undefined' ? globalThis : window);
