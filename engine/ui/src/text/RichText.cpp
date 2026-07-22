// mye/text/RichText.cpp — 리치 텍스트 태그 파서 (M5-A)
//
// 태그: {color=#RRGGBB}...{/color}, {outline=#RRGGBB}...{/outline}, {shadow}...{/shadow},
//       {icon=name}, {link=payload}...{/link}. 이스케이프: `{{` = 리터럴 '{'.
//   여는 태그는 해당 속성 스택에 push, 닫는 태그는 pop. 텍스트 조각은 현재 스택 스냅샷을 갖는다.
#include "mye/text/RichText.h"

#include <vector>

namespace mye::text {

Color ParseHexColor(std::string_view hex, Color fallback) {
    if (hex.size() >= 1 && hex[0] == '#') hex.remove_prefix(1);
    auto hexNib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    auto byteAt = [&](size_t i) -> int {
        if (i + 1 >= hex.size()) return -1;
        int hi = hexNib(hex[i]), lo = hexNib(hex[i + 1]);
        return (hi < 0 || lo < 0) ? -1 : (hi * 16 + lo);
    };
    if (hex.size() != 6 && hex.size() != 8) return fallback;
    int r = byteAt(0), g = byteAt(2), b = byteAt(4);
    int a = hex.size() == 8 ? byteAt(6) : 255;
    if (r < 0 || g < 0 || b < 0 || a < 0) return fallback;
    return Color{r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
}

namespace {

// 태그 이름/값 분해: "color=#FF0000" → name="color", value="#FF0000".
//   "/color" → name="/color", value="". "icon=potion" → name="icon", value="potion".
void SplitTag(std::string_view inner, std::string_view& name, std::string_view& value) {
    const size_t eq = inner.find('=');
    if (eq == std::string_view::npos) {
        name = inner;
        value = {};
    } else {
        name = inner.substr(0, eq);
        value = inner.substr(eq + 1);
    }
}

// 현재 유효 스타일을 스택 top 으로 구성해 run 에 반영.
struct StyleState {
    std::vector<Color> colorStack;
    std::vector<Color> outlineStack;   // outline 이 스택에 있으면 활성
    int shadowDepth = 0;
    std::vector<ShadowDesc> shadowStack;

    void applyTo(TextRun& run, const TextStyle& base) const {
        run.color = colorStack.empty() ? base.color : colorStack.back();
        if (!outlineStack.empty())      run.outline = outlineStack.back();
        else                            run.outline = base.outline;
        if (!shadowStack.empty())       run.shadow = shadowStack.back();
        else                            run.shadow = base.shadow;
    }
};

} // namespace

RichTextParse RichTextParser::Parse(std::string_view richText, const TextStyle& base) {
    RichTextParse out;
    StyleState st;
    // base 스타일을 스택 바닥으로.
    st.colorStack.push_back(base.color);
    if (base.outline) st.outlineStack.push_back(*base.outline);
    if (base.shadow)  st.shadowStack.push_back(*base.shadow);

    size_t i = 0;
    size_t runStart = 0;   // 현재 텍스트 조각 시작

    auto flushText = [&](size_t end) {
        if (end > runStart) {
            TextRun run;
            run.kind = RunKind::Text;
            run.utf8Text = richText.substr(runStart, end - runStart);
            st.applyTo(run, base);
            out.runs.push_back(run);
        }
    };

    const size_t n = richText.size();
    // 이스케이프된 '{{' 를 텍스트에 그대로 보존하기 위해, 조각을 별도 버퍼로 재조립하지 않고
    //   '{{' 는 별개 Text 런('{')으로 잘라낸다(원본 슬라이스 규약 유지).
    while (i < n) {
        const char c = richText[i];
        if (c == '{') {
            if (i + 1 < n && richText[i + 1] == '{') {
                // 이스케이프: 여기까지 flush 후 '{' 리터럴 1글자 런 삽입.
                flushText(i);
                TextRun lit;
                lit.kind = RunKind::Text;
                lit.utf8Text = richText.substr(i, 1);   // "{"
                st.applyTo(lit, base);
                out.runs.push_back(lit);
                i += 2;
                runStart = i;
                continue;
            }
            // 태그 시작. 닫는 '}' 찾기.
            const size_t close = richText.find('}', i + 1);
            if (close == std::string_view::npos) {
                // 닫히지 않은 '{' → 리터럴 취급(무해 폴백).
                out.hadError = true;
                break;   // 나머지는 아래 tail flush 로 텍스트 처리
            }
            flushText(i);
            const std::string_view inner = richText.substr(i + 1, close - (i + 1));
            std::string_view name, value;
            SplitTag(inner, name, value);

            if (name == "color") {
                st.colorStack.push_back(ParseHexColor(value, st.colorStack.back()));
            } else if (name == "/color") {
                if (st.colorStack.size() > 1) st.colorStack.pop_back();
                else out.hadError = true;
            } else if (name == "outline") {
                st.outlineStack.push_back(ParseHexColor(value, Color::Black()));
            } else if (name == "/outline") {
                if (!st.outlineStack.empty()) st.outlineStack.pop_back();
                else out.hadError = true;
            } else if (name == "shadow") {
                ShadowDesc sd;
                if (!value.empty()) sd.color = ParseHexColor(value, Color::Black());
                st.shadowStack.push_back(sd);
            } else if (name == "/shadow") {
                if (!st.shadowStack.empty()) st.shadowStack.pop_back();
                else out.hadError = true;
            } else if (name == "icon") {
                TextRun run;
                run.kind = RunKind::Icon;
                run.iconName = value;
                st.applyTo(run, base);
                out.runs.push_back(run);
            } else if (name == "link") {
                TextRun run;
                run.kind = RunKind::LinkBegin;
                run.linkPayload = value;
                st.applyTo(run, base);
                out.runs.push_back(run);
            } else if (name == "/link") {
                TextRun run;
                run.kind = RunKind::LinkEnd;
                st.applyTo(run, base);
                out.runs.push_back(run);
            } else {
                // 알 수 없는 태그 → 무시(경고). 텍스트로 노출하지 않음.
                out.hadError = true;
            }
            i = close + 1;
            runStart = i;
        } else {
            ++i;
        }
    }
    // 남은 텍스트(이스케이프 미종료 케이스 포함).
    flushText(n);
    return out;
}

std::string RichTextParser::StripTags(std::string_view richText) {
    std::string out;
    out.reserve(richText.size());
    const size_t n = richText.size();
    size_t i = 0;
    while (i < n) {
        const char c = richText[i];
        if (c == '{') {
            if (i + 1 < n && richText[i + 1] == '{') {
                out.push_back('{');
                i += 2;
                continue;
            }
            const size_t close = richText.find('}', i + 1);
            if (close == std::string_view::npos) {
                out.append(richText.substr(i));
                break;
            }
            // {icon=...} 은 시각적 자리를 차지하지만 순수 텍스트로는 제거(측정·검색용).
            i = close + 1;
        } else {
            out.push_back(c);
            ++i;
        }
    }
    return out;
}

} // namespace mye::text
