// ConsolePanel.cpp — 콘솔 패널(엔진 로그 싱크 구독·필터·검색·레벨색상·스크립트 에러 링크)
//   (docs/07 콘솔)
//
// 07 콘솔: 엔진 로그(카테고리×심각도)를 링버퍼 싱크로 수집→레벨/검색 필터, 레벨색상, 자동
//   스크롤. 스크립트 에러(ScriptErrorEvent)는 파일·라인과 함께 강조 표시(더블클릭 시 향후
//   에디터가 소스로 점프 — v1은 표시까지). 로그 싱크는 패널이 아니라 프로세스 전역(Log)에
//   장착되므로, 패널 인스턴스와 독립적인 공유 링버퍼(ConsoleLogStore)를 둔다.
//
// 소유: 내장 패널도 1급 플러그인 — IEditorPanelFactory로 등록(RegisterBuiltinPanels 경유).
#include "mye/editor/Panel.h"
#include "mye/editor/EditorContext.h"

#include "mye/core/Log.h"
#include "mye/core/I18n.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace mye::editor {

namespace {

const PanelDesc kConsoleDesc{
    /*id*/ "mye.console",
    /*title*/ "콘솔",
    /*allowMultiple*/ false,
    /*defaultDock*/ DockSlot::Bottom,
};

// ---- 공유 로그 저장소 + 싱크 ----
// 프로세스 전역 Log 에 장착되는 링버퍼. 패널이 여러 개/재생성돼도 로그는 여기 하나에 모인다.
struct ConsoleEntry {
    LogSeverity severity = LogSeverity::Info;
    std::string category;
    std::string message;
    std::uint64_t timestampNs = 0;
    // 스크립트 에러 링크(있으면). file 비어있지 않으면 소스 링크로 표시.
    std::string scriptFile;
    int         scriptLine = 0;
};

class ConsoleLogStore {
public:
    static ConsoleLogStore& Get() {
        static ConsoleLogStore s;
        return s;
    }

    void Push(ConsoleEntry e) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_entries.push_back(std::move(e));
        if (m_entries.size() > kMaxEntries)
            m_entries.pop_front();
        ++m_revision;
    }

    void Clear() {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_entries.clear();
        ++m_revision;
    }

    // 스냅샷 복사(락 최소화 — 드로우 중 싱크가 다른 스레드에서 push 가능).
    void Snapshot(std::vector<ConsoleEntry>& out, std::uint64_t& revision) {
        std::lock_guard<std::mutex> lk(m_mutex);
        out.assign(m_entries.begin(), m_entries.end());
        revision = m_revision;
    }

    std::uint64_t Revision() {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_revision;
    }

private:
    static constexpr std::size_t kMaxEntries = 4096;
    std::mutex             m_mutex;
    std::deque<ConsoleEntry> m_entries;
    std::uint64_t          m_revision = 0;
};

// 전역 Log 에 장착하는 싱크 — 모든 로그를 ConsoleLogStore 로 밀어넣는다.
class ConsoleLogSink final : public ILogSink {
public:
    void Write(const LogMessage& msg) override {
        ConsoleEntry e;
        e.severity = msg.severity;
        e.category = std::string(msg.category);
        e.message = std::string(msg.message);
        e.timestampNs = msg.timestampNs;
        ConsoleLogStore::Get().Push(std::move(e));
    }
};

const char* SeverityLabel(LogSeverity s) {
    switch (s) {
    case LogSeverity::Trace: return "TRACE";
    case LogSeverity::Debug: return "DEBUG";
    case LogSeverity::Info:  return "INFO";
    case LogSeverity::Warn:  return "WARN";
    case LogSeverity::Error: return "ERROR";
    case LogSeverity::Fatal: return "FATAL";
    }
    return "?";
}

ImVec4 SeverityColor(LogSeverity s) {
    switch (s) {
    case LogSeverity::Trace: return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
    case LogSeverity::Debug: return ImVec4(0.60f, 0.70f, 0.90f, 1.0f);
    case LogSeverity::Info:  return ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
    case LogSeverity::Warn:  return ImVec4(0.95f, 0.80f, 0.35f, 1.0f);
    case LogSeverity::Error: return ImVec4(0.95f, 0.40f, 0.40f, 1.0f);
    case LogSeverity::Fatal: return ImVec4(1.00f, 0.25f, 0.55f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

class ConsolePanel final : public IEditorPanel {
public:
    const PanelDesc& Desc() const override { return kConsoleDesc; }

    void OnGui(EditorContext& ctx) override {
        (void)ctx;
        if (!ImGui::Begin(mye::i18n::T("panel.console"))) {
            ImGui::End();
            return;
        }

        // ---- 툴바: 지우기 · 자동스크롤 · 레벨 토글 · 검색 ----
        if (ImGui::Button(mye::i18n::T("console.clear"))) ConsoleLogStore::Get().Clear();
        ImGui::SameLine();
        ImGui::Checkbox(mye::i18n::T("console.autoscroll"), &m_autoScroll);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // 레벨 필터 토글(최소 심각도 콤보).
        static const char* kLevels[] = {"Trace+", "Debug+", "Info+", "Warn+", "Error+"};
        ImGui::SetNextItemWidth(90.0f);
        ImGui::Combo("##minlevel", &m_minLevelIdx, kLevels, IM_ARRAYSIZE(kLevels));
        ImGui::SameLine();

        // 검색.
        ImGui::SetNextItemWidth(-1.0f);
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s", m_search.c_str());
        if (ImGui::InputTextWithHint("##search", mye::i18n::T("console.search"), buf, sizeof(buf)))
            m_search = buf;

        ImGui::Separator();

        // ---- 로그 목록 ----
        std::uint64_t rev = ConsoleLogStore::Get().Revision();
        if (rev != m_cacheRevision) {
            ConsoleLogStore::Get().Snapshot(m_cache, m_cacheRevision);
        }

        const LogSeverity minSev = static_cast<LogSeverity>(m_minLevelIdx);
        if (ImGui::BeginChild("##console_scroll", ImVec2(0, 0), false,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            for (const ConsoleEntry& e : m_cache) {
                if (static_cast<int>(e.severity) < static_cast<int>(minSev)) continue;
                if (!m_search.empty() && !Matches(e, m_search)) continue;

                ImGui::PushStyleColor(ImGuiCol_Text, SeverityColor(e.severity));
                if (!e.scriptFile.empty()) {
                    // 스크립트 에러 — 파일·라인 링크형 표시.
                    ImGui::Text("[%s][%s] %s (%s:%d)", SeverityLabel(e.severity),
                                e.category.c_str(), e.message.c_str(),
                                e.scriptFile.c_str(), e.scriptLine);
                } else {
                    ImGui::Text("[%s][%s] %s", SeverityLabel(e.severity),
                                e.category.c_str(), e.message.c_str());
                }
                ImGui::PopStyleColor();
            }

            if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        ImGui::End();
    }

    void SerializeState(json::Value& out) const override {
        json::Value::Object o;
        o["type"] = json::Value(std::string("mye.console"));
        out = json::Value(std::move(o));
    }

private:
    static bool Matches(const ConsoleEntry& e, const std::string& q) {
        auto contains = [&](const std::string& s) {
            if (q.empty()) return true;
            // 대소문자 무시 부분일치.
            auto it = std::search(s.begin(), s.end(), q.begin(), q.end(),
                                  [](char a, char b) {
                                      return std::tolower((unsigned char)a) ==
                                             std::tolower((unsigned char)b);
                                  });
            return it != s.end();
        };
        return contains(e.message) || contains(e.category) || contains(e.scriptFile);
    }

    bool                       m_autoScroll = true;
    int                        m_minLevelIdx = 0;   // 0=Trace+
    std::string                m_search;
    std::vector<ConsoleEntry>  m_cache;
    std::uint64_t              m_cacheRevision = ~0ull;
};

// 스크립트 에러(월드-로컬 ScriptErrorEvent) → 콘솔 항목. ScriptSystem 이 에러를 Log 로도
//   남기므로 기본 표시는 로그 싱크로 충분하나, 파일·라인 링크가 필요한 경우 이 진입점으로
//   구조화 항목을 직접 밀어넣을 수 있다(플레이 모드 배선 에이전트가 소비 가능).
void PushConsoleScriptError(std::string_view file, int line, std::string_view message) {
    ConsoleEntry e;
    e.severity = LogSeverity::Error;
    e.category = "Script";
    e.message = std::string(message);
    e.scriptFile = std::string(file);
    e.scriptLine = line;
    ConsoleLogStore::Get().Push(std::move(e));
}

class ConsolePanelFactory final : public IEditorPanelFactory {
public:
    const PanelDesc& Desc() const override { return kConsoleDesc; }
    std::unique_ptr<IEditorPanel> Create() override {
        return std::make_unique<ConsolePanel>();
    }
};

} // namespace

// 콘솔 로그 싱크를 전역 Log 에 1회 장착한다(EditorApp 초기화가 호출).
void InstallConsoleLogSink() {
    Log::AddSink(std::make_unique<ConsoleLogSink>());
}

std::unique_ptr<IEditorPanelFactory> MakeConsolePanelFactory() {
    return std::make_unique<ConsolePanelFactory>();
}

} // namespace mye::editor
