#include "FfmpegRunner.h"
#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

static std::wstring Quote(const std::wstring& value) {
    std::wstring result = L"\"";
    unsigned slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') { result.append(slashes * 2 + 1, L'\\'); result += c; slashes = 0; continue; }
        result.append(slashes, L'\\'); slashes = 0; result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}

static fs::path ExecutableDirectory() {
    std::wstring buffer(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return fs::path(buffer).parent_path();
}

static fs::path SettingsPath() {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw))) return {};
    fs::path path(raw); CoTaskMemFree(raw);
    return path / L"MP4toGifConvert" / L"settings.ini";
}

fs::path LoadToolsDirectory() {
    fs::path file = SettingsPath();
    if (file.empty()) return {};
    std::wstring value(32768, L'\0');
    DWORD length = GetPrivateProfileStringW(L"FFmpeg", L"ToolsDirectory", L"", value.data(), static_cast<DWORD>(value.size()), file.c_str());
    value.resize(length);
    return fs::path(value);
}

void SaveToolsDirectory(const fs::path& directory) {
    fs::path file = SettingsPath();
    if (file.empty()) throw std::runtime_error("設定フォルダーを取得できませんでした。");
    fs::create_directories(file.parent_path());
    if (!WritePrivateProfileStringW(L"FFmpeg", L"ToolsDirectory", directory.c_str(), file.c_str()))
        throw std::runtime_error("FFmpeg設定を保存できませんでした。");
}

bool FfmpegRunner::HasTools(const fs::path& directory) {
    std::error_code ec;
    return !directory.empty() && fs::is_regular_file(directory / L"ffmpeg.exe", ec) &&
        fs::is_regular_file(directory / L"ffprobe.exe", ec);
}

fs::path FfmpegRunner::ResolveToolsDirectory(const fs::path& configured) {
    std::vector<fs::path> candidates{configured, LoadToolsDirectory(), ExecutableDirectory() / L"tools", ExecutableDirectory()};
    if (const wchar_t* path = _wgetenv(L"PATH")) {
        std::wstringstream stream(path); std::wstring item;
        while (std::getline(stream, item, L';')) if (!item.empty()) candidates.emplace_back(item);
    }
    for (const auto& candidate : candidates) if (HasTools(candidate)) return fs::absolute(candidate);
    return {};
}

FfmpegRunner::FfmpegRunner(const fs::path& configured) {
    fs::path directory = ResolveToolsDirectory(configured);
    if (directory.empty()) throw std::runtime_error("ffmpeg.exe と ffprobe.exe が見つかりません。［FFmpeg設定］から選択してください。");
    ffmpeg_ = directory / L"ffmpeg.exe"; ffprobe_ = directory / L"ffprobe.exe";
}

std::string FfmpegRunner::Run(const fs::path& executable, const std::vector<std::wstring>& arguments) {
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) throw std::runtime_error("FFmpegの出力を取得できませんでした。");
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    std::wstring command = Quote(executable.wstring());
    for (const auto& argument : arguments) command += L" " + Quote(argument);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end()); mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{sizeof(STARTUPINFOW)}; startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe; startup.hStdError = writePipe; startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    BOOL started = CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(writePipe);
    if (!started) { CloseHandle(readPipe); throw std::runtime_error("FFmpegを起動できませんでした。"); }
    std::string output; char buffer[4096]; DWORD count = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &count, nullptr) && count) output.append(buffer, count);
    CloseHandle(readPipe); WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1; GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    if (exitCode != 0) throw std::runtime_error("FFmpegの処理に失敗しました。\n" + output);
    return output;
}

VideoInfo FfmpegRunner::Probe(const fs::path& input) const {
    std::string text = Run(ffprobe_, {L"-v", L"error", L"-select_streams", L"v:0", L"-show_entries",
        L"stream=width,height,avg_frame_rate:format=duration", L"-of", L"default=noprint_wrappers=1:nokey=1", input.wstring()});
    std::istringstream lines(text); std::string width, height, rate, duration;
    std::getline(lines, width); std::getline(lines, height); std::getline(lines, rate); std::getline(lines, duration);
    try {
        auto slash = rate.find('/');
        double fps = slash == std::string::npos ? std::stod(rate) : std::stod(rate.substr(0, slash)) / std::stod(rate.substr(slash + 1));
        return {std::stod(duration), std::stoi(width), std::stoi(height), fps > 0 ? fps : 10.0};
    } catch (...) { throw std::runtime_error("動画情報を読み取れませんでした。MP4ファイルを確認してください。"); }
}

void FfmpegRunner::CreateGif(const fs::path& input, const fs::path& output, const Attempt& attempt) const {
    fs::path palette = output.parent_path() / (L"palette-" + std::to_wstring(GetTickCount64()) + L".png");
    std::wostringstream fps; fps << attempt.fps;
    std::wstring filters = L"fps=" + fps.str() + L",scale=" + std::to_wstring(attempt.width) + L":" + std::to_wstring(attempt.height) + L":flags=lanczos";
    try {
        Run(ffmpeg_, {L"-y", L"-v", L"error", L"-i", input.wstring(), L"-vf", filters + L",palettegen=max_colors=" + std::to_wstring(attempt.colors) + L":stats_mode=diff", palette.wstring()});
        Run(ffmpeg_, {L"-y", L"-v", L"error", L"-i", input.wstring(), L"-i", palette.wstring(), L"-lavfi", filters + L" [x]; [x][1:v] paletteuse=dither=sierra2_4a", L"-loop", L"0", output.wstring()});
    } catch (...) { std::error_code ec; fs::remove(palette, ec); throw; }
    std::error_code ec; fs::remove(palette, ec);
}
