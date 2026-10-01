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
    {"menu.display",   "화면",   "Display",  "画面",     "显示"},

    {"display.fullscreen", "전체화면", "Fullscreen", "フルスクリーン", "全屏"},
    {"display.vsync",      "수직동기(VSync)", "VSync", "垂直同期(VSync)", "垂直同步(VSync)"},

    {"file.newscene",   "새 씬",        "New Scene",   "新規シーン",     "新建场景"},
    {"file.save",       "저장",         "Save",        "保存",           "保存"},
    {"file.savelayout", "레이아웃 저장","Save Layout", "レイアウト保存", "保存布局"},
    {"file.newproject", "새 프로젝트", "New Project", "新規プロジェクト", "新建项目"},
    {"file.openproject", "프로젝트 열기", "Open Project", "プロジェクトを開く", "打开项目"},
    {"file.openfolder", "프로젝트 폴더 열기", "Open Project Folder", "プロジェクトフォルダを開く", "打开项目文件夹"},
    {"file.saveproject", "프로젝트 저장", "Save Project", "プロジェクトを保存", "保存项目"},
    {"file.openscene", "씬 열기", "Open Scene", "シーンを開く", "打开场景"},
    {"file.saveas", "다른 이름으로 저장", "Save Scene As", "名前を付けて保存", "场景另存为"},
    {"file.projectname", "프로젝트 이름", "Project Name", "プロジェクト名", "项目名称"},
    {"file.projectfolder", "저장 폴더", "Project Folder", "保存先", "项目文件夹"},
    {"file.browsefolder", "폴더 선택...", "Browse Folder...", "フォルダ選択...", "选择文件夹..."},
    {"file.emptyfolder", "새 폴더 또는 빈 폴더를 선택하세요. 기존 파일이 있는 폴더에는 만들 수 없습니다.", "Choose a new or empty folder. Existing files are preserved.", "新しいフォルダまたは空のフォルダを選択してください。", "请选择新文件夹或空文件夹。保留现有文件。"},
    {"file.create", "만들기", "Create", "作成", "创建"},
    {"file.cancel", "취소", "Cancel", "キャンセル", "取消"},
    {"file.created", "프로젝트를 만들었습니다.", "Project created.", "プロジェクトを作成しました。", "项目已创建。"},
    {"file.opened", "파일을 열었습니다.", "File opened.", "ファイルを開きました。", "文件已打开。"},
    {"file.saved", "저장했습니다.", "Saved.", "保存しました。", "已保存。"},
    {"file.error", "실패", "Error", "エラー", "错误"},
    {"file.stopfirst", "플레이를 멈춘 뒤 편집 파일을 저장하거나 열어주세요.", "Stop play before saving or opening edit files.", "再生を停止してから保存・読み込みしてください。", "请停止播放后再保存或打开编辑文件。"},
    {"file.welcome", "새 프로젝트를 만들거나 기존 프로젝트 파일/폴더를 열어 편집을 시작하세요.", "Create a project or open an existing project file/folder to start editing.", "プロジェクトを作成するか既存のファイル・フォルダを開いてください。", "新建项目或打开现有项目文件/文件夹以开始编辑。"},
    {"file.unsavedprompt", "프로젝트 또는 씬에 저장하지 않은 변경이 있습니다. 계속하기 전에 프로젝트를 저장할까요?\n\n예: 저장 후 계속\n아니요: 변경 폐기 후 계속\n취소: 현재 프로젝트 유지", "The project or its scenes have unsaved changes. Save the project before continuing?\n\nYes: Save and continue\nNo: Discard changes and continue\nCancel: Keep the current project", "プロジェクトまたはシーンに未保存の変更があります。続行前に保存しますか？\nはい: 保存 / いいえ: 破棄 / キャンセル: 維持", "项目或场景存在未保存的更改。继续前保存项目？\n是: 保存 / 否: 放弃更改 / 取消: 保留当前项目"},

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
    {"panel.anim",      "애니메이션", "Animation", "アニメーション", "动画"},
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
