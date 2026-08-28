#include "GifConverter.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;
static int Even(int value) { return value % 2 == 0 ? value : value + 1; }

std::vector<Attempt> GifConverter::BuildAttempts(const VideoInfo& video) {
    constexpr int minWidth = 125, minHeight = 100;
    double minimumScale = std::max(static_cast<double>(minWidth) / video.width, static_cast<double>(minHeight) / video.height);
    std::vector<std::pair<int, int>> dimensions;
    for (double scale = 0.8; scale > minimumScale; scale *= 0.8)
        dimensions.emplace_back(Even(static_cast<int>(video.width * scale)), Even(static_cast<int>(video.height * scale)));
    dimensions.emplace_back(Even(static_cast<int>(std::ceil(video.width * minimumScale))), Even(static_cast<int>(std::ceil(video.height * minimumScale))));
    dimensions.erase(std::unique(dimensions.begin(), dimensions.end()), dimensions.end());
    double fps = std::clamp(video.fps, 1.0, 30.0);
    std::vector<Attempt> attempts;
    for (auto [width, height] : dimensions) attempts.push_back({width, height, 256, fps, L"手順1: 画像サイズを縮小中"});
    auto [width, height] = dimensions.back();
    attempts.push_back({width, height, 200, fps, L"手順2: 色数を200色に削減中"});
    attempts.push_back({width, height, 200, std::max(1.0, fps / 2.0), L"手順3: フレーム数を半分に削減中"});
    return attempts;
}

ConversionResult GifConverter::Convert(const fs::path& input, const fs::path& output,
    const std::function<void(const std::wstring&)>& progress) const {
    VideoInfo video = media_.Probe(input);
    if (video.duration > 10) throw std::runtime_error("動画の長さは10秒以下にしてください。");
    if (video.width < 125 || video.height < 100) throw std::runtime_error("動画は125×100px以上にしてください。");
    fs::path temporary = fs::temp_directory_path() / (L"MP4toGifConvert-" + std::to_wstring(GetTickCount64()));
    fs::create_directories(temporary);
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{temporary};
    int number = 0;
    for (const auto& attempt : BuildAttempts(video)) {
        progress(attempt.description + L"（" + std::to_wstring(attempt.width) + L"×" + std::to_wstring(attempt.height) + L"px）");
        fs::path candidate = temporary / (L"candidate-" + std::to_wstring(++number) + L".gif");
        media_.CreateGif(input, candidate, attempt);
        auto bytes = fs::file_size(candidate);
        progress(L"試行 " + std::to_wstring(number) + L": " + std::to_wstring(bytes / 1024) + L" KB");
        if (bytes <= MaximumBytes) {
            if (!output.parent_path().empty()) fs::create_directories(output.parent_path());
            fs::copy_file(candidate, output, fs::copy_options::overwrite_existing);
            return {true, output, bytes, L"変換が完了しました。"};
        }
    }
    return {false, {}, 0, L"512 KB以下にできませんでした。動画の時間を短くして、もう一度お試しください。"};
}
