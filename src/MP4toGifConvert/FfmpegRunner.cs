using System.Diagnostics;
using System.Globalization;
using System.Text.Json;

namespace MP4toGifConvert;

internal sealed class FfmpegRunner
{
    private readonly string _ffmpeg;
    private readonly string _ffprobe;

    public FfmpegRunner(string? toolsDirectory = null)
    {
        string? directory = ResolveToolsDirectory(toolsDirectory);
        if (directory is null)
            throw new FfmpegNotFoundException();

        _ffmpeg = Path.Combine(directory, "ffmpeg.exe");
        _ffprobe = Path.Combine(directory, "ffprobe.exe");
    }

    public async Task<VideoInfo> ProbeAsync(string inputPath, CancellationToken cancellationToken)
    {
        string json = await RunAndCaptureAsync(_ffprobe,
            ["-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height,avg_frame_rate:format=duration", "-of", "json", inputPath],
            cancellationToken);

        using JsonDocument document = JsonDocument.Parse(json);
        JsonElement stream = document.RootElement.GetProperty("streams")[0];
        JsonElement format = document.RootElement.GetProperty("format");
        double duration = double.Parse(format.GetProperty("duration").GetString()!, CultureInfo.InvariantCulture);
        int width = stream.GetProperty("width").GetInt32();
        int height = stream.GetProperty("height").GetInt32();
        double fps = ParseRate(stream.GetProperty("avg_frame_rate").GetString()!);
        if (fps <= 0) fps = 10;
        return new VideoInfo(duration, width, height, fps);
    }

    public async Task CreateGifAsync(string inputPath, string outputPath, ConversionAttempt attempt, CancellationToken cancellationToken)
    {
        string palettePath = Path.Combine(Path.GetDirectoryName(outputPath)!, $"palette-{Guid.NewGuid():N}.png");
        string fps = attempt.FramesPerSecond.ToString("0.###", CultureInfo.InvariantCulture);
        string filters = $"fps={fps},scale={attempt.Width}:{attempt.Height}:flags=lanczos";
        try
        {
            await RunAndCaptureAsync(_ffmpeg,
                ["-y", "-v", "error", "-i", inputPath, "-vf", $"{filters},palettegen=max_colors={attempt.Colors}:stats_mode=diff", palettePath],
                cancellationToken);
            await RunAndCaptureAsync(_ffmpeg,
                ["-y", "-v", "error", "-i", inputPath, "-i", palettePath, "-lavfi", $"{filters} [x]; [x][1:v] paletteuse=dither=sierra2_4a", "-loop", "0", outputPath],
                cancellationToken);
        }
        finally
        {
            if (File.Exists(palettePath)) File.Delete(palettePath);
        }
    }

    public static string? ResolveToolsDirectory(string? configuredDirectory = null)
    {
        IEnumerable<string?> candidates =
        [
            configuredDirectory,
            AppSettings.LoadToolsDirectory(),
            Path.Combine(AppContext.BaseDirectory, "tools"),
            AppContext.BaseDirectory,
            .. GetPathDirectories()
        ];

        return candidates
            .Where(directory => !string.IsNullOrWhiteSpace(directory))
            .Select(TryGetFullPath)
            .FirstOrDefault(directory => directory is not null && HasRequiredTools(directory));
    }

    public static bool HasRequiredTools(string directory) =>
        File.Exists(Path.Combine(directory, "ffmpeg.exe")) &&
        File.Exists(Path.Combine(directory, "ffprobe.exe"));

    private static IEnumerable<string> GetPathDirectories() =>
        (Environment.GetEnvironmentVariable("PATH") ?? string.Empty)
            .Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

    private static string? TryGetFullPath(string? directory)
    {
        try { return Path.GetFullPath(directory!); }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException or PathTooLongException) { return null; }
    }

    private static double ParseRate(string rate)
    {
        string[] parts = rate.Split('/');
        if (parts.Length != 2 || !double.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out double numerator)
            || !double.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out double denominator) || denominator == 0)
            return 0;
        return numerator / denominator;
    }

    private static async Task<string> RunAndCaptureAsync(string executable, IReadOnlyList<string> arguments, CancellationToken cancellationToken)
    {
        var startInfo = new ProcessStartInfo(executable) { RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false, CreateNoWindow = true };
        foreach (string argument in arguments) startInfo.ArgumentList.Add(argument);
        try
        {
            using var process = new Process { StartInfo = startInfo };
            process.Start();
            Task<string> outputTask = process.StandardOutput.ReadToEndAsync(cancellationToken);
            Task<string> errorTask = process.StandardError.ReadToEndAsync(cancellationToken);
            await process.WaitForExitAsync(cancellationToken);
            string output = await outputTask;
            string error = await errorTask;
            if (process.ExitCode != 0) throw new ConversionException($"FFmpeg の処理に失敗しました。\n{error.Trim()}");
            return output;
        }
        catch (System.ComponentModel.Win32Exception)
        {
            throw new FfmpegNotFoundException();
        }
    }
}
