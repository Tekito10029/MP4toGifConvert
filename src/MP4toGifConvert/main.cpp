#include "GifConverter.h"
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <algorithm>
#include <stdexcept>
#include <cwchar>
#include <cstring>

namespace fs = std::filesystem;
namespace {
constexpr int InputEdit=101, OutputEdit=102, SelectInput=103, SelectOutput=104, ConvertButton=106, StatusLabel=107, Progress=109, StartField=110, EndField=111, WidthField=112, HeightField=113, ColorsField=114, FpsField=115, PreviewControl=116;
constexpr UINT_PTR PreviewTimer=1;
constexpr UINT ProgressMessage=WM_APP+1, CompleteMessage=WM_APP+2;
HWND windowHandle{}, inputEdit{}, outputEdit{}, startEdit{}, endEdit{}, widthEdit{}, heightEdit{}, colorsEdit{}, fpsEdit{}, previewControl{}, statusLabel{}, convertButton{}, progressBar{};
HBITMAP previewBitmap{};
fs::path inputPath, outputPath;
bool busy = false;

std::wstring FromUtf8(const std::string& text) {
    if (text.empty()) return {};
    int length=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring result(length,L'\0'); MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),length); return result;
}
void SetText(HWND control,const std::wstring& text){SetWindowTextW(control,text.c_str());}
double ReadNumber(HWND control,const wchar_t* name) {
    wchar_t text[64]{}; GetWindowTextW(control,text,64); wchar_t* end=nullptr; double value=wcstod(text,&end);
    if (end==text || *end!=L'\0') throw std::runtime_error("編集項目には数値を入力してください。");
    (void)name;
    return value;
}
EditOptions ReadOptions() {
    return {ReadNumber(startEdit,L"開始秒"),ReadNumber(endEdit,L"終了秒"),
        static_cast<int>(ReadNumber(widthEdit,L"幅")),static_cast<int>(ReadNumber(heightEdit,L"高さ")),
        static_cast<int>(ReadNumber(colorsEdit,L"色数")),ReadNumber(fpsEdit,L"FPS")};
}
void UpdatePreview() {
    if (inputPath.empty()) return;
    try {
        EditOptions options=ReadOptions();
        PreviewImage preview=WindowsMedia{}.CreatePreview(inputPath,options.startSeconds,options.width,options.height,315,178);
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=preview.width;
        info.bmiHeader.biHeight=-preview.height; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
        void* destination=nullptr; HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&destination,nullptr,0);
        if (!bitmap || !destination) throw std::runtime_error("プレビュー表示を作成できませんでした。");
        std::memcpy(destination,preview.pixels.data(),preview.pixels.size());
        HBITMAP old=reinterpret_cast<HBITMAP>(SendMessageW(previewControl,STM_SETIMAGE,IMAGE_BITMAP,reinterpret_cast<LPARAM>(bitmap)));
        if (old) DeleteObject(old); previewBitmap=bitmap; SetText(statusLabel,L"プレビューを更新しました。");
    } catch (const std::exception&) { SetText(statusLabel,L"編集値を確定するとプレビューを更新します。"); }
}
bool PickFile(bool save,const wchar_t* filter,fs::path& path,const wchar_t* title) {
    wchar_t buffer[32768]{}; if(!path.empty()) wcsncpy_s(buffer,path.c_str(),_TRUNCATE);
    OPENFILENAMEW dialog{sizeof(dialog)}; dialog.hwndOwner=windowHandle; dialog.lpstrFilter=filter; dialog.lpstrFile=buffer;
    dialog.nMaxFile=32768; dialog.lpstrTitle=title; dialog.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    dialog.lpstrDefExt=save?L"gif":nullptr;
    if((save?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog))==FALSE)return false; path=buffer; return true;
}
void BeginConversion(){
    if(inputPath.empty()||outputPath.empty())return;
    EditOptions options;
    try { options=ReadOptions(); }
    catch(const std::exception& e) { MessageBoxW(windowHandle,FromUtf8(e.what()).c_str(),L"入力エラー",MB_ICONERROR); return; }
    busy=true;EnableWindow(convertButton,FALSE);SendMessageW(progressBar,PBM_SETMARQUEE,TRUE,30);
    auto input=inputPath, output=outputPath; HWND target=windowHandle;
    std::thread([input,output,options,target]{
        auto result=std::make_unique<ConversionResult>();
        try{GifConverter converter;*result=converter.Convert(input,output,options,[target](const std::wstring& message){PostMessageW(target,ProgressMessage,0,reinterpret_cast<LPARAM>(new std::wstring(message)));});}
        catch(const std::exception& e){result->message=FromUtf8(e.what());}
        PostMessageW(target,CompleteMessage,0,reinterpret_cast<LPARAM>(result.release()));
    }).detach();
}
HWND Add(const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int width,int height,int id=0){return CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,x,y,width,height,windowHandle,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);}

LRESULT CALLBACK WindowProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam){
    switch(message){
    case WM_CREATE:
        windowHandle=hwnd; Add(L"STATIC",L"入力 MP4",0,18,22,80,24); inputEdit=Add(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|ES_READONLY,100,18,475,26,InputEdit);Add(L"BUTTON",L"選択...",0,585,18,100,28,SelectInput);
        Add(L"STATIC",L"出力 GIF",0,18,62,80,24);outputEdit=Add(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|ES_READONLY,100,58,475,26,OutputEdit);Add(L"BUTTON",L"変更...",0,585,58,100,28,SelectOutput);
        Add(L"STATIC",L"切り出し",0,18,108,70,24);Add(L"STATIC",L"開始(秒)",0,90,108,65,24);startEdit=Add(L"EDIT",L"0",WS_BORDER|ES_AUTOHSCROLL,155,104,70,26,StartField);Add(L"STATIC",L"終了(秒)",0,235,108,65,24);endEdit=Add(L"EDIT",L"10",WS_BORDER|ES_AUTOHSCROLL,300,104,60,26,EndField);
        Add(L"STATIC",L"サイズ",0,18,148,70,24);Add(L"STATIC",L"幅",0,90,148,30,24);widthEdit=Add(L"EDIT",L"320",WS_BORDER|ES_AUTOHSCROLL,120,144,70,26,WidthField);Add(L"STATIC",L"高さ",0,205,148,40,24);heightEdit=Add(L"EDIT",L"240",WS_BORDER|ES_AUTOHSCROLL,245,144,70,26,HeightField);
        Add(L"STATIC",L"画質",0,18,188,70,24);Add(L"STATIC",L"色数",0,90,188,40,24);colorsEdit=Add(L"EDIT",L"200",WS_BORDER|ES_AUTOHSCROLL,130,184,70,26,ColorsField);Add(L"STATIC",L"FPS",0,215,188,35,24);fpsEdit=Add(L"EDIT",L"15",WS_BORDER|ES_AUTOHSCROLL,250,184,65,26,FpsField);
        Add(L"STATIC",L"プレビュー（開始位置）",0,370,102,200,22);previewControl=Add(L"STATIC",L"MP4を選択してください",SS_BITMAP|SS_CENTERIMAGE|WS_BORDER,370,126,315,178,PreviewControl);
        Add(L"STATIC",L"設定値で1回だけ書き出します（自動圧縮なし）",0,18,224,330,24);
        progressBar=Add(PROGRESS_CLASSW,L"",PBS_MARQUEE,18,318,667,18,Progress);statusLabel=Add(L"STATIC",L"MP4を選択して編集内容を指定してください。",0,18,350,520,40,StatusLabel);
        convertButton=Add(L"BUTTON",L"GIFを書き出す",BS_DEFPUSHBUTTON,545,378,140,32,ConvertButton);EnableWindow(convertButton,FALSE);return 0;
    case WM_COMMAND:
        switch(LOWORD(wParam)){
        case SelectInput: if(PickFile(false,L"MP4動画 (*.mp4)\0*.mp4\0\0",inputPath,L"MP4ファイルを選択")){outputPath=inputPath.parent_path()/(inputPath.stem().wstring()+L".gif");SetText(inputEdit,inputPath.wstring());SetText(outputEdit,outputPath.wstring());try{VideoInfo info=WindowsMedia{}.Probe(inputPath);SetText(endEdit,std::to_wstring(info.duration));SetText(widthEdit,std::to_wstring(info.width));SetText(heightEdit,std::to_wstring(info.height));SetText(fpsEdit,std::to_wstring(std::min(info.fps,30.0)));EnableWindow(convertButton,TRUE);UpdatePreview();}catch(const std::exception& e){MessageBoxW(hwnd,FromUtf8(e.what()).c_str(),L"動画エラー",MB_ICONERROR);}}break;
        case SelectOutput: if(PickFile(true,L"GIF画像 (*.gif)\0*.gif\0\0",outputPath,L"GIFの保存先"))SetText(outputEdit,outputPath.wstring());break;
        case ConvertButton: BeginConversion();break;
        case StartField: case EndField: case WidthField: case HeightField: case ColorsField: case FpsField:
            if (HIWORD(wParam)==EN_CHANGE && !inputPath.empty()) SetTimer(hwnd,PreviewTimer,450,nullptr); break;
        }return 0;
    case WM_TIMER: if(wParam==PreviewTimer){KillTimer(hwnd,PreviewTimer);UpdatePreview();}return 0;
    case ProgressMessage:{std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));SetText(statusLabel,*text);return 0;}
    case CompleteMessage:{std::unique_ptr<ConversionResult> result(reinterpret_cast<ConversionResult*>(lParam));busy=false;EnableWindow(convertButton,TRUE);SendMessageW(progressBar,PBM_SETMARQUEE,FALSE,0);std::wstring display=result->message;if(result->success)display+=L"\nファイルサイズ: "+std::to_wstring(result->bytes/1024)+L" KB";SetText(statusLabel,display);
        MessageBoxW(hwnd,display.c_str(),result->success?L"書き出し完了":L"書き出しできませんでした",result->success?MB_ICONINFORMATION:MB_ICONWARNING);
        if(result->success){std::wstring args=L"/select,\""+result->output.wstring()+L"\"";ShellExecuteW(hwnd,L"open",L"explorer.exe",args.c_str(),nullptr,SW_SHOW);}return 0;}
    case WM_CLOSE: if(busy){MessageBoxW(hwnd,L"変換が完了するまでお待ちください。",L"変換中",MB_ICONINFORMATION);return 0;}DestroyWindow(hwnd);return 0;
    case WM_DESTROY:if(previewBitmap)DeleteObject(previewBitmap);PostQuitMessage(0);return 0;
    }return DefWindowProcW(hwnd,message,wParam,lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_PROGRESS_CLASS};InitCommonControlsEx(&controls);
    WNDCLASSEXW type{sizeof(type)};type.lpfnWndProc=WindowProc;type.hInstance=instance;type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.hIcon=LoadIconW(nullptr,IDI_APPLICATION);type.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);type.lpszClassName=L"MP4toGifConvertWindow";RegisterClassExW(&type);
    HWND hwnd=CreateWindowExW(0,type.lpszClassName,L"MP4 → GIF エディター",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,720,470,nullptr,nullptr,instance,nullptr);
    ShowWindow(hwnd,show);UpdateWindow(hwnd);MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}return static_cast<int>(message.wParam);
}
