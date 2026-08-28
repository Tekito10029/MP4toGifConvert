#include "GifConverter.h"
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

namespace fs = std::filesystem;
namespace {
constexpr int InputEdit=101, OutputEdit=102, SelectInput=103, SelectOutput=104, SelectFfmpeg=105, ConvertButton=106, StatusLabel=107, ToolsLabel=108, Progress=109;
constexpr UINT ProgressMessage=WM_APP+1, CompleteMessage=WM_APP+2;
HWND windowHandle{}, inputEdit{}, outputEdit{}, toolsLabel{}, statusLabel{}, convertButton{}, progressBar{};
fs::path inputPath, outputPath, toolsDirectory;
bool busy = false;

std::wstring FromUtf8(const std::string& text) {
    if (text.empty()) return {};
    int length=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring result(length,L'\0'); MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),length); return result;
}
void SetText(HWND control,const std::wstring& text){SetWindowTextW(control,text.c_str());}
void RefreshTools(){toolsDirectory=FfmpegRunner::ResolveToolsDirectory(toolsDirectory); SetText(toolsLabel,toolsDirectory.empty()?L"未設定":toolsDirectory.wstring());}

bool PickFile(bool save,const wchar_t* filter,fs::path& path,const wchar_t* title) {
    wchar_t buffer[32768]{}; if(!path.empty()) wcsncpy_s(buffer,path.c_str(),_TRUNCATE);
    OPENFILENAMEW dialog{sizeof(dialog)}; dialog.hwndOwner=windowHandle; dialog.lpstrFilter=filter; dialog.lpstrFile=buffer;
    dialog.nMaxFile=32768; dialog.lpstrTitle=title; dialog.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    dialog.lpstrDefExt=save?L"gif":nullptr;
    if((save?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog))==FALSE)return false; path=buffer; return true;
}
void ChooseFfmpeg(){
    fs::path selected=toolsDirectory.empty()?fs::path(L"ffmpeg.exe"):toolsDirectory/L"ffmpeg.exe";
    if(!PickFile(false,L"ffmpeg.exe\0ffmpeg.exe\0\0",selected,L"ffmpeg.exeを選択（ffprobe.exeも同じフォルダーに必要です）"))return;
    auto directory=selected.parent_path();
    if(!FfmpegRunner::HasTools(directory)){MessageBoxW(windowHandle,L"同じフォルダーにffprobe.exeがありません。2ファイルを同じフォルダーへ配置してください。",L"FFmpeg設定",MB_ICONERROR);return;}
    try{SaveToolsDirectory(directory);toolsDirectory=directory;RefreshTools();SetText(statusLabel,L"FFmpegを設定しました。");}
    catch(const std::exception& e){MessageBoxW(windowHandle,FromUtf8(e.what()).c_str(),L"エラー",MB_ICONERROR);}
}
void BeginConversion(){
    if(inputPath.empty()||outputPath.empty())return;
    if(FfmpegRunner::ResolveToolsDirectory(toolsDirectory).empty()){MessageBoxW(windowHandle,L"先に［FFmpeg設定...］からffmpeg.exeを選択してください。",L"FFmpegが見つかりません",MB_ICONWARNING);ChooseFfmpeg();return;}
    busy=true;EnableWindow(convertButton,FALSE);SendMessageW(progressBar,PBM_SETMARQUEE,TRUE,30);
    auto input=inputPath, output=outputPath, tools=toolsDirectory; HWND target=windowHandle;
    std::thread([input,output,tools,target]{
        auto result=std::make_unique<ConversionResult>();
        try{GifConverter converter{FfmpegRunner{tools}};*result=converter.Convert(input,output,[target](const std::wstring& message){PostMessageW(target,ProgressMessage,0,reinterpret_cast<LPARAM>(new std::wstring(message)));});}
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
        Add(L"STATIC",L"FFmpeg",0,18,102,80,24);toolsLabel=Add(L"STATIC",L"未設定",SS_PATHELLIPSIS,100,102,475,24,ToolsLabel);Add(L"BUTTON",L"FFmpeg設定...",0,585,98,100,28,SelectFfmpeg);
        Add(L"STATIC",L"上限: 512 KB　最小画像サイズ: 横125px・縦100px",0,100,140,480,24);
        progressBar=Add(PROGRESS_CLASSW,L"",PBS_MARQUEE,18,172,667,18,Progress);statusLabel=Add(L"STATIC",L"3～10秒のMP4ファイルを選択してください。",0,18,204,550,40,StatusLabel);
        convertButton=Add(L"BUTTON",L"GIFに変換",BS_DEFPUSHBUTTON,565,218,120,32,ConvertButton);EnableWindow(convertButton,FALSE);RefreshTools();return 0;
    case WM_COMMAND:
        switch(LOWORD(wParam)){
        case SelectInput: if(PickFile(false,L"MP4動画 (*.mp4)\0*.mp4\0\0",inputPath,L"MP4ファイルを選択")){outputPath=inputPath.parent_path()/(inputPath.stem().wstring()+L".gif");SetText(inputEdit,inputPath.wstring());SetText(outputEdit,outputPath.wstring());EnableWindow(convertButton,TRUE);}break;
        case SelectOutput: if(PickFile(true,L"GIF画像 (*.gif)\0*.gif\0\0",outputPath,L"GIFの保存先"))SetText(outputEdit,outputPath.wstring());break;
        case SelectFfmpeg: ChooseFfmpeg();break; case ConvertButton: BeginConversion();break;
        }return 0;
    case ProgressMessage:{std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));SetText(statusLabel,*text);return 0;}
    case CompleteMessage:{std::unique_ptr<ConversionResult> result(reinterpret_cast<ConversionResult*>(lParam));busy=false;EnableWindow(convertButton,TRUE);SendMessageW(progressBar,PBM_SETMARQUEE,FALSE,0);SetText(statusLabel,result->message);
        MessageBoxW(hwnd,result->message.c_str(),result->success?L"変換完了":L"変換できませんでした",result->success?MB_ICONINFORMATION:MB_ICONWARNING);
        if(result->success){std::wstring args=L"/select,\""+result->output.wstring()+L"\"";ShellExecuteW(hwnd,L"open",L"explorer.exe",args.c_str(),nullptr,SW_SHOW);}return 0;}
    case WM_CLOSE: if(busy){MessageBoxW(hwnd,L"変換が完了するまでお待ちください。",L"変換中",MB_ICONINFORMATION);return 0;}DestroyWindow(hwnd);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }return DefWindowProcW(hwnd,message,wParam,lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_PROGRESS_CLASS};InitCommonControlsEx(&controls);
    WNDCLASSEXW type{sizeof(type)};type.lpfnWndProc=WindowProc;type.hInstance=instance;type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.hIcon=LoadIconW(nullptr,IDI_APPLICATION);type.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);type.lpszClassName=L"MP4toGifConvertWindow";RegisterClassExW(&type);
    HWND hwnd=CreateWindowExW(0,type.lpszClassName,L"MP4 → GIF（512 KB以下）",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,720,300,nullptr,nullptr,instance,nullptr);
    ShowWindow(hwnd,show);UpdateWindow(hwnd);MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}return static_cast<int>(message.wParam);
}
