#pragma once
#include <filesystem>
#include <string>

struct VideoInfo { double duration{}; int width{}; int height{}; double fps{}; };
struct Attempt { int width; int height; int colors; double fps; std::wstring description; };

class WindowsMedia {
public:
    VideoInfo Probe(const std::filesystem::path& input) const;
    void CreateGif(const std::filesystem::path& input, const std::filesystem::path& output, const Attempt& attempt) const;
};
