// mye/imgui/ImGuiFonts.cpp — ImGuiFonts.h 구현 (HaFont/haruna CJK 로딩 + 경로 해석)
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

// HaFont.ttf 후보 경로 — exe 상대(배포) 우선, 개발 편의를 위해 저장소 절대 경로 폴백.
std::wstring ResolveFont(const wchar_t* file) {
    const std::wstring base = ExeDir();
    const std::wstring rels[] = {
        base + L"assets\\fonts\\" + file,
        base + L"fonts\\" + file,
        base + file,
        std::wstring(L"E:\\MyEngine\\assets\\fonts\\") + file,   // 개발 폴백
    };
    for (const auto& p : rels)
        if (FileExists(p)) return p;
    return {};
}

} // namespace

bool LoadEditorFonts(float sizePx) {
    ImGuiIO& io = ImGui::GetIO();

    const std::wstring path = ResolveFont(L"HaFont.ttf");
    if (path.empty())
        return false;   // 폰트 없음 → 기본 폰트 유지(호출부 계속 진행)

    // 글리프 범위: 기본(라틴) + 한글 + 중/일 상용. HaFont(haruna)는 CJK 커버리지가 넓어
    // 한글은 확실히 렌더된다. ja/zh 미커버 글리프는 i18n 단계에서 폰트 병합으로 보강한다.
    static ImVector<ImWchar> ranges;
    if (ranges.empty()) {
        ImFontGlyphRangesBuilder b;
        b.AddRanges(io.Fonts->GetGlyphRangesDefault());
        b.AddRanges(io.Fonts->GetGlyphRangesKorean());
        b.AddRanges(io.Fonts->GetGlyphRangesJapanese());
        b.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        b.BuildRanges(&ranges);
    }

    ImFontConfig cfg;
    cfg.OversampleH = 1;   // 아틀라스 크기 축소(한글 글리프 다수)
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = true;

    const std::string utf8 = Utf8(path);
    ImFont* f = io.Fonts->AddFontFromFileTTF(utf8.c_str(), sizePx, &cfg, ranges.Data);
    if (f == nullptr)
        return false;

    io.FontDefault = f;
    return true;
}

} // namespace mye::imgui
