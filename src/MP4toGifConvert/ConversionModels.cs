namespace MP4toGifConvert;

internal sealed record VideoInfo(double DurationSeconds, int Width, int Height, double FramesPerSecond);

internal sealed record ConversionAttempt(int Width, int Height, int Colors, double FramesPerSecond, string Description);

internal sealed record ConversionResult(bool Success, string? OutputPath, long SizeBytes, string Message);

internal sealed class ConversionException(string message) : Exception(message);
