namespace MP4toGifConvert;

internal sealed class GifConverter(FfmpegRunner runner)
{
    public const long MaximumBytes = 512 * 1024;
    private const int MinimumWidth = 125;
    private const int MinimumHeight = 100;

    public async Task<ConversionResult> ConvertAsync(string inputPath, string outputPath, IProgress<string>? progress, CancellationToken cancellationToken)
    {
        VideoInfo video = await runner.ProbeAsync(inputPath, cancellationToken);
        if (video.DurationSeconds < 3 || video.DurationSeconds > 10)
            throw new ConversionException($"動画の長さは3～10秒にしてください（選択した動画: {video.DurationSeconds:0.##}秒）。");
        if (video.Width < MinimumWidth || video.Height < MinimumHeight)
            throw new ConversionException($"動画は {MinimumWidth}×{MinimumHeight}px 以上にしてください。");

        string temporaryDirectory = Path.Combine(Path.GetTempPath(), $"MP4toGifConvert-{Guid.NewGuid():N}");
        Directory.CreateDirectory(temporaryDirectory);
        try
        {
            int attemptNumber = 0;
            foreach (ConversionAttempt attempt in BuildAttempts(video))
            {
                cancellationToken.ThrowIfCancellationRequested();
                attemptNumber++;
                progress?.Report($"{attempt.Description}（{attempt.Width}×{attempt.Height}px / {attempt.Colors}色 / {attempt.FramesPerSecond:0.##}fps）");
                string candidate = Path.Combine(temporaryDirectory, $"candidate-{attemptNumber}.gif");
                await runner.CreateGifAsync(inputPath, candidate, attempt, cancellationToken);
                long size = new FileInfo(candidate).Length;
                progress?.Report($"試行 {attemptNumber}: {size / 1024d:0.0} KB");
                if (size <= MaximumBytes)
                {
                    string? directory = Path.GetDirectoryName(outputPath);
                    if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);
                    File.Copy(candidate, outputPath, true);
                    return new ConversionResult(true, outputPath, size, "変換が完了しました。");
                }
            }
            return new ConversionResult(false, null, 0, "512 KB以下にできませんでした。動画の時間を短くして、もう一度お試しください。");
        }
        finally
        {
            try { Directory.Delete(temporaryDirectory, true); } catch (IOException) { }
        }
    }

    internal static IReadOnlyList<ConversionAttempt> BuildAttempts(VideoInfo video)
    {
        var attempts = new List<ConversionAttempt>();
        double minimumScale = Math.Max((double)MinimumWidth / video.Width, (double)MinimumHeight / video.Height);
        double scale = Math.Min(0.8, 1.0);
        var dimensions = new List<(int Width, int Height)>();
        while (scale > minimumScale)
        {
            dimensions.Add((MakeEven((int)(video.Width * scale)), MakeEven((int)(video.Height * scale))));
            scale *= 0.8;
        }
        dimensions.Add((MakeEven((int)Math.Ceiling(video.Width * minimumScale)), MakeEven((int)Math.Ceiling(video.Height * minimumScale))));
        dimensions = dimensions.Distinct().ToList();

        double fps = Math.Clamp(video.FramesPerSecond, 1, 30);
        foreach ((int width, int height) in dimensions)
            attempts.Add(new ConversionAttempt(width, height, 256, fps, "手順1: 画像サイズを縮小中"));

        (int minWidth, int minHeight) = dimensions[^1];
        attempts.Add(new ConversionAttempt(minWidth, minHeight, 200, fps, "手順2: 色数を200色に削減中"));
        attempts.Add(new ConversionAttempt(minWidth, minHeight, 200, Math.Max(1, fps / 2), "手順3: フレーム数を半分に削減中"));
        return attempts;
    }

    private static int MakeEven(int value) => value % 2 == 0 ? value : value + 1;
}
