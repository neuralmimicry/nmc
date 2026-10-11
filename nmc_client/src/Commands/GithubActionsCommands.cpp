#include "GithubActionsCommands.h"

#include <regex>
#include <utility>

#include <nlohmann/json.hpp>

namespace NMC::Commands {

namespace {

std::string flagValue(const std::map<std::string, std::string>& flags, const std::string& name) {
    const auto found = flags.find(name);
    return found == flags.end() ? std::string() : found->second;
}

bool isSha1(const std::string& value) {
    static const std::regex pattern(R"(^[0-9a-fA-F]{40}$)");
    return std::regex_match(value, pattern);
}

bool isTaskId(const std::string& value) {
    static const std::regex pattern(
            R"(^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$)");
    return std::regex_match(value, pattern);
}

bool isRunId(const std::string& value) {
    static const std::regex pattern(R"(^[0-9]{1,20}$)");
    return std::regex_match(value, pattern);
}

bool isWorkspaceSlug(const std::string& value) {
    static const std::regex pattern(R"(^[a-z0-9-]{1,48}$)");
    return std::regex_match(value, pattern);
}

bool isPackageVersion(const std::string& value) {
    static const std::regex pattern(R"(^[A-Za-z0-9_][A-Za-z0-9_.-]{0,63}$)");
    return std::regex_match(value, pattern);
}

int printInputError(const std::string& message, const CLI::GlobalFlags& globalFlags) {
    Models::CloudResponse response;
    response.success = false;
    response.message = message;
    response.data = nlohmann::json::object();
    response.statusCode = 400;
    BaseCommand::printOutput(response, globalFlags);
    return 1;
}

} // namespace

GithubActionsCommand::GithubActionsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client)
    : BaseCommand("github-actions", "Use Continuum to control allowlisted GitHub Actions tasks", std::move(client)) {
    usage = "nmc github-actions <workflow|dispatch|task|run|cancel|result> [flags]";
    examples =
            "nmc github-actions workflow [--workflow-id nmide-build-images.yml]\n"
            "nmc github-actions dispatch --workflow-id nmide-remote-task.yml --task-id UUID --workspace-id workspace-one --source-sha SHA --source-tree TREE --task-kind test\n"
            "nmc github-actions dispatch --workflow-id nmide-build-images.yml --task-id UUID --workspace-id workspace-one --source-sha SHA --source-tree TREE --package-version pilot-0.1.0\n"
            "nmc github-actions task --task-id UUID\n"
            "nmc github-actions run --run-id 12345 --task-id UUID\n"
            "nmc github-actions result --run-id 12345 --task-id UUID --output ./nmide-result.zip\n"
            "nmc github-actions cancel --run-id 12345 --task-id UUID";
    addFlag(CLI::Flag("t", "task-id", "Continuum task UUID", CLI::FlagType::String, false));
    addFlag(CLI::Flag("w", "workspace-id", "Workspace associated with the task", CLI::FlagType::String, false));
    addFlag(CLI::Flag("s", "source-sha", "Exact checkpoint commit SHA", CLI::FlagType::String, false));
    addFlag(CLI::Flag("S", "source-tree", "Exact checkpoint tree SHA", CLI::FlagType::String, false));
    addFlag(CLI::Flag("k", "task-kind", "Allowlisted task kind: check, test or python-test", CLI::FlagType::String, false));
    addFlag(CLI::Flag("W", "workflow-id", "Allowlisted NMIDE workflow filename", CLI::FlagType::String, false));
    addFlag(CLI::Flag("p", "package-version", "Immutable OCI tag version for the image build workflow", CLI::FlagType::String, false));
    addFlag(CLI::Flag("r", "run-id", "Numeric GitHub Actions run ID", CLI::FlagType::String, false));
    addFlag(CLI::Flag("o", "output", "Destination path for a bounded result ZIP", CLI::FlagType::String, false));
}

int GithubActionsCommand::execute(const std::map<std::string, std::string>& parsedFlags,
                                  const std::vector<std::string>& parsedArgs,
                                  const CLI::GlobalFlags& globalFlags) {
    if (!validateFlags(parsedFlags)) {
        return 1;
    }
    if (parsedArgs.empty()) {
        printHelp();
        return 0;
    }
    if (parsedArgs.size() != 1) {
        return printInputError("Choose one GitHub Actions operation.", globalFlags);
    }

    const std::string& operation = parsedArgs.front();
    const std::string workflowId = flagValue(parsedFlags, "workflow-id");
    const bool imageBuild = workflowId == "nmide-build-images.yml";
    if (!workflowId.empty() && workflowId != "nmide-remote-task.yml" && !imageBuild) {
        return printInputError("Workflow ID must be one of the two allowlisted NeuralMimicry workflows.", globalFlags);
    }
    Models::CloudResponse response;
    if (operation == "workflow") {
        response = apiClient->getGithubActionsWorkflow(workflowId);
    } else if (operation == "dispatch") {
        const std::string taskId = flagValue(parsedFlags, "task-id");
        const std::string workspaceId = flagValue(parsedFlags, "workspace-id");
        const std::string sourceSha = flagValue(parsedFlags, "source-sha");
        const std::string sourceTree = flagValue(parsedFlags, "source-tree");
        const std::string taskKind = flagValue(parsedFlags, "task-kind");
        const std::string packageVersion = flagValue(parsedFlags, "package-version");
        if (!isTaskId(taskId) || !isWorkspaceSlug(workspaceId)
            || !isSha1(sourceSha) || !isSha1(sourceTree)
            || (imageBuild ? !isPackageVersion(packageVersion)
                           : (taskKind != "check" && taskKind != "test" && taskKind != "python-test"))) {
            return printInputError("Dispatch requires a task UUID, workspace ID, exact commit/tree SHA and the selected workflow's valid inputs.", globalFlags);
        }
        nlohmann::json request = {
                {"task_id", taskId},
                {"workspace_id", workspaceId},
                {"source_sha", sourceSha},
                {"source_tree", sourceTree},
                {"workflow_id", workflowId.empty() ? "nmide-remote-task.yml" : workflowId}
        };
        if (imageBuild) request["package_version"] = packageVersion;
        else request["task_kind"] = taskKind;
        response = apiClient->dispatchGithubActionsTask(request);
    } else if (operation == "task") {
        const std::string taskId = flagValue(parsedFlags, "task-id");
        if (!isTaskId(taskId)) {
            return printInputError("Task lookup requires a canonical task UUID.", globalFlags);
        }
        response = apiClient->getGithubActionsTask(taskId);
    } else if (operation == "run" || operation == "cancel" || operation == "result") {
        const std::string runId = flagValue(parsedFlags, "run-id");
        const std::string taskId = flagValue(parsedFlags, "task-id");
        if (!isRunId(runId) || !isTaskId(taskId)) {
            return printInputError("Run operations require a numeric run ID and canonical task UUID.", globalFlags);
        }
        if (operation == "run") {
            response = apiClient->getGithubActionsRun(runId, taskId);
        } else if (operation == "cancel") {
            response = apiClient->cancelGithubActionsRun(runId, taskId);
        } else {
            const std::string output = flagValue(parsedFlags, "output");
            if (output.empty()) {
                return printInputError("Result download requires --output with a destination path.", globalFlags);
            }
            response = apiClient->downloadGithubActionsRunResult(runId, taskId, output);
        }
    } else {
        return printInputError("Operation must be workflow, dispatch, task, run, cancel or result.", globalFlags);
    }

    printOutput(response, globalFlags);
    return response.success ? 0 : 1;
}

} // namespace NMC::Commands
