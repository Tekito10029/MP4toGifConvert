#include "WindowsMedia.h"
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <propvarutil.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace {
void Check(HRESULT result, const char* message) { if (FAILED(result)) throw std::runtime_error(message); }
struct Runtime {
    Runtime() { Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Windows画像処理を初期化できませんでした。"); Check(MFStartup(MF_VERSION), "Media Foundationを初期化できませんでした。"); }
    ~Runtime() { MFShutdown(); CoUninitialize(); }
};
ComPtr<IMFSourceReader> OpenReader(const fs::path& input, UINT32& width, UINT32& height, double& fps, double& duration) {
    ComPtr<IMFSourceReader> reader;
    Check(MFCreateSourceReaderFromURL(input.c_str(), nullptr, &reader), "MP4ファイルを開けませんでした。");
    ComPtr<IMFMediaType> type;
    Check(MFCreateMediaType(&type), "動画形式を作成できませんでした。");
    Check(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "動画形式を設定できませんでした。");
    Check(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32), "動画をRGBへ変換できませんでした。");
    Check(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type.Get()), "このMP4の映像形式をWindowsでデコードできませんでした。");
    ComPtr<IMFMediaType> actual;
    Check(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actual), "動画情報を取得できませんでした。");
    Check(MFGetAttributeSize(actual.Get(), MF_MT_FRAME_SIZE, &width, &height), "動画サイズを取得できませんでした。");
    UINT32 numerator=0, denominator=1;
    if (FAILED(MFGetAttributeRatio(actual.Get(), MF_MT_FRAME_RATE, &numerator, &denominator)) || denominator==0) fps=10; else fps=static_cast<double>(numerator)/denominator;
    PROPVARIANT value; PropVariantInit(&value);
    Check(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &value), "動画時間を取得できませんでした。");
    duration=static_cast<double>(value.uhVal.QuadPart)/10000000.0; PropVariantClear(&value);
    return reader;
}
void SetDelay(IWICBitmapFrameEncode* frame, double fps) {
    ComPtr<IWICMetadataQueryWriter> metadata;
    if (FAILED(frame->GetMetadataQueryWriter(&metadata))) return;
    PROPVARIANT value; PropVariantInit(&value); value.vt=VT_UI2; value.uiVal=static_cast<USHORT>(std::max(1.0, std::round(100.0/fps)));
    metadata->SetMetadataByName(L"/grctlext/Delay", &value);
    value.uiVal=2; metadata->SetMetadataByName(L"/grctlext/Disposal", &value); PropVariantClear(&value);
}
}

VideoInfo WindowsMedia::Probe(const fs::path& input) const {
    Runtime runtime; UINT32 width=0,height=0; double fps=0,duration=0; OpenReader(input,width,height,fps,duration);
    return {duration,static_cast<int>(width),static_cast<int>(height),fps};
}

void WindowsMedia::CreateGif(const fs::path& input, const fs::path& output, const Attempt& attempt) const {
    Runtime runtime; UINT32 sourceWidth=0,sourceHeight=0; double sourceFps=0,duration=0;
    ComPtr<IMFSourceReader> reader=OpenReader(input,sourceWidth,sourceHeight,sourceFps,duration);
    ComPtr<IWICImagingFactory> factory; Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Windows画像エンコーダーを作成できませんでした。");
    ComPtr<IWICStream> stream; Check(factory->CreateStream(&stream),"GIF出力を作成できませんでした。");
    Check(stream->InitializeFromFilename(output.c_str(),GENERIC_WRITE),"GIFファイルを作成できませんでした。");
    ComPtr<IWICBitmapEncoder> encoder; Check(factory->CreateEncoder(GUID_ContainerFormatGif,nullptr,&encoder),"GIFエンコーダーを作成できませんでした。");
    Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"GIFエンコーダーを初期化できませんでした。");
    LONGLONG nextFrame=0; const LONGLONG interval=static_cast<LONGLONG>(10000000.0/attempt.fps); bool wrote=false;
    for (;;) {
        DWORD streamIndex=0,flags=0; LONGLONG timestamp=0; ComPtr<IMFSample> sample;
        Check(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,&streamIndex,&flags,&timestamp,&sample),"動画フレームを読み取れませんでした。");
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample || timestamp < nextFrame) continue; nextFrame=timestamp+interval;
        ComPtr<IMFMediaBuffer> mediaBuffer; Check(sample->ConvertToContiguousBuffer(&mediaBuffer),"動画フレームを変換できませんでした。");
        BYTE* bytes=nullptr; DWORD length=0; Check(mediaBuffer->Lock(&bytes,nullptr,&length),"動画フレームを読み込めませんでした。");
        ComPtr<IWICBitmap> bitmap; HRESULT bitmapResult=factory->CreateBitmapFromMemory(sourceWidth,sourceHeight,GUID_WICPixelFormat32bppBGR,sourceWidth*4,length,bytes,&bitmap); mediaBuffer->Unlock(); Check(bitmapResult,"動画フレームを画像に変換できませんでした。");
        ComPtr<IWICBitmapScaler> scaler; Check(factory->CreateBitmapScaler(&scaler),"画像縮小処理を作成できませんでした。");
        Check(scaler->Initialize(bitmap.Get(),attempt.width,attempt.height,WICBitmapInterpolationModeFant),"画像を縮小できませんでした。");
        ComPtr<IWICPalette> palette; Check(factory->CreatePalette(&palette),"GIFパレットを作成できませんでした。");
        Check(palette->InitializeFromBitmap(scaler.Get(),attempt.colors,FALSE),"GIFの色数を削減できませんでした。");
        ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> properties;
        Check(encoder->CreateNewFrame(&frame,&properties),"GIFフレームを作成できませんでした。"); Check(frame->Initialize(properties.Get()),"GIFフレームを初期化できませんでした。");
        Check(frame->SetSize(attempt.width,attempt.height),"GIFサイズを設定できませんでした。"); WICPixelFormatGUID format=GUID_WICPixelFormat8bppIndexed; Check(frame->SetPixelFormat(&format),"GIFピクセル形式を設定できませんでした。");
        Check(frame->SetPalette(palette.Get()),"GIFパレットを設定できませんでした。"); SetDelay(frame.Get(),attempt.fps);
        Check(frame->WriteSource(scaler.Get(),nullptr),"GIFフレームを書き込めませんでした。"); Check(frame->Commit(),"GIFフレームを保存できませんでした。"); wrote=true;
    }
    if (!wrote) throw std::runtime_error("動画からフレームを取得できませんでした。");
    Check(encoder->Commit(),"GIFを保存できませんでした。"); Check(stream->Commit(STGC_DEFAULT),"GIFファイルを保存できませんでした。");
}
