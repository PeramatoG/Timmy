#include "timmy/core/resource_path_resolver.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

// Verifies deterministic precedence and executable-parent development lookup.
int main()
{
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path()
        / ("timmy-resource-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "models");
    fs::create_directories(root / "out/build/windows-debug");
    std::ofstream(root / "models/default.bin") << "test";
    std::ofstream(root / "supplied.bin") << "test";
    std::ofstream(root / "override.bin") << "test";

    const timmy::core::ResourcePathRequest request{
        "TEST_MODEL", root / "supplied.bin",
        root / "out/build/windows-debug/timmy-gui.exe", "models/default.bin" };
    auto result = timmy::core::resolveResourcePath(request,
        [&](std::string_view) -> std::optional<std::filesystem::path> {
            return (root / "override.bin").string();
        });
    assert(result.found && result.path.filename() == "override.bin");

    result = timmy::core::resolveResourcePath(request,
        [](std::string_view) -> std::optional<std::filesystem::path> { return std::nullopt; });
    assert(result.found && result.path.filename() == "supplied.bin");

    auto developmentRequest = request;
    developmentRequest.suppliedPath = root / "missing.bin";
    result = timmy::core::resolveResourcePath(developmentRequest,
        [](std::string_view) -> std::optional<std::filesystem::path> { return std::nullopt; });
    assert(result.found && result.path.filename() == "default.bin");

    result = timmy::core::resolveResourcePath(developmentRequest,
        [](std::string_view) -> std::optional<std::filesystem::path> { return "missing-override.bin"; });
    assert(!result.found);
    assert(result.diagnostic.find("TEST_MODEL") != std::string::npos);
    fs::remove_all(root);
}
