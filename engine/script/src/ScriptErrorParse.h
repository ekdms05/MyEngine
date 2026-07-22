// mye/script/src/ScriptErrorParse.h — Lua 에러 메시지 파싱(내부 전용, 비공개 표면)
//
// Lua/sol2 에러 메시지는 관례적으로 "chunkname:line: message" 형식이다(traceback 이 붙기도).
//   에디터 콘솔이 파일·라인 링크를 만들 수 있도록 file/line/message 로 분해한다. 실패 시
//   line=0, file 은 fallback(청크명)으로 둔다. 이 헤더는 mye_script 내부 TU 만 포함한다.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace mye::script::detail {

struct ParsedError {
    std::string file;
    int32_t     line = 0;
    std::string message;   // 원문(traceback 포함) 그대로 유지
};

// raw: sol::error::what() 문자열. fallbackFile: 청크명(파싱 실패 시 사용).
inline ParsedError ParseLuaError(std::string_view raw, std::string_view fallbackFile) {
    ParsedError out;
    out.message.assign(raw.begin(), raw.end());
    out.file.assign(fallbackFile.begin(), fallbackFile.end());
    out.line = 0;

    // 첫 줄에서 "path:line:" 패턴을 찾는다. 경로에 콜론(윈도우 드라이브)이 없다는 가정을
    //   피하려, 오른쪽에서 ": " 앞의 "숫자" 를 라인으로 해석한다.
    // 형태: <file>:<line>: <message...>
    std::string_view s = raw;
    // 첫 줄만 대상.
    size_t nl = s.find('\n');
    std::string_view firstLine = (nl == std::string_view::npos) ? s : s.substr(0, nl);

    // 뒤에서부터 두 번째 콜론을 찾는다: file : line : msg
    size_t secondColon = firstLine.find(": ");
    if (secondColon == std::string_view::npos) return out;
    // secondColon 은 "line: " 의 콜론일 수도, 그 앞이 file:line 일 수도.
    // file:line 부분 = firstLine[0, secondColon)
    std::string_view head = firstLine.substr(0, secondColon);
    size_t lastColon = head.rfind(':');
    if (lastColon == std::string_view::npos) return out;

    std::string_view fileCand = head.substr(0, lastColon);
    std::string_view lineCand = head.substr(lastColon + 1);

    // lineCand 가 전부 숫자면 라인으로 채택.
    if (lineCand.empty()) return out;
    int32_t line = 0;
    for (char c : lineCand) {
        if (c < '0' || c > '9') return out;   // 숫자가 아니면 파싱 실패 → fallback 유지
        line = line * 10 + (c - '0');
    }
    if (!fileCand.empty()) out.file.assign(fileCand.begin(), fileCand.end());
    out.line = line;
    return out;
}

} // namespace mye::script::detail
