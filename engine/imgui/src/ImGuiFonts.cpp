// ImGuiFonts.cpp — bundled UI font with installed Windows CJK fallbacks.
#include "mye/imgui/ImGuiFonts.h"

#include "imgui.h"

#include <Windows.h>

#include <string>
#include <vector>

namespace mye::imgui {

namespace {

// wide → UTF-8 (ImGui 파일 IO는 UTF-8 경로를 받아 내부에서 UTF-16으로 변환).
std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

bool FileExists(const std::wstring& p) {
    const DWORD a = ::GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// 실행 파일 디렉터리(끝에 구분자 포함).
std::wstring ExeDir() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s(buf, n);
    const size_t sl = s.find_last_of(L"\\/");
    return sl == std::wstring::npos ? std::wstring{} : s.substr(0, sl + 1);
}

// Bundled fonts live beside the executable; system fonts are never copied.
std::wstring ResolveFont(const wchar_t* file) {
    const std::wstring base = ExeDir();
    const std::wstring rels[] = {
        base + L"assets\\fonts\\" + file,
        base + L"fonts\\" + file,
        base + file,
    };
    for (const auto& p : rels)
        if (FileExists(p)) return p;
    return {};
}

std::wstring ResolveSystemFont(const wchar_t* file) {
    wchar_t windowsDirectory[MAX_PATH]{};
    const auto count = ::GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
    if (count == 0 || count >= MAX_PATH) return {};
    const auto path = std::wstring(windowsDirectory, count) + L"\\Fonts\\" + file;
    return FileExists(path) ? path : std::wstring{};
}

} // namespace

bool LoadEditorFonts(float sizePx) {
    ImGuiIO& io = ImGui::GetIO();

    auto basePath = ResolveFont(L"NanumSquareRoundR.ttf");
    if (basePath.empty()) basePath = ResolveSystemFont(L"malgun.ttf");
    if (basePath.empty())
        return false;   // 폰트 없음 → 기본 폰트 유지(호출부 계속 진행)

    // The official Regular font covers all 11,172 modern Hangul syllables.
    static ImVector<ImWchar> baseRanges;
    if (baseRanges.empty()) {
        ImFontGlyphRangesBuilder b;
        b.AddRanges(io.Fonts->GetGlyphRangesDefault());
        b.AddRanges(io.Fonts->GetGlyphRangesKorean());
        b.BuildRanges(&baseRanges);
    }

    ImFontConfig cfg;
    cfg.OversampleH = 1;   // 아틀라스 크기 축소(CJK 글리프 다수)
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = true;

    const std::string baseUtf8 = Utf8(basePath);
    ImFont* f = io.Fonts->AddFontFromFileTTF(baseUtf8.c_str(), sizePx, &cfg, baseRanges.Data);
    if (f == nullptr)
        return false;

    // CJK coverage follows the installed Windows language fonts.
    auto mergeFont = [&](const wchar_t* file, const ImWchar* rng) {
        const std::wstring p = ResolveSystemFont(file);
        if (p.empty()) return;
        ImFontConfig m;
        m.MergeMode = true;
        m.OversampleH = 1;
        m.OversampleV = 1;
        m.PixelSnapH = true;
        const std::string u = Utf8(p);
        io.Fonts->AddFontFromFileTTF(u.c_str(), sizePx, &m, rng);
    };
    mergeFont(L"meiryo.ttc", io.Fonts->GetGlyphRangesJapanese());
    mergeFont(L"msyh.ttc", io.Fonts->GetGlyphRangesChineseSimplifiedCommon());

    io.FontDefault = f;
    return true;
}

} // namespace mye::imgui
