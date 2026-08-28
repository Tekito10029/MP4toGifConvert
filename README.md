# MP4 to GIF Converter

3～10秒のMP4動画を、**512 KB以下**のGIFへ変換するWindowsデスクトップアプリです。

## 変換手順

アプリは次の順序を必ず守り、各試行後に実際のファイルサイズを確認します。

1. 縦横比を維持したまま段階的に縮小します（横125px・縦100pxのどちらも下回りません）。
2. 最小画像サイズでも512 KBを超える場合、色数を256色から200色へ減らします。
3. それでも超える場合、フレームレート（フレーム数）を半分にします。
4. まだ512 KBを超える場合、動画を短くするよう案内し、GIFを出力せず終了します。

入力動画は3～10秒かつ125×100px以上である必要があります。

## 必要環境

- Windows 10/11
- [.NET 8 Desktop Runtime](https://dotnet.microsoft.com/download/dotnet/8.0)
- FFmpegの `ffmpeg.exe` と `ffprobe.exe`

FFmpegの2ファイルをアプリのexeと同じフォルダーに配置してください。配置されていない場合は、環境変数 `PATH` 上のFFmpegを使用します。

## ビルド

Visual Studio 2022で `MP4toGifConvert.sln` を開くか、次のコマンドを実行します。

```powershell
dotnet build MP4toGifConvert.sln -c Release
dotnet publish src/MP4toGifConvert/MP4toGifConvert.csproj -c Release -r win-x64 --self-contained false
```

発行先に `ffmpeg.exe` と `ffprobe.exe` をコピーすれば配布できます。

## 使い方

1. 「選択...」からMP4ファイルを選びます。
2. 必要なら「変更...」でGIFの保存先を変更します。
3. 「GIFに変換」を押します。

変換に成功すると、出力サイズを表示してエクスプローラーで保存場所を開きます。既存の出力ファイルは、変換に成功した場合だけ上書きされます。
