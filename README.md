# MP4 to GIF Converter（C++版）

10秒以下のMP4動画を**512 KB以下**のGIFへ変換する、ネイティブWindowsデスクトップアプリです。動画の最低秒数に制限はありません。C++17、Windows Media Foundation、Windows Imaging Componentで実装しており、FFmpegや.NETランタイムは不要です。

## 変換手順

各試行後に実際のファイルサイズを確認し、次の順序で処理します。

1. 縦横比を維持して段階的に縮小（横100px・縦100pxのどちらも下回りません）
2. 最小画像サイズでも512 KBを超えた場合、色数を256色から200色へ削減
3. まだ超える場合、フレームレートを半分に削減
4. それでも超える場合はGIFを出力せず、動画を短くするよう案内

入力動画は10秒以下かつ100×100px以上である必要があります。最低秒数の制限はないため、3秒未満の動画も変換できます。

アニメーション全体から共通カラーパレットを作成して全フレームで共有します。フレームごとのカラーテーブルと高エントロピーなディザリングを避けることで、同じ解像度・フレーム数でもGIFを圧縮しやすくしています。

## 必要環境

- Windows 10/11（64ビット）
- ビルドする場合のみVisual Studio 2022の「C++によるデスクトップ開発」

MP4の読み込みにはWindows標準のMedia Foundation、GIFの縮小・減色・保存にはWindows Imaging Componentを使用します。追加の実行ファイルをダウンロードしたり、場所を設定したりする必要はありません。

## ビルド

Visual Studio 2022で`MP4toGifConvert.sln`を開き、構成を`Release | x64`にしてビルドします。または「Developer Command Prompt for VS 2022」で実行します。

```bat
msbuild MP4toGifConvert.sln /p:Configuration=Release /p:Platform=x64
```

出力は`src\MP4toGifConvert\x64\Release`です。生成された`MP4toGifConvert.exe`単体で起動できます。

## 使い方

1. 「選択...」からMP4ファイルを選びます。
2. 必要なら「変更...」でGIFの保存先を変更します。
3. 「GIFに変換」を押します。

変換処理はバックグラウンドで行われ、進捗を画面に表示します。成功した場合だけ保存先を上書きし、エクスプローラーで出力ファイルを表示します。

出力GIFにはNetscape Application Extensionの無限ループ設定を付与するため、対応するブラウザーや画像ビューアーでは繰り返し再生されます。

## 対応動画について

Windows Media FoundationがデコードできるMP4に対応します。アプリはMedia Foundationの動画色変換とハードウェアデコーダーを有効化するため、一般的なH.264形式をRGBへ変換できます。

それでも「映像形式をWindowsでデコードできませんでした」と表示される場合は、エラー末尾の`HRESULT`を確認してください。HEVC/H.265、AV1など、Windows環境にデコーダーが導入されていない形式では、Microsoft Storeから対応する映像拡張機能をインストールするか、動画をH.264形式で書き出し直す必要があります。FFmpegをアプリに同梱・呼び出すことはありません。

Media Foundationが返すRGBフレームには、行末の余白や負方向の行間隔が含まれる場合があります。本アプリは`IMF2DBuffer`の行間隔を使って各行を正規化してからWICへ渡すため、斜めの線や画像の崩れを防止します。
