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
#include <sstream>
#include <numeric>
#include <iterator>
#include <cstring>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace {
void Check(HRESULT result, const char* message) {
    if (SUCCEEDED(result)) return;
    std::ostringstream details;
    details << message << " (HRESULT: 0x" << std::hex << std::uppercase << static_cast<unsigned long>(result) << ")";
    throw std::runtime_error(details.str());
}
struct Runtime {
    bool comStarted=false, mediaFoundationStarted=false;
    Runtime() {
        Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Windows画像処理を初期化できませんでした。"); comStarted=true;
        HRESULT result=MFStartup(MF_VERSION);
        if (FAILED(result)) { CoUninitialize(); comStarted=false; Check(result, "Media Foundationを初期化できませんでした。"); }
        mediaFoundationStarted=true;
    }
    ~Runtime() { if (mediaFoundationStarted) MFShutdown(); if (comStarted) CoUninitialize(); }
};
ComPtr<IMFSourceReader> OpenReader(const fs::path& input, UINT32& width, UINT32& height, double& fps, double& duration) {
    ComPtr<IMFAttributes> attributes;
    Check(MFCreateAttributes(&attributes, 3), "動画デコーダー設定を作成できませんでした。");
    Check(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE), "動画の色変換を有効にできませんでした。");
    Check(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE), "動画のハードウェアデコードを有効にできませんでした。");
    ComPtr<IMFSourceReader> reader;
    Check(MFCreateSourceReaderFromURL(input.c_str(), attributes.Get(), &reader), "MP4ファイルを開けませんでした。");
    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    Check(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE), "MP4に映像ストリームがありません。");
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

void SetInfiniteLoop(IWICBitmapEncoder* encoder) {
    ComPtr<IWICMetadataQueryWriter> metadata;
    Check(encoder->GetMetadataQueryWriter(&metadata), "GIFのループ設定を作成できませんでした。");

    // Netscape Application Extension. A loop count of zero means forever.
    char application[]="NETSCAPE2.0";
    PROPVARIANT value; PropVariantInit(&value);
    value.vt=VT_VECTOR|VT_UI1; value.caub.cElems=static_cast<ULONG>(std::size(application)-1); value.caub.pElems=reinterpret_cast<BYTE*>(application);
    Check(metadata->SetMetadataByName(L"/appext/application",&value), "GIFのループ識別子を設定できませんでした。");

    BYTE loopData[]={3,1,0,0,0};
    value.caub.cElems=static_cast<ULONG>(std::size(loopData)); value.caub.pElems=loopData;
    Check(metadata->SetMetadataByName(L"/appext/data",&value), "GIFの無限ループを設定できませんでした。");
}

ComPtr<IWICBitmap> CreateBitmapFromSample(IMFSample* sample, IWICImagingFactory* factory,
    UINT32 width, UINT32 height) {
    ComPtr<IMFMediaBuffer> mediaBuffer;
    Check(sample->ConvertToContiguousBuffer(&mediaBuffer), "動画フレームを変換できませんでした。");

    const DWORD rowBytes=width*4;
    const DWORD requiredBytes=rowBytes*height;
    std::vector<BYTE> pixels(requiredBytes);
    ComPtr<IMF2DBuffer> buffer2d;
    if (SUCCEEDED(mediaBuffer.As(&buffer2d))) {
        // Media Foundation frames commonly have a padded or negative stride.
        // scanline0 is the display's first row, so signed pitch also fixes
        // bottom-up RGB32 buffers while copying into WIC's top-down layout.
        BYTE* scanline0=nullptr; LONG pitch=0;
        Check(buffer2d->Lock2D(&scanline0,&pitch), "動画フレームの行間隔を読み取れませんでした。");
        if (scanline0==nullptr || std::abs(pitch)<static_cast<LONG>(rowBytes)) {
            buffer2d->Unlock2D();
            throw std::runtime_error("動画フレームの行間隔が不正です。");
        }
        for (UINT32 row=0;row<height;++row)
            std::memcpy(pixels.data()+static_cast<size_t>(row)*rowBytes,
                scanline0+static_cast<ptrdiff_t>(row)*pitch,rowBytes);
        buffer2d->Unlock2D();
    } else {
        BYTE* source=nullptr; DWORD length=0;
        Check(mediaBuffer->Lock(&source,nullptr,&length), "動画フレームを読み込めませんでした。");
        if (length<requiredBytes) { mediaBuffer->Unlock(); throw std::runtime_error("動画フレームのデータサイズが不足しています。"); }
        std::memcpy(pixels.data(),source,requiredBytes);
        mediaBuffer->Unlock();
    }

    ComPtr<IWICBitmap> bitmap;
    Check(factory->CreateBitmapFromMemory(width,height,GUID_WICPixelFormat32bppBGR,rowBytes,
        requiredBytes,pixels.data(),&bitmap), "動画フレームを画像に変換できませんでした。");
    return bitmap;
}

ComPtr<IWICPalette> BuildGlobalPalette(IMFSourceReader* reader, IWICImagingFactory* factory,
    UINT32 sourceWidth, UINT32 sourceHeight, const Attempt& attempt) {
    // A single palette for the whole animation avoids writing a 768-byte local
    // color table for every frame and gives LZW stable color indices.
    std::vector<unsigned long long> histogram(32 * 32 * 32);
    LONGLONG nextFrame=0;
    const LONGLONG interval=static_cast<LONGLONG>(10000000.0/attempt.fps);
    for (;;) {
        DWORD streamIndex=0,flags=0; LONGLONG timestamp=0; ComPtr<IMFSample> sample;
        Check(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,&streamIndex,&flags,&timestamp,&sample),"動画の色を解析できませんでした。");
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample || timestamp < nextFrame) continue; nextFrame=timestamp+interval;
        ComPtr<IWICBitmap> bitmap=CreateBitmapFromSample(sample.Get(),factory,sourceWidth,sourceHeight);
        ComPtr<IWICBitmapScaler> scaler; Check(factory->CreateBitmapScaler(&scaler),"画像縮小処理を作成できませんでした。");
        Check(scaler->Initialize(bitmap.Get(),attempt.width,attempt.height,WICBitmapInterpolationModeFant),"画像を縮小できませんでした。");
        std::vector<BYTE> pixels(static_cast<size_t>(attempt.width)*attempt.height*4);
        Check(scaler->CopyPixels(nullptr,attempt.width*4,static_cast<UINT>(pixels.size()),pixels.data()),"画像の色を解析できませんでした。");
        for (size_t offset=0;offset+3<pixels.size();offset+=4) {
            unsigned index=(pixels[offset]>>3)|((pixels[offset+1]>>3)<<5)|((pixels[offset+2]>>3)<<10);
            ++histogram[index];
        }
    }
    std::vector<unsigned> indices(histogram.size()); std::iota(indices.begin(),indices.end(),0);
    const size_t colorCount=static_cast<size_t>(std::clamp(attempt.colors,2,256));
    std::partial_sort(indices.begin(),indices.begin()+colorCount,indices.end(),[&](unsigned a,unsigned b){return histogram[a]>histogram[b];});
    std::vector<WICColor> colors; colors.reserve(colorCount);
    for (size_t i=0;i<colorCount;++i) {
        unsigned value=indices[i]; BYTE b=static_cast<BYTE>(((value&31)<<3)|4); BYTE g=static_cast<BYTE>((((value>>5)&31)<<3)|4); BYTE r=static_cast<BYTE>((((value>>10)&31)<<3)|4);
        colors.push_back(0xFF000000u|(static_cast<WICColor>(r)<<16)|(static_cast<WICColor>(g)<<8)|b);
    }
    ComPtr<IWICPalette> palette; Check(factory->CreatePalette(&palette),"GIFパレットを作成できませんでした。");
    Check(palette->InitializeCustom(colors.data(),static_cast<UINT>(colors.size())),"共通GIFパレットを作成できませんでした。");
    return palette;
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
    ComPtr<IWICPalette> palette=BuildGlobalPalette(reader.Get(),factory.Get(),sourceWidth,sourceHeight,attempt);
    reader=OpenReader(input,sourceWidth,sourceHeight,sourceFps,duration);
    ComPtr<IWICStream> stream; Check(factory->CreateStream(&stream),"GIF出力を作成できませんでした。");
    Check(stream->InitializeFromFilename(output.c_str(),GENERIC_WRITE),"GIFファイルを作成できませんでした。");
    ComPtr<IWICBitmapEncoder> encoder; Check(factory->CreateEncoder(GUID_ContainerFormatGif,nullptr,&encoder),"GIFエンコーダーを作成できませんでした。");
    Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"GIFエンコーダーを初期化できませんでした。");
    SetInfiniteLoop(encoder.Get());
    Check(encoder->SetPalette(palette.Get()),"GIFの共通パレットを設定できませんでした。");
    LONGLONG nextFrame=0; const LONGLONG interval=static_cast<LONGLONG>(10000000.0/attempt.fps); bool wrote=false;
    for (;;) {
        DWORD streamIndex=0,flags=0; LONGLONG timestamp=0; ComPtr<IMFSample> sample;
        Check(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,&streamIndex,&flags,&timestamp,&sample),"動画フレームを読み取れませんでした。");
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample || timestamp < nextFrame) continue; nextFrame=timestamp+interval;
        ComPtr<IWICBitmap> bitmap=CreateBitmapFromSample(sample.Get(),factory.Get(),sourceWidth,sourceHeight);
        ComPtr<IWICBitmapScaler> scaler; Check(factory->CreateBitmapScaler(&scaler),"画像縮小処理を作成できませんでした。");
        Check(scaler->Initialize(bitmap.Get(),attempt.width,attempt.height,WICBitmapInterpolationModeFant),"画像を縮小できませんでした。");
        ComPtr<IWICFormatConverter> indexed; Check(factory->CreateFormatConverter(&indexed),"GIF色変換を作成できませんでした。");
        Check(indexed->Initialize(scaler.Get(),GUID_WICPixelFormat8bppIndexed,WICBitmapDitherTypeNone,palette.Get(),0,WICBitmapPaletteTypeCustom),"GIFの色数を削減できませんでした。");
        ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> properties;
        Check(encoder->CreateNewFrame(&frame,&properties),"GIFフレームを作成できませんでした。"); Check(frame->Initialize(properties.Get()),"GIFフレームを初期化できませんでした。");
        Check(frame->SetSize(attempt.width,attempt.height),"GIFサイズを設定できませんでした。"); WICPixelFormatGUID format=GUID_WICPixelFormat8bppIndexed; Check(frame->SetPixelFormat(&format),"GIFピクセル形式を設定できませんでした。");
        SetDelay(frame.Get(),attempt.fps);
        Check(frame->WriteSource(indexed.Get(),nullptr),"GIFフレームを書き込めませんでした。"); Check(frame->Commit(),"GIFフレームを保存できませんでした。"); wrote=true;
    }
    if (!wrote) throw std::runtime_error("動画からフレームを取得できませんでした。");
    Check(encoder->Commit(),"GIFを保存できませんでした。"); Check(stream->Commit(STGC_DEFAULT),"GIFファイルを保存できませんでした。");
}
