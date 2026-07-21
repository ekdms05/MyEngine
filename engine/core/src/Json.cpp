// Json.cpp — 코어 내장 최소 JSON 파서·직렬화 (RFC 8259 서브셋, docs/01 설정 시스템)
//
// 규약: 예외 불사용 — 파서는 내부 에러 상태 누적 후 Expected로 반환. 에러 메시지에
// 줄·열·오프셋 위치를 포함한다. 주석·트레일링 콤마 미지원(포맷 완화는 오픈 이슈 A3).
#include "mye/core/Json.h"

#include <charconv>
#include <cmath>
#include <format>

namespace mye::json {

const Value* Value::Find(std::string_view key) const {
    if (!IsObject()) return nullptr;
    const auto it = m_object.find(key);
    return it != m_object.end() ? &it->second : nullptr;
}

// ---------------------------------------------------------------------------
// 파서
// ---------------------------------------------------------------------------
namespace {

constexpr int kMaxDepth = 64;   // 재귀 하강 깊이 상한 (스택 오버플로 방지)

constexpr bool IsDigit(char c) { return c >= '0' && c <= '9'; }

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

class Parser {
public:
    explicit Parser(std::string_view text)
        : m_begin(text.data()), m_cur(text.data()), m_end(text.data() + text.size()) {}

    Expected<Value, Error> Run() {
        // UTF-8 BOM 허용 (파일 로드 관용)
        if (m_end - m_cur >= 3 && static_cast<unsigned char>(m_cur[0]) == 0xEF &&
            static_cast<unsigned char>(m_cur[1]) == 0xBB &&
            static_cast<unsigned char>(m_cur[2]) == 0xBF) {
            m_cur += 3;
        }
        Value root;
        if (!ParseValue(root)) return MakeError();
        SkipWs();
        if (m_cur != m_end) {
            SetError("unexpected trailing characters after JSON value");
            return MakeError();
        }
        return root;
    }

private:
    bool AtEnd() const { return m_cur == m_end; }
    char Peek() const { return *m_cur; }

    void SkipWs() {
        while (m_cur != m_end &&
               (*m_cur == ' ' || *m_cur == '\t' || *m_cur == '\n' || *m_cur == '\r'))
            ++m_cur;
    }

    // 최초 에러만 보존 (위치 = 현재 커서)
    bool SetError(std::string reason) {
        if (m_error.empty()) {
            m_errorPos = m_cur;
            m_error = std::move(reason);
        }
        return false;
    }

    Error MakeError() const {
        int line = 1, column = 1;
        for (const char* p = m_begin; p < m_errorPos && p < m_end; ++p) {
            if (*p == '\n') { ++line; column = 1; }
            else ++column;
        }
        return Error{std::format("JSON parse error at line {}, column {} (offset {}): {}",
                                 line, column, m_errorPos - m_begin,
                                 m_error.empty() ? "unknown error" : m_error),
                     1};
    }

    bool ParseValue(Value& out) {
        if (m_depth >= kMaxDepth)
            return SetError(std::format("nesting depth exceeds limit ({})", kMaxDepth));
        ++m_depth;
        const bool ok = ParseValueInner(out);
        --m_depth;
        return ok;
    }

    bool ParseValueInner(Value& out) {
        SkipWs();
        if (AtEnd()) return SetError("unexpected end of input, expected a value");
        switch (Peek()) {
            case '{': return ParseObject(out);
            case '[': return ParseArray(out);
            case '"': {
                std::string s;
                if (!ParseString(s)) return false;
                out = Value(std::move(s));
                return true;
            }
            case 't':
                if (!ParseLiteral("true")) return false;
                out = Value(true);
                return true;
            case 'f':
                if (!ParseLiteral("false")) return false;
                out = Value(false);
                return true;
            case 'n':
                if (!ParseLiteral("null")) return false;
                out = Value();
                return true;
            default:
                if (Peek() == '-' || IsDigit(Peek())) return ParseNumber(out);
                return SetError(std::format("unexpected character '{}'", Peek()));
        }
    }

    bool ParseLiteral(std::string_view lit) {
        if (m_end - m_cur < static_cast<ptrdiff_t>(lit.size()) ||
            std::string_view(m_cur, lit.size()) != lit)
            return SetError(std::format("invalid literal, expected '{}'", lit));
        m_cur += lit.size();
        return true;
    }

    bool ParseObject(Value& out) {
        ++m_cur;   // '{'
        Value::Object obj;
        SkipWs();
        if (!AtEnd() && Peek() == '}') {
            ++m_cur;
            out = Value(std::move(obj));
            return true;
        }
        for (;;) {
            SkipWs();
            if (AtEnd() || Peek() != '"')
                return SetError("expected object key (string)");
            std::string key;
            if (!ParseString(key)) return false;
            SkipWs();
            if (AtEnd() || Peek() != ':') return SetError("expected ':' after object key");
            ++m_cur;
            Value v;
            if (!ParseValue(v)) return false;
            obj.insert_or_assign(std::move(key), std::move(v));   // 중복 키: 마지막 승리
            SkipWs();
            if (AtEnd()) return SetError("unterminated object, expected ',' or '}'");
            if (Peek() == ',') { ++m_cur; continue; }
            if (Peek() == '}') { ++m_cur; break; }
            return SetError("expected ',' or '}' in object");
        }
        out = Value(std::move(obj));
        return true;
    }

    bool ParseArray(Value& out) {
        ++m_cur;   // '['
        Value::Array arr;
        SkipWs();
        if (!AtEnd() && Peek() == ']') {
            ++m_cur;
            out = Value(std::move(arr));
            return true;
        }
        for (;;) {
            Value v;
            if (!ParseValue(v)) return false;
            arr.push_back(std::move(v));
            SkipWs();
            if (AtEnd()) return SetError("unterminated array, expected ',' or ']'");
            if (Peek() == ',') { ++m_cur; continue; }
            if (Peek() == ']') { ++m_cur; break; }
            return SetError("expected ',' or ']' in array");
        }
        out = Value(std::move(arr));
        return true;
    }

    bool ParseString(std::string& out) {
        ++m_cur;   // '"'
        out.clear();
        for (;;) {
            if (AtEnd()) return SetError("unterminated string");
            const unsigned char c = static_cast<unsigned char>(*m_cur);
            if (c == '"') { ++m_cur; return true; }
            if (c == '\\') {
                ++m_cur;
                if (!ParseEscape(out)) return false;
                continue;
            }
            if (c < 0x20)
                return SetError("raw control character in string (use \\u escape)");
            out.push_back(static_cast<char>(c));   // UTF-8 바이트 그대로 통과
            ++m_cur;
        }
    }

    bool ParseEscape(std::string& out) {
        if (AtEnd()) return SetError("unterminated escape sequence");
        const char e = *m_cur++;
        switch (e) {
            case '"':  out.push_back('"');  return true;
            case '\\': out.push_back('\\'); return true;
            case '/':  out.push_back('/');  return true;
            case 'b':  out.push_back('\b'); return true;
            case 'f':  out.push_back('\f'); return true;
            case 'n':  out.push_back('\n'); return true;
            case 'r':  out.push_back('\r'); return true;
            case 't':  out.push_back('\t'); return true;
            case 'u': {
                uint32_t cp = 0;
                if (!ParseHex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF) {   // 상위 서로게이트 → 페어 필수
                    if (m_end - m_cur < 2 || m_cur[0] != '\\' || m_cur[1] != 'u')
                        return SetError("high surrogate not followed by \\u low surrogate");
                    m_cur += 2;
                    uint32_t low = 0;
                    if (!ParseHex4(low)) return false;
                    if (low < 0xDC00 || low > 0xDFFF)
                        return SetError("invalid low surrogate in \\u pair");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return SetError("unpaired low surrogate");
                }
                AppendUtf8(out, cp);
                return true;
            }
            default:
                --m_cur;
                return SetError(std::format("invalid escape character '\\{}'", e));
        }
    }

    bool ParseHex4(uint32_t& out) {
        if (m_end - m_cur < 4) return SetError("truncated \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = m_cur[i];
            uint32_t digit;
            if (c >= '0' && c <= '9')      digit = static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') digit = static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = static_cast<uint32_t>(c - 'A' + 10);
            else return SetError("invalid hex digit in \\u escape");
            out = (out << 4) | digit;
        }
        m_cur += 4;
        return true;
    }

    bool ParseNumber(Value& out) {
        const char* start = m_cur;
        if (!AtEnd() && Peek() == '-') ++m_cur;
        if (AtEnd() || !IsDigit(Peek())) return SetError("invalid number, expected digit");
        if (Peek() == '0') {
            ++m_cur;
            if (!AtEnd() && IsDigit(Peek()))
                return SetError("leading zeros are not allowed in number");
        } else {
            while (!AtEnd() && IsDigit(Peek())) ++m_cur;
        }
        if (!AtEnd() && Peek() == '.') {
            ++m_cur;
            if (AtEnd() || !IsDigit(Peek()))
                return SetError("expected digits after decimal point");
            while (!AtEnd() && IsDigit(Peek())) ++m_cur;
        }
        if (!AtEnd() && (Peek() == 'e' || Peek() == 'E')) {
            ++m_cur;
            if (!AtEnd() && (Peek() == '+' || Peek() == '-')) ++m_cur;
            if (AtEnd() || !IsDigit(Peek()))
                return SetError("expected digits in exponent");
            while (!AtEnd() && IsDigit(Peek())) ++m_cur;
        }
        double d = 0.0;
        const auto result = std::from_chars(start, m_cur, d);
        if (result.ec != std::errc{} || result.ptr != m_cur) {
            m_cur = start;   // 에러 위치를 숫자 시작으로
            return SetError("number out of range or malformed");
        }
        out = Value(d);
        return true;
    }

    const char* m_begin;
    const char* m_cur;
    const char* m_end;
    const char* m_errorPos = nullptr;
    std::string m_error;
    int         m_depth = 0;
};

} // namespace

Expected<Value, Error> Parse(std::string_view utf8Text) {
    return Parser(utf8Text).Run();
}

// ---------------------------------------------------------------------------
// 직렬화 (ConfigSystem::Save 용 최소 구현 — pretty print)
// ---------------------------------------------------------------------------
namespace {

void AppendEscaped(std::string& out, std::string_view s) {
    out.push_back('"');
    for (const char raw : s) {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) out += std::format("\\u{:04x}", c);
                else out.push_back(raw);   // UTF-8 바이트 그대로
                break;
        }
    }
    out.push_back('"');
}

void AppendNumber(std::string& out, double n) {
    if (!std::isfinite(n)) {   // NaN/Inf는 유효한 JSON이 아님 → null
        out += "null";
        return;
    }
    // 정수값은 정수 표기 (2^53 이하 — double 정밀 정수 범위)
    if (n == std::floor(n) && std::abs(n) <= 9007199254740992.0) {
        out += std::format("{}", static_cast<int64_t>(n));
        return;
    }
    out += std::format("{}", n);   // shortest round-trip 표기
}

void StringifyInto(std::string& out, const Value& v, int indent, int level) {
    const bool pretty = indent > 0;
    const auto pad = [&](int lvl) {
        if (pretty) out.append(static_cast<size_t>(indent) * static_cast<size_t>(lvl), ' ');
    };
    switch (v.GetType()) {
        case Type::Null:   out += "null"; break;
        case Type::Bool:   out += v.AsBool() ? "true" : "false"; break;
        case Type::Number: AppendNumber(out, v.AsDouble()); break;
        case Type::String: AppendEscaped(out, v.AsString()); break;
        case Type::Array: {
            const auto& arr = v.AsArray();
            if (arr.empty()) { out += "[]"; break; }
            out.push_back('[');
            if (pretty) out.push_back('\n');
            for (size_t i = 0; i < arr.size(); ++i) {
                pad(level + 1);
                StringifyInto(out, arr[i], indent, level + 1);
                if (i + 1 < arr.size()) out.push_back(',');
                if (pretty) out.push_back('\n');
            }
            pad(level);
            out.push_back(']');
            break;
        }
        case Type::Object: {
            const auto& obj = v.AsObject();
            if (obj.empty()) { out += "{}"; break; }
            out.push_back('{');
            if (pretty) out.push_back('\n');
            size_t i = 0;
            for (const auto& [key, val] : obj) {
                pad(level + 1);
                AppendEscaped(out, key);
                out += pretty ? ": " : ":";
                StringifyInto(out, val, indent, level + 1);
                if (++i < obj.size()) out.push_back(',');
                if (pretty) out.push_back('\n');
            }
            pad(level);
            out.push_back('}');
            break;
        }
    }
}

} // namespace

std::string Stringify(const Value& value, int indent) {
    std::string out;
    StringifyInto(out, value, indent, 0);
    return out;
}

} // namespace mye::json
