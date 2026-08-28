namespace MP4toGifConvert;

internal sealed record VideoInfo(double DurationSeconds, int Width, int Height, double FramesPerSecond);

internal sealed record ConversionAttempt(int Width, int Height, int Colors, double FramesPerSecond, string Description);

internal sealed record ConversionResult(bool Success, string? OutputPath, long SizeBytes, string Message);

internal class ConversionException(string message) : Exception(message);

internal sealed class FfmpegNotFoundException() : ConversionException(
    "ffmpeg.exe と ffprobe.exe が見つかりません。「FFmpeg設定...」から2ファイルがあるフォルダーを選択してください。");
