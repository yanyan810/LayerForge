#pragma once

#include <Windows.h>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class StyleBackendStatus { Idle, Running, Completed, Failed, Stopped };

struct StyleBackendLogEntry {
    bool isError = false;
    std::string text;
};

class StyleAIBackend {
public:
    StyleAIBackend() = default;
    ~StyleAIBackend();
    StyleAIBackend(const StyleAIBackend&) = delete;
    StyleAIBackend& operator=(const StyleAIBackend&) = delete;

    bool Start(const std::filesystem::path& pythonExe, const std::filesystem::path& script,
        const std::filesystem::path& configPath, std::string& error);
    void Update();
    void Stop();

    [[nodiscard]] bool IsRunning() const { return running_.load(std::memory_order_acquire); }
    [[nodiscard]] int GetExitCode() const { return exitCode_.load(std::memory_order_acquire); }
    [[nodiscard]] float GetProgress() const { return progress_.load(std::memory_order_acquire); }
    [[nodiscard]] StyleBackendStatus GetStatus() const { return status_.load(std::memory_order_acquire); }
    [[nodiscard]] std::vector<StyleBackendLogEntry> GetLogs() const;

private:
    void Run(HANDLE stdoutRead, HANDLE stderrRead);
    void AppendBytes(bool isError, const char* bytes, size_t size, std::string& pending);
    void AppendLine(bool isError, std::string line);
    static bool DrainPipe(HANDLE pipe, bool isError, StyleAIBackend& backend, std::string& pending);
    void CloseProcessHandles();

    mutable std::mutex logsMutex_;
    std::vector<StyleBackendLogEntry> logs_;
    std::mutex processMutex_;
    HANDLE process_ = nullptr;
    HANDLE thread_ = nullptr;
    std::jthread worker_;
    std::atomic_bool running_ = false;
    std::atomic_int exitCode_ = 0;
    std::atomic<float> progress_ = 0.0f;
    std::atomic<StyleBackendStatus> status_ = StyleBackendStatus::Idle;
};
