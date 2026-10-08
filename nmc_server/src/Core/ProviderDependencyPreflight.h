// Server-owned dependency and health gates for provider compute mutations.
#pragma once

#include "ProviderCompute.h"

namespace NMC::Server {

/**
 * @brief Validates and evaluates the Continuum provider dependency policy.
 *
 * The policy is a private, server-owned JSON registry. Client requests select
 * an exact provider resource or an approved create profile; they never supply
 * health URLs or dependency claims. Health probes are bounded and run in
 * parallel immediately before and after provider mutations.
 */
class ProviderDependencyPreflight final {
public:
    /** Return safe readiness metadata without disclosing the configured path. */
    static nlohmann::json status();

    /** Resolve an exact provider/scope/resource/action binding. */
    static ProviderComputeResult resolveAction(const std::string& provider,
                                               const std::string& scope,
                                               const std::string& instanceId,
                                               const std::string& location,
                                               const std::string& action,
                                               nlohmann::json& binding);

    /** Resolve a create profile bound to the exact provider scope and location. */
    static ProviderComputeResult resolveCreate(const std::string& provider,
                                               const std::string& scope,
                                               const nlohmann::json& specification,
                                               nlohmann::json& binding);

    /** Check prerequisite health and the selected pre-mutation target checks. */
    static ProviderComputeResult checkBeforeMutation(const nlohmann::json& binding,
                                                     bool checkHost,
                                                     bool checkDependentServices);

    /** Verify that the host and dependent services recovered after the action. */
    static ProviderComputeResult checkAfterMutation(const nlohmann::json& binding,
                                                    bool checkHost,
                                                    bool checkDependentServices);

    /** Refuse queued work if its server-owned dependency graph changed. */
    static ProviderComputeResult verifyRevision(const nlohmann::json& binding,
                                                const std::string& expectedRevision,
                                                const std::string& expectedPolicyFingerprint);

    /** Reload policy contents at the mutation boundary to close stale-read gaps. */
    static ProviderComputeResult verifyCurrentPolicy(const nlohmann::json& binding);
};

} // namespace NMC::Server
