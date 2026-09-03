#include "StyleAIBackend.h"

#include <algorithm>
#include <charconv>

namespace {
std::wstring Quote(const std::filesystem::path& value) {
    std::wstring text = value.wstring();
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const wchar_t c : text) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') { result.append(slashes * 2 + 1, L'\\'); result += c; slashes = 0; continue; }
        result.append(slashes, L'\\'); slashes = 0; result += c;
    }
    result.append(slashes * 2, L'\\'); result += L'"';
    return result;
}
}

StyleAIBackend::~StyleAIBackend() { Stop(); }

bool StyleAIBackend::Start(const std::filesystem::path& pythonExe, const std::filesystem::path& script,
    const std::filesystem::path& configPath, std::string& error) {
    error.clear();
    if (IsRunning()) { error = "Backend is already running."; return false; }
    if (worker_.joinable()) worker_.join();
    CloseProcessHandles();
    SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
    HANDLE stdoutRead = nullptr, stdoutWrite = nullptr, stderrRead = nullptr, stderrWrite = nullptr;
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &security, 0) || !CreatePipe(&stderrRead, &stderrWrite, &security, 0)) {
        error = "Could not create Backend output pipes.";
        if (stdoutRead) CloseHandle(stdoutRead); if (stdoutWrite) CloseHandle(stdoutWrite);
        if (stderrRead) CloseHandle(stderrRead); if (stderrWrite) CloseHandle(stderrWrite);
        return false;
    }
    SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderrRead, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{ sizeof(startup) };
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = stdoutWrite; startup.hStdError = stderrWrite; startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    std::wstring command = Quote(pythonExe) + L" -u " + Quote(script) + L" " + Quote(configPath);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end()); mutableCommand.push_back(L'\0');
    const std::filesystem::path workingDirectory = script.parent_path();
    const BOOL created = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &process);
    const DWORD createError = created ? ERROR_SUCCESS : GetLastError();
    CloseHandle(stdoutWrite); CloseHandle(stderrWrite);
    if (!created) {
        CloseHandle(stdoutRead); CloseHandle(stderrRead);
        error = "Could not start Python Backend (Windows error " + std::to_string(createError) + ").";
        return false;
    }
    {
        std::scoped_lock lock(processMutex_); process_ = process.hProcess; thread_ = process.hThread;
    }
    { std::scoped_lock lock(logsMutex_); logs_.clear(); }
    progress_.store(0.0f); exitCode_.store(STILL_ACTIVE); status_.store(StyleBackendStatus::Running); running_.store(true);
    worker_ = std::jthread([this, stdoutRead, stderrRead] { Run(stdoutRead, stderrRead); });
    return true;
}

void StyleAIBackend::AppendLine(bool isError, std::string line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) return;
    if (!isError && line.rfind("[Progress]", 0) == 0) {
        const size_t start = line.find_first_of("0123456789", 10);
        if (start != std::string::npos) {
            int value = 0;
            const auto result = std::from_chars(line.data() + start, line.data() + line.size(), value);
            if (result.ec == std::errc()) progress_.store(std::clamp(value / 100.0f, 0.0f, 1.0f));
        }
    }
    std::scoped_lock lock(logsMutex_); logs_.push_back({ isError, std::move(line) });
}

void StyleAIBackend::AppendBytes(bool isError, const char* bytes, size_t size, std::string& pending) {
    pending.append(bytes, size);
    size_t newline = 0;
    while ((newline = pending.find('\n')) != std::string::npos) {
        AppendLine(isError, pending.substr(0, newline)); pending.erase(0, newline + 1);
    }
}

bool StyleAIBackend::DrainPipe(HANDLE pipe, bool isError, StyleAIBackend& backend, std::string& pending) {
    DWORD available = 0;
    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) return false;
    if (available == 0) return true;
    char buffer[4096]; DWORD read = 0;
    if (!ReadFile(pipe, buffer, std::min<DWORD>(available, sizeof(buffer)), &read, nullptr) || read == 0) return false;
    backend.AppendBytes(isError, buffer, read, pending); return true;
}

void StyleAIBackend::Run(HANDLE stdoutRead, HANDLE stderrRead) {
    std::string stdoutPending, stderrPending;
    for (;;) {
        DrainPipe(stdoutRead, false, *this, stdoutPending);
        DrainPipe(stderrRead, true, *this, stderrPending);
        HANDLE process = nullptr; { std::scoped_lock lock(processMutex_); process = process_; }
        if (!process || WaitForSingleObject(process, 15) == WAIT_OBJECT_0) break;
    }
    char tail[4096]; DWORD read = 0;
    while (ReadFile(stdoutRead, tail, sizeof(tail), &read, nullptr) && read > 0) AppendBytes(false, tail, read, stdoutPending);
    while (ReadFile(stderrRead, tail, sizeof(tail), &read, nullptr) && read > 0) AppendBytes(true, tail, read, stderrPending);
    if (!stdoutPending.empty()) AppendLine(false, std::move(stdoutPending));
    if (!stderrPending.empty()) AppendLine(true, std::move(stderrPending));
    DWORD code = 1;
    { std::scoped_lock lock(processMutex_); if (process_) GetExitCodeProcess(process_, &code); }
    exitCode_.store(static_cast<int>(code));
    const StyleBackendStatus previous = status_.load();
    if (previous != StyleBackendStatus::Stopped) status_.store(code == 0 ? StyleBackendStatus::Completed : StyleBackendStatus::Failed);
    running_.store(false, std::memory_order_release);
    CloseHandle(stdoutRead); CloseHandle(stderrRead);
}

void StyleAIBackend::Update() {}

void StyleAIBackend::Stop() {
    if (IsRunning()) {
        status_.store(StyleBackendStatus::Stopped);
        std::scoped_lock lock(processMutex_);
        if (process_) TerminateProcess(process_, 1);
    }
    if (worker_.joinable()) worker_.join();
    running_.store(false); CloseProcessHandles();
}

void StyleAIBackend::CloseProcessHandles() {
    std::scoped_lock lock(processMutex_);
    if (thread_) { CloseHandle(thread_); thread_ = nullptr; }
    if (process_) { CloseHandle(process_); process_ = nullptr; }
}

std::vector<StyleBackendLogEntry> StyleAIBackend::GetLogs() const {
    std::scoped_lock lock(logsMutex_); return logs_;
}
