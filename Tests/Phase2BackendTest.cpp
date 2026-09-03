#include "StyleAI/StyleAIBackend.h"
#include "StyleAI/StyleTrainingConfig.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

int wmain(int argc, wchar_t** argv) {
    if (argc == 4 || argc == 5) {
        StyleAIBackend backend; std::string error;
        const std::wstring mode = argc == 5 ? argv[4] : L"generate";
        if (!backend.Start(argv[1], std::filesystem::absolute(argv[2]), mode, std::filesystem::absolute(argv[3]), error)) { std::cerr << error << '\n'; return 1; }
        std::this_thread::sleep_for(std::chrono::seconds(5)); backend.Stop();
        if (backend.IsRunning() || backend.GetStatus() != StyleBackendStatus::Stopped) { std::cerr << "real Backend stop test failed\n"; return 1; }
        std::cout << "Real Backend stop test passed.\n"; return 0;
    }
    if (argc != 3) { std::cerr << "usage: Phase2BackendTest python.exe backend.py [config.json]\n"; return 2; }
    const auto testRoot = std::filesystem::current_path() / ".phase2-backend-test";
    std::error_code ec; std::filesystem::create_directories(testRoot / "dataset", ec);
    StyleTrainingConfig config;
    config.datasetPath = testRoot / "dataset"; config.outputName = "BackendTest";
    config.outputDirectory = testRoot / "output"; config.baseModel = "test-model";
    const auto configPath = testRoot / "training_config.json";
    std::string error;
    if (!SaveTrainingConfig(config, configPath, error)) { std::cerr << error << '\n'; return 1; }

    StyleAIBackend backend;
    if (!backend.Start(argv[1], std::filesystem::absolute(argv[2]), configPath, error)) { std::cerr << error << '\n'; return 1; }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (backend.IsRunning() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (backend.IsRunning() || backend.GetStatus() != StyleBackendStatus::Completed || backend.GetExitCode() != 0 || backend.GetProgress() != 1.0f) {
        std::cerr << "completion test failed\n"; backend.Stop(); return 1;
    }
    const auto logs = backend.GetLogs();
    if (logs.size() < 12) { std::cerr << "expected streamed stdout logs\n"; return 1; }

    if (!backend.Start(argv[1], std::filesystem::absolute(argv[2]), testRoot / "missing.json", error)) { std::cerr << error << '\n'; return 1; }
    const auto errorDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (backend.IsRunning() && std::chrono::steady_clock::now() < errorDeadline) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    bool receivedStderr = false;
    for (const auto& log : backend.GetLogs()) receivedStderr = receivedStderr || log.isError;
    if (backend.GetStatus() != StyleBackendStatus::Failed || !receivedStderr) { std::cerr << "stderr test failed\n"; return 1; }

    if (!backend.Start(argv[1], std::filesystem::absolute(argv[2]), configPath, error)) { std::cerr << error << '\n'; return 1; }
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    backend.Stop();
    if (backend.IsRunning() || backend.GetStatus() != StyleBackendStatus::Stopped) { std::cerr << "stop test failed\n"; return 1; }
    std::cout << "Phase 2 backend completion and stop tests passed.\n";
    return 0;
}
