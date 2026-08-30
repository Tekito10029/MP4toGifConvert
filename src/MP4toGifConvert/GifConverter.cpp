#include "GifConverter.h"
#include <windows.h>
#include <stdexcept>

namespace fs = std::filesystem;

ConversionResult GifConverter::Convert(const fs::path& input, const fs::path& output,
    const EditOptions& options, const std::function<void(const std::wstring&)>& progress) const {
    VideoInfo video = media_.Probe(input);
    if (options.startSeconds < 0 || options.endSeconds <= options.startSeconds || options.endSeconds > video.duration + 0.05)
        throw std::runtime_error("切り出し範囲を動画の長さ以内で正しく指定してください。");
    if (options.width < 1 || options.height < 1 || options.width > 4096 || options.height > 4096)
        throw std::runtime_error("出力サイズは1～4096pxで指定してください。");
    if (options.colors < 2 || options.colors > 256) throw std::runtime_error("色数は2～256色で指定してください。");
    if (options.fps < 1 || options.fps > 60) throw std::runtime_error("FPSは1～60で指定してください。");
    fs::path temporary = fs::temp_directory_path() / (L"MP4toGifConvert-" + std::to_wstring(GetTickCount64()));
    fs::create_directories(temporary);
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{temporary};
    progress(L"指定した編集内容でGIFを作成しています...");
    fs::path candidate = temporary / L"edited.gif";
    media_.CreateGif(input, candidate, options);
    auto bytes = fs::file_size(candidate);
    if (!output.parent_path().empty()) fs::create_directories(output.parent_path());
    fs::copy_file(candidate, output, fs::copy_options::overwrite_existing);
    return {true, output, bytes, L"GIFの編集と書き出しが完了しました。"};
}
