# MP4 to GIF Converter（C++版）

3～10秒のMP4動画を**512 KB以下**のGIFへ変換する、ネイティブWindowsデスクトップアプリです。C++17とWin32 APIで実装しており、.NETランタイムは不要です。

## 変換手順

各試行後に実際のファイルサイズを確認し、次の順序で処理します。

1. 縦横比を維持して段階的に縮小（横125px・縦100pxのどちらも下回りません）
2. 最小画像サイズでも512 KBを超えた場合、色数を256色から200色へ削減
3. まだ超える場合、フレームレートを半分に削減
4. それでも超える場合はGIFを出力せず、動画を短くするよう案内

入力動画は3～10秒かつ125×100px以上である必要があります。

## 必要環境

- Windows 10/11（64ビット）
- FFmpegの`ffmpeg.exe`と`ffprobe.exe`
- ビルドする場合のみVisual Studio 2022の「C++によるデスクトップ開発」

FFmpeg本体はこのリポジトリやビルド結果には含まれていないため、別途入手する必要があります。

1. [FFmpeg公式ダウンロードページのWindows欄](https://ffmpeg.org/download.html#build-windows)を開き、案内されているWindowsビルドのZIPをダウンロードします。
2. ZIPを任意の場所へ展開します（例: `C:\ffmpeg`）。
3. 展開したフォルダー内の`bin`を開き、`ffmpeg.exe`と`ffprobe.exe`があることを確認します（例: `C:\ffmpeg\bin\ffmpeg.exe`）。
4. アプリのFFmpeg欄にある「設定...」を押し、その`ffmpeg.exe`を選択します。「入手方法...」から公式ダウンロードページを開くこともできます。

選択した場所は次回起動用に保存されます。アプリ本体またはその下の`tools`フォルダー、環境変数`PATH`からの自動検出にも対応しています。

> `ffmpeg.exe`だけでは動作しません。必ず`ffprobe.exe`も同じフォルダーに配置してください。

## ビルド

Visual Studio 2022で`MP4toGifConvert.sln`を開き、構成を`Release | x64`にしてビルドします。または「Developer Command Prompt for VS 2022」で実行します。

```bat
msbuild MP4toGifConvert.sln /p:Configuration=Release /p:Platform=x64
```

出力は`src\MP4toGifConvert\x64\Release`です。`src\MP4toGifConvert\tools`に`ffmpeg.exe`と`ffprobe.exe`を配置してからビルドすると、出力の`tools`フォルダーへ自動コピーされます。FFmpeg本体はリポジトリには含まれません。

## 使い方

1. 必要ならFFmpeg欄の「設定...」から`ffmpeg.exe`を選択します。
2. 「選択...」からMP4ファイルを選びます。
3. 必要なら「変更...」でGIFの保存先を変更します。
4. 「GIFに変換」を押します。

変換処理はバックグラウンドで行われ、進捗を画面に表示します。成功した場合だけ保存先を上書きし、エクスプローラーで出力ファイルを表示します。

## 「FFmpegが見つかりません」と表示される場合

Visual Studioでこのプロジェクトをビルドしても、FFmpeg本体は自動ではインストールされません。「入手方法...」の案内に従ってダウンロードし、「設定...」から展開した`bin\ffmpeg.exe`を選択してください。`ffprobe.exe`が別のフォルダーにある場合は認識されないため、2つのexeが同じ`bin`フォルダーにある状態で選択します。
