#pragma once
#include <filesystem>
#include <string>
#include <vector>

struct VideoInfo { double duration{}; int width{}; int height{}; double fps{}; };
struct EditOptions {
    double startSeconds{};
    double endSeconds{};
    int width{};
    int height{};
    int colors{};
    double fps{};
};
struct PreviewImage { int width{}; int height{}; std::vector<unsigned char> pixels; };

class WindowsMedia {
public:
    VideoInfo Probe(const std::filesystem::path& input) const;
    PreviewImage CreatePreview(const std::filesystem::path& input, double seconds,
        int outputWidth, int outputHeight, int maximumWidth, int maximumHeight) const;
    void CreateGif(const std::filesystem::path& input, const std::filesystem::path& output, const EditOptions& options) const;
};
