// mye/core/I18n.cpp — I18n.h 구현. 정적 번역 테이블 + 현재 언어 상태.
#include "mye/core/I18n.h"

#include <array>
#include <string>
#include <unordered_map>

namespace mye::i18n {

namespace {

// 번역 엔트리: {ko, en, ja, zh} — Lang enum 순서와 일치.
struct Entry {
    const char* key;
    const char* ko;
    const char* en;
    const char* ja;
    const char* zh;
};

// UI 문자열 테이블. 새 문자열은 여기에 키를 추가하고 T("key")로 참조한다.
constexpr Entry kTable[] = {
    // ---- 메뉴 ----
    {"menu.file",      "파일",   "File",   "ファイル",   "文件"},
    {"menu.edit",      "편집",   "Edit",   "編集",       "编辑"},
    {"menu.play",      "플레이", "Play",   "プレイ",     "播放"},
    {"menu.window",    "창",     "Window", "ウィンドウ", "窗口"},
    {"menu.language",  "언어",   "Language", "言語",     "语言"},

    {"file.newscene",   "새 씬",        "New Scene",   "新規シーン",     "新建场景"},
    {"file.save",       "저장",         "Save",        "保存",           "保存"},
    {"file.savelayout", "레이아웃 저장","Save Layout", "レイアウト保存", "保存布局"},

    {"edit.undo", "실행 취소", "Undo", "元に戻す", "撤销"},
    {"edit.redo", "다시 실행", "Redo", "やり直し", "重做"},

    {"play.toggle", "재생/정지",   "Play/Stop",  "再生/停止",     "播放/停止"},
    {"play.pause",  "일시정지",    "Pause",      "一時停止",       "暂停"},
    {"play.step",   "프레임 스텝", "Frame Step", "フレームステップ","逐帧"},

    // ---- 툴바 ----
    {"toolbar.newscene", "새 씬", "New Scene", "新規シーン", "新建场景"},
    {"toolbar.save",     "저장",  "Save",      "保存",       "保存"},
    {"toolbar.play",     "재생",  "Play",      "再生",       "播放"},
    {"toolbar.stop",     "정지",  "Stop",      "停止",       "停止"},

    // ---- 패널 제목 ----
    {"panel.hierarchy", "하이어라키",  "Hierarchy", "ヒエラルキー",   "层级"},
    {"panel.viewport",  "씬 뷰포트",   "Scene",     "シーンビュー",   "场景"},
    {"panel.inspector", "인스펙터",    "Inspector", "インスペクター", "检查器"},
    {"panel.assets",    "에셋",        "Assets",    "アセット",       "资源"},
    {"panel.console",   "콘솔",        "Console",   "コンソール",     "控制台"},
    {"panel.doteditor", "닷 에디터",   "Dot Editor","ドットエディタ", "像素编辑器"},

    // ---- 도트 에디터 ----
    {"dot.brush",      "브러시",     "Brush",   "ブラシ",       "画笔"},
    {"dot.eraser",     "지우개",     "Eraser",  "消しゴム",     "橡皮"},
    {"dot.eyedropper", "스포이드",   "Picker",  "スポイト",     "取色"},
    {"dot.bucket",     "채우기",     "Fill",    "塗りつぶし",   "填充"},
    {"dot.grid",       "격자",       "Grid",    "グリッド",     "网格"},
    {"dot.color",      "색",         "Color",   "色",           "颜色"},
    {"dot.size",       "크기",       "Size",    "サイズ",       "大小"},
    {"dot.clear",      "전체 지우기","Clear",   "全消去",       "清空"},
    {"dot.name",       "이름",       "Name",    "名前",         "名称"},
    {"dot.savepng",    "PNG 저장",   "Save PNG","PNG保存",      "保存PNG"},

    // ---- 기타 패널 문자열 ----
    {"inspector.empty",  "선택된 엔티티가 없습니다.", "No entity selected.", "選択されたエンティティがありません。", "未选择实体。"},
    {"console.clear",    "지우기",       "Clear",       "クリア",         "清除"},
    {"console.autoscroll","자동 스크롤", "Auto-scroll", "自動スクロール", "自动滚动"},
    {"console.search",   "검색...",      "Search...",   "検索...",        "搜索..."},
    {"assets.noproject", "프로젝트가 열려 있지 않습니다.", "No project open.", "プロジェクトが開かれていません。", "未打开项目。"},
};

using Table = std::unordered_map<std::string, std::array<std::string, 4>>;

const Table& Data() {
    static const Table t = []() {
        Table m;
        m.reserve(sizeof(kTable) / sizeof(kTable[0]));
        for (const Entry& e : kTable)
            m.emplace(e.key, std::array<std::string, 4>{e.ko, e.en, e.ja, e.zh});
        return m;
    }();
    return t;
}

Lang          g_lang = Lang::Ko;
std::uint32_t g_version = 0;

} // namespace

void SetLanguage(Lang lang) {
    if (lang == g_lang) return;
    g_lang = lang;
    ++g_version;
}

Lang GetLanguage() { return g_lang; }
std::uint32_t Version() { return g_version; }

const char* T(const char* key) {
    if (!key) return "";
    const Table& d = Data();
    auto it = d.find(key);
    if (it == d.end()) return key;   // 미등록 키 → 키 자체 노출(누락 가시화)
    const auto& tr = it->second;
    const std::string& s = tr[static_cast<std::size_t>(g_lang)];
    if (!s.empty()) return s.c_str();
    const std::string& ko = tr[0];
    return ko.empty() ? key : ko.c_str();
}

const char* LangName(Lang lang) {
    switch (lang) {
    case Lang::Ko: return "한국어";
    case Lang::En: return "English";
    case Lang::Ja: return "日本語";
    case Lang::Zh: return "中文";
    }
    return "?";
}

} // namespace mye::i18n
