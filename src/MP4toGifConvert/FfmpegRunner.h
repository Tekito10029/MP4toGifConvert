#pragma once
#include <filesystem>
#include <string>
#include <vector>

struct VideoInfo { double duration{}; int width{}; int height{}; double fps{}; };
struct Attempt { int width; int height; int colors; double fps; std::wstring description; };

class FfmpegRunner {
public:
    explicit FfmpegRunner(const std::filesystem::path& configured = {});
    VideoInfo Probe(const std::filesystem::path& input) const;
    void CreateGif(const std::filesystem::path& input, const std::filesystem::path& output, const Attempt& attempt) const;
    static std::filesystem::path ResolveToolsDirectory(const std::filesystem::path& configured = {});
    static bool HasTools(const std::filesystem::path& directory);
private:
    std::filesystem::path ffmpeg_, ffprobe_;
    static std::string Run(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments);
};

std::filesystem::path LoadToolsDirectory();
void SaveToolsDirectory(const std::filesystem::path& directory);
