// JsonTests.cpp — 코어 내장 최소 JSON 파서·직렬화 검증 (M0-config)
#include "TestFramework.h"

#include "mye/core/Json.h"

#include <string>

using namespace mye;

MYE_TEST(JsonParseScalars) {
    auto t = json::Parse("true");
    MYE_EXPECT(t && t.Value().IsBool() && t.Value().AsBool() == true);

    auto f = json::Parse("  false  ");
    MYE_EXPECT(f && f.Value().AsBool(true) == false);

    auto n = json::Parse("null");
    MYE_EXPECT(n && n.Value().IsNull());

    auto num = json::Parse("42");
    MYE_EXPECT(num && num.Value().IsNumber());
    MYE_EXPECT(num.Value().AsInt() == 42);

    auto s = json::Parse("\"hello\"");
    MYE_EXPECT(s && s.Value().AsString() == std::string_view("hello"));
}

MYE_TEST(JsonParseNumberForms) {
    MYE_EXPECT(json::Parse("0").Value().AsInt() == 0);
    MYE_EXPECT(json::Parse("-0").Value().IsNumber());
    MYE_EXPECT_NEAR(json::Parse("3.25").Value().AsDouble(), 3.25, 1e-12);
    MYE_EXPECT_NEAR(json::Parse("1e3").Value().AsDouble(), 1000.0, 1e-9);
    MYE_EXPECT_NEAR(json::Parse("-1.5e-2").Value().AsDouble(), -0.015, 1e-12);

    // RFC 8259 위반 형태는 거부
    MYE_EXPECT(!json::Parse("01"));
    MYE_EXPECT(!json::Parse("1."));
    MYE_EXPECT(!json::Parse("+1"));
    MYE_EXPECT(!json::Parse(".5"));
    MYE_EXPECT(!json::Parse("1e"));
    MYE_EXPECT(!json::Parse("-"));
}

MYE_TEST(JsonParseStringEscapes) {
    auto esc = json::Parse(R"("a\nb\t\"c\"\\")");
    MYE_EXPECT(esc);
    MYE_EXPECT(esc.Value().AsString() == std::string_view("a\nb\t\"c\"\\"));

    auto uni = json::Parse(R"("A가")");   // A + 가
    MYE_EXPECT(uni);
    MYE_EXPECT(uni.Value().AsString() == std::string_view("A\xEA\xB0\x80"));

    // 서로게이트 페어 (U+1F600)
    auto pair = json::Parse(R"("😀")");
    MYE_EXPECT(pair);
    MYE_EXPECT(pair.Value().AsString() == std::string_view("\xF0\x9F\x98\x80"));

    // UTF-8 원문 통과 (전면 UTF-8 규약)
    auto raw = json::Parse("\"한글 텍스트\"");
    MYE_EXPECT(raw && raw.Value().AsString() == std::string_view("한글 텍스트"));

    // 잘못된 이스케이프·짝 없는 서로게이트는 거부
    MYE_EXPECT(!json::Parse(R"("\x41")"));
    MYE_EXPECT(!json::Parse(R"("\uD83D")"));
    MYE_EXPECT(!json::Parse(R"("\uDE00")"));
    MYE_EXPECT(!json::Parse("\"unterminated"));
}

MYE_TEST(JsonParseStructure) {
    auto r = json::Parse(R"({
        "window": { "width": 1280, "height": 720, "vsync": true },
        "tags": [1, 2.5, "three", null, [true]]
    })");
    MYE_EXPECT(r);
    const json::Value& root = r.Value();
    MYE_EXPECT(root.IsObject());

    const json::Value* window = root.Find("window");
    MYE_EXPECT(window != nullptr && window->IsObject());
    MYE_EXPECT(window->Find("width")->AsInt() == 1280);
    MYE_EXPECT(window->Find("vsync")->AsBool() == true);
    MYE_EXPECT(window->Find("missing") == nullptr);

    const json::Value* tags = root.Find("tags");
    MYE_EXPECT(tags != nullptr && tags->IsArray());
    MYE_EXPECT(tags->AsArray().size() == 5);
    MYE_EXPECT(tags->AsArray()[2].AsString() == std::string_view("three"));
    MYE_EXPECT(tags->AsArray()[3].IsNull());
    MYE_EXPECT(tags->AsArray()[4].AsArray()[0].AsBool() == true);

    // 중복 키는 마지막 승리
    auto dup = json::Parse(R"({"a": 1, "a": 2})");
    MYE_EXPECT(dup && dup.Value().Find("a")->AsInt() == 2);

    // 빈 컨테이너
    MYE_EXPECT(json::Parse("{}") && json::Parse("{}").Value().AsObject().empty());
    MYE_EXPECT(json::Parse("[]") && json::Parse("[]").Value().AsArray().empty());
}

MYE_TEST(JsonParseErrorsReportPosition) {
    auto r = json::Parse("{\n  \"a\": }");
    MYE_EXPECT(!r);
    MYE_EXPECT(r.GetError().message.find("line 2") != std::string::npos);

    auto trailing = json::Parse("{} garbage");
    MYE_EXPECT(!trailing);
    MYE_EXPECT(trailing.GetError().message.find("trailing") != std::string::npos);

    auto empty = json::Parse("");
    MYE_EXPECT(!empty);
    MYE_EXPECT(empty.GetError().message.find("line 1") != std::string::npos);

    MYE_EXPECT(!json::Parse("{\"a\" 1}"));       // ':' 누락
    MYE_EXPECT(!json::Parse("[1, 2"));           // 미종결 배열
    MYE_EXPECT(!json::Parse("{\"a\": 1,}"));     // 트레일링 콤마 거부
}

MYE_TEST(JsonDepthLimit) {
    std::string deep(70, '[');
    deep.append(70, ']');
    auto r = json::Parse(deep);
    MYE_EXPECT(!r);
    MYE_EXPECT(r.GetError().message.find("depth") != std::string::npos);
}

MYE_TEST(JsonStringifyRoundTrip) {
    auto parsed = json::Parse(R"({"s": "한\n글", "i": -7, "d": 0.5, "b": true, "n": null,
                                 "arr": [1, "x"], "obj": {"k": 2}})");
    MYE_EXPECT(parsed);
    const std::string text = json::Stringify(parsed.Value(), 2);

    auto reparsed = json::Parse(text);
    MYE_EXPECT(reparsed);
    const json::Value& v = reparsed.Value();
    MYE_EXPECT(v.Find("s")->AsString() == std::string_view("한\n글"));
    MYE_EXPECT(v.Find("i")->AsInt() == -7);
    MYE_EXPECT_NEAR(v.Find("d")->AsDouble(), 0.5, 1e-12);
    MYE_EXPECT(v.Find("b")->AsBool() == true);
    MYE_EXPECT(v.Find("n")->IsNull());
    MYE_EXPECT(v.Find("arr")->AsArray().size() == 2);
    MYE_EXPECT(v.Find("obj")->Find("k")->AsInt() == 2);

    // 정수는 정수 표기로 직렬화
    MYE_EXPECT(json::Stringify(json::Value(int64_t{5}), 0) == "5");
    MYE_EXPECT(json::Stringify(json::Value(true), 0) == "true");
    MYE_EXPECT(json::Stringify(json::Value(), 0) == "null");
}
