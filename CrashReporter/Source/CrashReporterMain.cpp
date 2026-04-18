#include <Core/Crash/CrashHandling.h>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <Windows.h>
#include <Richedit.h>
#include <shellapi.h>

#include <filesystem>
#include <sstream>
#include <string>

namespace
{

constexpr int kTitleTextId = 1001;
constexpr int kGlyphTextId = 1002;
constexpr int kSubtitleTextId = 1003;
constexpr int kStatusTextId = 1004;
constexpr int kTextEditId = 1005;
constexpr int kOpenFolderButtonId = 1006;
constexpr int kOpenLogButtonId = 1007;
constexpr int kRelaunchButtonId = 1008;
constexpr int kCloseButtonId = 1009;

constexpr int kHeaderHeight = 120;
constexpr COLORREF kHeaderColor = RGB(31, 37, 46);
constexpr COLORREF kBodyColor = RGB(242, 245, 249);
constexpr COLORREF kAccentColor = RGB(224, 122, 78);
constexpr COLORREF kTitleColor = RGB(245, 247, 250);
constexpr COLORREF kSubtitleColor = RGB(186, 194, 204);
constexpr COLORREF kStatusColor = RGB(255, 210, 192);
constexpr COLORREF kBodyTextColor = RGB(40, 46, 54);
constexpr COLORREF kEditBackground = RGB(252, 253, 255);
constexpr wchar_t kWarningGlyph[] = L"\x26A0";

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty())
        return {};

    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty())
        return {};

    const int size =
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring BuildDisplayText(const Dot::CrashMetadata& metadata, const std::filesystem::path& metadataPath)
{
    std::wostringstream stream;
    stream << L"Application: " << Utf8ToWide(metadata.appName) << L"\r\n";
    stream << L"Time: " << Utf8ToWide(metadata.timestampUtc) << L"\r\n";
    stream << L"Build: " << Utf8ToWide(metadata.buildConfig) << L"\r\n";
    stream << L"Exception: " << Utf8ToWide(metadata.exceptionName) << L" (" << Utf8ToWide(metadata.exceptionCode)
           << L")\r\n";
    stream << L"Address: " << Utf8ToWide(metadata.exceptionAddress) << L"\r\n";
    if (!metadata.accessType.empty())
        stream << L"Access: " << Utf8ToWide(metadata.accessType) << L" @ " << Utf8ToWide(metadata.accessAddress)
               << L"\r\n";
    if (!metadata.projectPath.empty())
        stream << L"Project: " << Utf8ToWide(metadata.projectPath) << L"\r\n";
    if (!metadata.scenePath.empty())
        stream << L"Scene: " << Utf8ToWide(metadata.scenePath) << L"\r\n";
    stream << L"Crash Folder: " << metadataPath.parent_path().wstring() << L"\r\n";
    stream << L"Log: " << (metadataPath.parent_path() / "crash.log").wstring() << L"\r\n";
    stream << L"Dump: " << (metadataPath.parent_path() / "crash.dmp").wstring() << L"\r\n";
    stream << L"Metadata: " << metadataPath.wstring() << L"\r\n";
    stream << L"\r\nStack Summary\r\n-------------\r\n";
    std::wstring stack = Utf8ToWide(metadata.stackSummary);
    std::wstring normalized;
    normalized.reserve(stack.size() + 32);
    for (size_t i = 0; i < stack.size(); ++i)
    {
        const wchar_t ch = stack[i];
        if (ch == L'\r')
        {
            normalized.push_back(L'\r');
            if (i + 1 < stack.size() && stack[i + 1] == L'\n')
                normalized.push_back(L'\n');
            continue;
        }
        if (ch == L'\n')
        {
            if (normalized.empty() || normalized.back() != L'\r')
                normalized.push_back(L'\r');
            normalized.push_back(L'\n');
            continue;
        }
        normalized.push_back(ch);
    }
    stream << normalized;
    return stream.str();
}

std::wstring BuildTitle(const Dot::CrashMetadata& metadata)
{
    if (metadata.appName.empty())
        return L"Crash Report";
    return Utf8ToWide(metadata.appName) + L" crashed";
}

std::wstring BuildSubtitle(const Dot::CrashMetadata& metadata)
{
    std::wstring subtitle = L"Crash artifacts were captured locally.";
    if (!metadata.timestampUtc.empty())
        subtitle += L" " + Utf8ToWide(metadata.timestampUtc);
    return subtitle;
}

std::wstring BuildStatusLine(const Dot::CrashMetadata& metadata)
{
    std::wstring status = Utf8ToWide(metadata.exceptionName);
    if (!metadata.exceptionCode.empty())
        status += L"  " + Utf8ToWide(metadata.exceptionCode);
    if (!metadata.exceptionAddress.empty())
        status += L"  at  " + Utf8ToWide(metadata.exceptionAddress);
    return status;
}

HFONT CreateUiFont(int height, int weight)
{
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

class CrashReporterWindow
{
public:
    ~CrashReporterWindow()
    {
        if (m_RichEditModule)
            FreeLibrary(m_RichEditModule);
        if (m_GlyphFont)
            DeleteObject(m_GlyphFont);
        if (m_TitleFont)
            DeleteObject(m_TitleFont);
        if (m_SubtitleFont)
            DeleteObject(m_SubtitleFont);
        if (m_BodyFont)
            DeleteObject(m_BodyFont);
        if (m_HeaderBrush)
            DeleteObject(m_HeaderBrush);
        if (m_BodyBrush)
            DeleteObject(m_BodyBrush);
    }

    bool Initialize(const std::filesystem::path& metadataPath)
    {
        m_MetadataPath = metadataPath;
        if (!Dot::CrashHandling::LoadCrashMetadata(metadataPath, m_Metadata))
            return false;

        m_RichEditModule = LoadLibraryW(L"Msftedit.dll");
        if (!m_RichEditModule)
            return false;

        m_HeaderBrush = CreateSolidBrush(kHeaderColor);
        m_BodyBrush = CreateSolidBrush(kBodyColor);
        m_GlyphFont = CreateUiFont(-32, FW_SEMIBOLD);
        m_TitleFont = CreateUiFont(-28, FW_SEMIBOLD);
        m_SubtitleFont = CreateUiFont(-16, FW_NORMAL);
        m_BodyFont = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = m_BodyBrush;
        wc.lpszClassName = L"DotCrashReporterWindow";

        RegisterClassExW(&wc);

        m_Window = CreateWindowExW(0, wc.lpszClassName, L"Dot Crash Reporter",
                                   WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                   CW_USEDEFAULT, CW_USEDEFAULT, 860, 650, nullptr, nullptr, wc.hInstance, this);
        if (!m_Window)
            return false;

        ShowWindow(m_Window, SW_SHOW);
        UpdateWindow(m_Window);
        return true;
    }

    int Run()
    {
        MSG msg = {};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return static_cast<int>(msg.wParam);
    }

private:
    void CreateControls()
    {
        const DWORD staticStyle = WS_CHILD | WS_VISIBLE;
        const DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
                                ES_AUTOHSCROLL | ES_READONLY | ES_LEFT | ES_NOHIDESEL;
        const DWORD buttonStyle = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;

        m_TitleLabel = CreateWindowExW(0, L"STATIC", BuildTitle(m_Metadata).c_str(), staticStyle, 20, 20, 640, 34,
                                       m_Window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTitleTextId)),
                                       GetModuleHandleW(nullptr), nullptr);
        m_GlyphLabel = CreateWindowExW(0, L"STATIC", kWarningGlyph, staticStyle, 772, 16, 52, 52, m_Window,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGlyphTextId)),
                                       GetModuleHandleW(nullptr), nullptr);
        m_SubtitleLabel =
            CreateWindowExW(0, L"STATIC", BuildSubtitle(m_Metadata).c_str(), staticStyle, 20, 60, 760, 22, m_Window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSubtitleTextId)), GetModuleHandleW(nullptr),
                            nullptr);
        m_StatusLabel =
            CreateWindowExW(0, L"STATIC", BuildStatusLine(m_Metadata).c_str(), staticStyle, 20, 86, 780, 22, m_Window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusTextId)), GetModuleHandleW(nullptr),
                            nullptr);

        m_DetailsEdit = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"", editStyle, 20, 142, 804, 420, m_Window,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTextEditId)),
                                        GetModuleHandleW(nullptr), nullptr);

        m_OpenFolderButton = CreateWindowExW(0, L"BUTTON", L"Open Crash Folder", buttonStyle, 20, 578, 150, 34, m_Window,
                                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpenFolderButtonId)),
                                             GetModuleHandleW(nullptr), nullptr);
        m_OpenLogButton = CreateWindowExW(0, L"BUTTON", L"Open Log", buttonStyle, 180, 578, 110, 34, m_Window,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpenLogButtonId)),
                                          GetModuleHandleW(nullptr), nullptr);
        m_RelaunchButton =
            CreateWindowExW(0, L"BUTTON", L"Relaunch", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 604, 578, 100, 34,
                            m_Window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRelaunchButtonId)),
                            GetModuleHandleW(nullptr), nullptr);
        m_CloseButton = CreateWindowExW(0, L"BUTTON", L"Close", buttonStyle, 724, 578, 100, 34, m_Window,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCloseButtonId)),
                                        GetModuleHandleW(nullptr), nullptr);

        SendMessageW(m_Window, DM_SETDEFID, kRelaunchButtonId, 0);

        SendMessageW(m_TitleLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_TitleFont), TRUE);
        SendMessageW(m_GlyphLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_GlyphFont), TRUE);
        SendMessageW(m_SubtitleLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_SubtitleFont), TRUE);
        SendMessageW(m_StatusLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_SubtitleFont), TRUE);
        SendMessageW(m_DetailsEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_BodyFont), TRUE);
        SendMessageW(m_DetailsEdit, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(kEditBackground));
        const std::wstring detailsText = BuildDisplayText(m_Metadata, m_MetadataPath);
        SendMessageW(m_DetailsEdit, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(detailsText.c_str()));
        SendMessageW(m_OpenFolderButton, WM_SETFONT, reinterpret_cast<WPARAM>(m_SubtitleFont), TRUE);
        SendMessageW(m_OpenLogButton, WM_SETFONT, reinterpret_cast<WPARAM>(m_SubtitleFont), TRUE);
        SendMessageW(m_RelaunchButton, WM_SETFONT, reinterpret_cast<WPARAM>(m_SubtitleFont), TRUE);
        SendMessageW(m_CloseButton, WM_SETFONT, reinterpret_cast<WPARAM>(m_SubtitleFont), TRUE);
    }

    void PaintWindow()
    {
        PAINTSTRUCT ps = {};
        HDC dc = BeginPaint(m_Window, &ps);

        RECT clientRect = {};
        GetClientRect(m_Window, &clientRect);
        FillRect(dc, &clientRect, m_BodyBrush);

        RECT headerRect = clientRect;
        headerRect.bottom = kHeaderHeight;
        FillRect(dc, &headerRect, m_HeaderBrush);

        HPEN accentPen = CreatePen(PS_SOLID, 4, kAccentColor);
        HGDIOBJ oldPen = SelectObject(dc, accentPen);
        MoveToEx(dc, 0, kHeaderHeight, nullptr);
        LineTo(dc, clientRect.right, kHeaderHeight);
        SelectObject(dc, oldPen);
        DeleteObject(accentPen);

        EndPaint(m_Window, &ps);
    }

    void OpenCrashFolder()
    {
        ShellExecuteW(m_Window, L"open", m_MetadataPath.parent_path().wstring().c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
    }

    void OpenLog()
    {
        const std::filesystem::path logPath = m_MetadataPath.parent_path() / "crash.log";
        if (std::filesystem::exists(logPath))
            ShellExecuteW(m_Window, L"open", logPath.wstring().c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
    }

    void Relaunch()
    {
        if (m_Metadata.relaunchExecutablePath.empty())
            return;

        const std::wstring executable = Utf8ToWide(m_Metadata.relaunchExecutablePath);
        std::wstring commandLine = L"\"" + executable + L"\"";
        if (!m_Metadata.relaunchArguments.empty())
            commandLine += L" " + Utf8ToWide(m_Metadata.relaunchArguments);

        STARTUPINFOW startupInfo = {};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo = {};
        if (CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                           &startupInfo, &processInfo))
        {
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
        }
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        CrashReporterWindow* self = nullptr;
        if (msg == WM_NCCREATE)
        {
            auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<CrashReporterWindow*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->m_Window = hwnd;
        }
        else
        {
            self = reinterpret_cast<CrashReporterWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (!self)
            return DefWindowProcW(hwnd, msg, wParam, lParam);

        switch (msg)
        {
            case WM_CREATE:
                self->CreateControls();
                return 0;
            case WM_CTLCOLORSTATIC:
            {
                HDC dc = reinterpret_cast<HDC>(wParam);
                const HWND control = reinterpret_cast<HWND>(lParam);
                SetBkMode(dc, TRANSPARENT);
                if (control == self->m_TitleLabel)
                {
                    SetTextColor(dc, kTitleColor);
                    return reinterpret_cast<LRESULT>(self->m_HeaderBrush);
                }
                if (control == self->m_GlyphLabel)
                {
                    SetTextColor(dc, kAccentColor);
                    return reinterpret_cast<LRESULT>(self->m_HeaderBrush);
                }
                if (control == self->m_SubtitleLabel)
                {
                    SetTextColor(dc, kSubtitleColor);
                    return reinterpret_cast<LRESULT>(self->m_HeaderBrush);
                }
                if (control == self->m_StatusLabel)
                {
                    SetTextColor(dc, kStatusColor);
                    return reinterpret_cast<LRESULT>(self->m_HeaderBrush);
                }

                SetTextColor(dc, kBodyTextColor);
                return reinterpret_cast<LRESULT>(self->m_BodyBrush);
            }
            case WM_CTLCOLOREDIT:
            {
                HDC dc = reinterpret_cast<HDC>(wParam);
                const HWND control = reinterpret_cast<HWND>(lParam);
                if (control != self->m_DetailsEdit)
                    return DefWindowProcW(hwnd, msg, wParam, lParam);
                SetBkColor(dc, kEditBackground);
                SetTextColor(dc, kBodyTextColor);
                static HBRUSH editBrush = CreateSolidBrush(kEditBackground);
                return reinterpret_cast<LRESULT>(editBrush);
            }
            case WM_PAINT:
                self->PaintWindow();
                return 0;
            case WM_COMMAND:
                switch (LOWORD(wParam))
                {
                    case kOpenFolderButtonId:
                        self->OpenCrashFolder();
                        return 0;
                    case kOpenLogButtonId:
                        self->OpenLog();
                        return 0;
                    case kRelaunchButtonId:
                        self->Relaunch();
                        return 0;
                    case kCloseButtonId:
                        DestroyWindow(hwnd);
                        return 0;
                    default:
                        break;
                }
                break;
            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            default:
                break;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    HWND m_Window = nullptr;
    HWND m_TitleLabel = nullptr;
    HWND m_GlyphLabel = nullptr;
    HWND m_SubtitleLabel = nullptr;
    HWND m_StatusLabel = nullptr;
    HWND m_DetailsEdit = nullptr;
    HWND m_OpenFolderButton = nullptr;
    HWND m_OpenLogButton = nullptr;
    HWND m_RelaunchButton = nullptr;
    HWND m_CloseButton = nullptr;
    HFONT m_GlyphFont = nullptr;
    HFONT m_TitleFont = nullptr;
    HFONT m_SubtitleFont = nullptr;
    HFONT m_BodyFont = nullptr;
    HBRUSH m_HeaderBrush = nullptr;
    HBRUSH m_BodyBrush = nullptr;
    HMODULE m_RichEditModule = nullptr;
    std::filesystem::path m_MetadataPath;
    Dot::CrashMetadata m_Metadata;
};

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::filesystem::path metadataPath;
    if (argv && argc > 1)
        metadataPath = WideToUtf8(argv[1]);
    if (argv)
        LocalFree(argv);

    if (metadataPath.empty())
    {
        MessageBoxW(nullptr, L"Missing crash metadata path.", L"Dot Crash Reporter", MB_OK | MB_ICONERROR);
        return 1;
    }

    CrashReporterWindow window;
    if (!window.Initialize(metadataPath))
    {
        MessageBoxW(nullptr, L"Failed to load crash metadata.", L"Dot Crash Reporter", MB_OK | MB_ICONERROR);
        return 1;
    }

    return window.Run();
}
