#pragma once
#include "FfmpegRunner.h"
#include <functional>

struct ConversionResult { bool success{}; std::filesystem::path output; std::uintmax_t bytes{}; std::wstring message; };

class GifConverter {
public:
    static constexpr std::uintmax_t MaximumBytes = 512u * 1024u;
    explicit GifConverter(FfmpegRunner runner) : runner_(std::move(runner)) {}
    ConversionResult Convert(const std::filesystem::path& input, const std::filesystem::path& output,
        const std::function<void(const std::wstring&)>& progress) const;
    static std::vector<Attempt> BuildAttempts(const VideoInfo& video);
private:
    FfmpegRunner runner_;
};
