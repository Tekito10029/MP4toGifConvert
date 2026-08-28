using System.Diagnostics;

namespace MP4toGifConvert;

internal sealed class MainForm : Form
{
    private readonly TextBox _inputBox = new() { ReadOnly = true, Dock = DockStyle.Fill };
    private readonly TextBox _outputBox = new() { ReadOnly = true, Dock = DockStyle.Fill };
    private readonly Button _convertButton = new() { Text = "GIFに変換", AutoSize = true, Enabled = false };
    private readonly ProgressBar _progress = new() { Style = ProgressBarStyle.Marquee, MarqueeAnimationSpeed = 0, Dock = DockStyle.Fill };
    private readonly Label _status = new() { Text = "3～10秒のMP4ファイルを選択してください。", AutoSize = true };
    private readonly Label _ffmpegStatus = new() { AutoSize = true, Anchor = AnchorStyles.Left };
    private string? _inputPath;
    private string? _outputPath;
    private string? _toolsDirectory;

    public MainForm()
    {
        Text = "MP4 → GIF（512 KB以下）";
        ClientSize = new Size(720, 280);
        MinimumSize = new Size(620, 300);
        StartPosition = FormStartPosition.CenterScreen;

        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(18), ColumnCount = 3, RowCount = 7 };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        layout.Controls.Add(new Label { Text = "入力 MP4", AutoSize = true, Anchor = AnchorStyles.Left }, 0, 0);
        layout.Controls.Add(_inputBox, 1, 0);
        var selectButton = new Button { Text = "選択...", AutoSize = true };
        selectButton.Click += SelectInput;
        layout.Controls.Add(selectButton, 2, 0);
        layout.Controls.Add(new Label { Text = "出力 GIF", AutoSize = true, Anchor = AnchorStyles.Left }, 0, 1);
        layout.Controls.Add(_outputBox, 1, 1);
        var outputButton = new Button { Text = "変更...", AutoSize = true };
        outputButton.Click += SelectOutput;
        layout.Controls.Add(outputButton, 2, 1);
        layout.Controls.Add(new Label { Text = "FFmpeg", AutoSize = true, Anchor = AnchorStyles.Left }, 0, 2);
        layout.Controls.Add(_ffmpegStatus, 1, 2);
        var ffmpegButton = new Button { Text = "FFmpeg設定...", AutoSize = true };
        ffmpegButton.Click += SelectFfmpegDirectory;
        layout.Controls.Add(ffmpegButton, 2, 2);
        layout.Controls.Add(new Label { Text = "上限: 512 KB　最小画像サイズ: 横125px・縦100px", AutoSize = true }, 1, 3);
        layout.Controls.Add(_progress, 0, 4);
        layout.SetColumnSpan(_progress, 3);
        layout.Controls.Add(_status, 0, 5);
        layout.SetColumnSpan(_status, 3);
        layout.Controls.Add(_convertButton, 2, 6);
        _convertButton.Click += Convert;
        Controls.Add(layout);
        AcceptButton = _convertButton;
        RefreshFfmpegStatus();
    }

    private void SelectInput(object? sender, EventArgs e)
    {
        using var dialog = new OpenFileDialog { Filter = "MP4 動画 (*.mp4)|*.mp4", CheckFileExists = true, Title = "MP4ファイルを選択" };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        _inputPath = dialog.FileName;
        _outputPath = Path.Combine(Path.GetDirectoryName(dialog.FileName)!, Path.GetFileNameWithoutExtension(dialog.FileName) + ".gif");
        _inputBox.Text = _inputPath;
        _outputBox.Text = _outputPath;
        _convertButton.Enabled = true;
        _status.Text = "変換を開始できます。";
    }

    private void SelectOutput(object? sender, EventArgs e)
    {
        using var dialog = new SaveFileDialog { Filter = "GIF画像 (*.gif)|*.gif", DefaultExt = "gif", FileName = _outputPath is null ? "output.gif" : Path.GetFileName(_outputPath) };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        _outputPath = dialog.FileName;
        _outputBox.Text = _outputPath;
    }

    private async void Convert(object? sender, EventArgs e)
    {
        if (_inputPath is null || _outputPath is null) return;
        SetBusy(true);
        try
        {
            var progress = new Progress<string>(message => _status.Text = message);
            ConversionResult result = await new GifConverter(new FfmpegRunner(_toolsDirectory)).ConvertAsync(_inputPath, _outputPath, progress, CancellationToken.None);
            _status.Text = result.Success ? $"完了: {result.SizeBytes / 1024d:0.0} KB" : result.Message;
            MessageBox.Show(this, result.Message + (result.Success ? $"\n\n{result.OutputPath}\nサイズ: {result.SizeBytes / 1024d:0.0} KB" : ""),
                result.Success ? "変換完了" : "変換できませんでした", MessageBoxButtons.OK,
                result.Success ? MessageBoxIcon.Information : MessageBoxIcon.Warning);
            if (result.Success) Process.Start(new ProcessStartInfo("explorer.exe", $"/select,\"{result.OutputPath}\"") { UseShellExecute = true });
        }
        catch (FfmpegNotFoundException ex)
        {
            ShowError(ex.Message);
            SelectFfmpegDirectory(this, EventArgs.Empty);
        }
        catch (ConversionException ex) { ShowError(ex.Message); }
        catch (OperationCanceledException) { _status.Text = "キャンセルしました。"; }
        catch (Exception ex) { ShowError($"予期しないエラーが発生しました。\n{ex.Message}"); }
        finally { SetBusy(false); }
    }

    private void SetBusy(bool busy)
    {
        _convertButton.Enabled = !busy && _inputPath is not null;
        _progress.MarqueeAnimationSpeed = busy ? 30 : 0;
    }

    private void ShowError(string message)
    {
        _status.Text = message.Split('\n')[0];
        MessageBox.Show(this, message, "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error);
    }

    private void SelectFfmpegDirectory(object? sender, EventArgs e)
    {
        using var dialog = new OpenFileDialog
        {
            Filter = "ffmpeg.exe|ffmpeg.exe",
            FileName = "ffmpeg.exe",
            CheckFileExists = true,
            Title = "ffmpeg.exeを選択（同じフォルダーにffprobe.exeも必要です）"
        };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;

        string directory = Path.GetDirectoryName(dialog.FileName)!;
        if (!FfmpegRunner.HasRequiredTools(directory))
        {
            ShowError("選択したフォルダーに ffprobe.exe がありません。ffmpeg.exe と ffprobe.exe の両方を同じフォルダーへ配置してください。");
            return;
        }

        try
        {
            AppSettings.SaveToolsDirectory(directory);
            _toolsDirectory = directory;
            RefreshFfmpegStatus();
            _status.Text = "FFmpegを設定しました。変換を開始できます。";
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            ShowError($"FFmpeg設定を保存できませんでした。\n{ex.Message}");
        }
    }

    private void RefreshFfmpegStatus()
    {
        _toolsDirectory = FfmpegRunner.ResolveToolsDirectory(_toolsDirectory);
        _ffmpegStatus.Text = _toolsDirectory is null ? "未設定" : _toolsDirectory;
        _ffmpegStatus.ForeColor = _toolsDirectory is null ? Color.Firebrick : Color.DarkGreen;
    }
}
