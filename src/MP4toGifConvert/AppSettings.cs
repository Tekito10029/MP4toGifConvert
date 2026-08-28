using System.Text.Json;

namespace MP4toGifConvert;

internal static class AppSettings
{
    private sealed record Settings(string? ToolsDirectory);

    private static readonly string SettingsPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "MP4toGifConvert", "settings.json");

    public static string? LoadToolsDirectory()
    {
        try
        {
            if (!File.Exists(SettingsPath)) return null;
            return JsonSerializer.Deserialize<Settings>(File.ReadAllText(SettingsPath))?.ToolsDirectory;
        }
        catch (JsonException) { return null; }
        catch (IOException) { return null; }
        catch (UnauthorizedAccessException) { return null; }
    }

    public static void SaveToolsDirectory(string directory)
    {
        string? parent = Path.GetDirectoryName(SettingsPath);
        if (parent is not null) Directory.CreateDirectory(parent);
        File.WriteAllText(SettingsPath, JsonSerializer.Serialize(new Settings(directory)));
    }
}
