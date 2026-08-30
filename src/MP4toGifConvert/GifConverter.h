#pragma once
#include "WindowsMedia.h"
#include <functional>
#include <utility>

struct ConversionResult { bool success{}; std::filesystem::path output; std::uintmax_t bytes{}; std::wstring message; };

class GifConverter {
public:
    explicit GifConverter(WindowsMedia media = {}) : media_(std::move(media)) {}
    ConversionResult Convert(const std::filesystem::path& input, const std::filesystem::path& output,
        const EditOptions& options, const std::function<void(const std::wstring&)>& progress) const;
private:
    WindowsMedia media_;
};
