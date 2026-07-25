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

    const std::wstring haPath = ResolveFont(L"HaFont.ttf");
    if (haPath.empty())
        return false;   // 폰트 없음 → 기본 폰트 유지(호출부 계속 진행)

    // 베이스: HaFont(haruna) = 라틴 + 한글. 한자·가나는 아래 병합 폰트가 담당한다.
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

    const std::string haUtf8 = Utf8(haPath);
    ImFont* f = io.Fonts->AddFontFromFileTTF(haUtf8.c_str(), sizePx, &cfg, baseRanges.Data);
    if (f == nullptr)
        return false;

    // 병합 폰트: 같은 글리프 아틀라스에 얹어 코드포인트 커버리지를 넓힌다(MergeMode).
    //   먼저 병합된 폰트가 해당 코드포인트를 선점 → HaFont(한글)에 없는 일본어 한자/가나는
    //   M PLUS Rounded 1c 가, 중국어 간체 한자는 ZCOOL KuaiLe 가 채운다. 둘 다 둥근 서체(가독성).
    auto mergeFont = [&](const wchar_t* file, const ImWchar* rng) {
        const std::wstring p = ResolveFont(file);
        if (p.empty()) return;
        ImFontConfig m;
        m.MergeMode = true;
        m.OversampleH = 1;
        m.OversampleV = 1;
        m.PixelSnapH = true;
        const std::string u = Utf8(p);
        io.Fonts->AddFontFromFileTTF(u.c_str(), sizePx, &m, rng);
    };
    mergeFont(L"MPLUSRounded1c-Regular.ttf", io.Fonts->GetGlyphRangesJapanese());
    mergeFont(L"ZCOOLKuaiLe-Regular.ttf",    io.Fonts->GetGlyphRangesChineseSimplifiedCommon());

    io.FontDefault = f;
    return true;
}

} // namespace mye::imgui
