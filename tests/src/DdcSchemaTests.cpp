// DdcSchemaTests.cpp — 데이터드리븐 컴포넌트: 스키마·인스턴스·직렬화·레지스트리 (엔진 확장성)
//
// 게임이 엔진 재컴파일 없이 컴포넌트/스탯 타입을 데이터로 정의하는 확장점 검증.
#include "TestFramework.h"

#include "mye/ddc/SchemaRegistry.h"
#include "mye/core/Json.h"

#include <cmath>
#include <string>

using namespace mye;
using namespace mye::ddc;

namespace { bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; } }

MYE_TEST(DdcSchemaInstantiateDefaultsAndTypedAccess) {
    // 게임이 자유롭게 정의한 스탯 컴포넌트(고정 str/agi/int/vit 아님).
    ComponentSchema s{"Stats"};
    s.AddField({"hp",     FieldType::I32,    100})
     .AddField({"mp",     FieldType::I32,    50})
     .AddField({"crit",   FieldType::F32,    0, 0.15})
     .AddField({"alive",  FieldType::Bool,   1})
     .AddField({"title",  FieldType::String, 0, 0.0, "새내기"});

    DynamicComponent c = s.Instantiate();
    // 기본값.
    MYE_EXPECT(c.GetInt("hp") == 100 && c.GetInt("mp") == 50);
    MYE_EXPECT(Near(c.GetFloat("crit"), 0.15));
    MYE_EXPECT(c.GetBool("alive"));
    MYE_EXPECT(c.GetString("title") == "새내기");

    // 타입드 set/get.
    MYE_EXPECT(c.SetInt("hp", 70));
    MYE_EXPECT(c.GetInt("hp") == 70);
    MYE_EXPECT(c.SetFloat("crit", 0.5));
    MYE_EXPECT(Near(c.GetFloat("crit"), 0.5));
    MYE_EXPECT(c.SetBool("alive", false));
    MYE_EXPECT(!c.GetBool("alive"));
    MYE_EXPECT(c.SetString("title", "용사"));
    MYE_EXPECT(c.GetString("title") == "용사");

    // 없는 필드/타입 불일치는 안전 실패(false/fallback).
    MYE_EXPECT(!c.SetInt("nope", 1));
    MYE_EXPECT(c.GetInt("nope", -1) == -1);
    MYE_EXPECT(!c.SetString("hp", "x"));       // hp 는 문자열 아님
    MYE_EXPECT(!c.SetBool("crit", true));      // crit 는 bool 아님

    // 숫자 관용 변환(int 필드에 float set → 절삭, float 필드 int 조회).
    MYE_EXPECT(c.SetFloat("hp", 42.9));        // i32 에 float → 42
    MYE_EXPECT(c.GetInt("hp") == 42);
}

MYE_TEST(DdcSchemaJsonRoundtrip) {
    const char* json = R"({
        "name": "Monster",
        "fields": [
            { "name": "hp",    "type": "i32",    "default": 30 },
            { "name": "speed", "type": "f32",    "default": 1.5 },
            { "name": "boss",  "type": "bool",   "default": true },
            { "name": "name",  "type": "string", "default": "슬라임" }
        ]
    })";
    auto parsed = mye::json::Parse(json);
    MYE_EXPECT(static_cast<bool>(parsed));

    auto s = ComponentSchema::FromJson(parsed.Value());
    MYE_EXPECT(static_cast<bool>(s));
    const ComponentSchema& schema = s.Value();
    MYE_EXPECT(schema.Name() == "Monster");
    MYE_EXPECT(schema.Fields().size() == 4);
    MYE_EXPECT(schema.Id() == HashFnv1a64("Monster"));

    DynamicComponent c = schema.Instantiate();
    MYE_EXPECT(c.GetInt("hp") == 30);
    MYE_EXPECT(Near(c.GetFloat("speed"), 1.5));
    MYE_EXPECT(c.GetBool("boss"));
    MYE_EXPECT(c.GetString("name") == "슬라임");

    // ToJson → FromJson 왕복 보존.
    mye::json::Value out = schema.ToJson();
    auto s2 = ComponentSchema::FromJson(out);
    MYE_EXPECT(static_cast<bool>(s2));
    MYE_EXPECT(s2.Value().Fields().size() == 4);
    DynamicComponent c2 = s2.Value().Instantiate();
    MYE_EXPECT(c2.GetInt("hp") == 30 && c2.GetString("name") == "슬라임");

    // 알 수 없는 타입 → 오류.
    auto bad = mye::json::Parse(R"({ "name":"X", "fields":[{"name":"f","type":"vec3"}] })");
    MYE_EXPECT(!ComponentSchema::FromJson(bad.Value()));
}

MYE_TEST(DdcRegistryLoadAndInstantiate) {
    const char* json = R"([
        { "name": "Health", "fields": [ { "name":"cur","type":"i32","default":10 }, { "name":"max","type":"i32","default":10 } ] },
        { "name": "Position","fields": [ { "name":"x","type":"f32" }, { "name":"y","type":"f32" } ] }
    ])";
    auto parsed = mye::json::Parse(json);
    MYE_EXPECT(static_cast<bool>(parsed));

    SchemaRegistry reg;
    auto n = reg.LoadFromJson(parsed.Value());
    MYE_EXPECT(static_cast<bool>(n) && n.Value() == 2);
    MYE_EXPECT(reg.Count() == 2);
    MYE_EXPECT(reg.Find("Health") != nullptr);
    MYE_EXPECT(reg.Find(HashFnv1a64("Position")) != nullptr);
    MYE_EXPECT(reg.Find("Missing") == nullptr);

    // 중복 등록 거부.
    ComponentSchema dup{"Health"};
    MYE_EXPECT(!reg.Register(dup));

    // 레지스트리로 인스턴스 생성.
    DynamicComponent h = reg.Instantiate("Health");
    MYE_EXPECT(h.Schema() != nullptr);
    MYE_EXPECT(h.GetInt("max") == 10);
    MYE_EXPECT(h.SetInt("cur", 3) && h.GetInt("cur") == 3);

    // 스키마 포인터 안정성: 등록 후 추가 등록해도 기존 인스턴스 유효.
    ComponentSchema more{"Mana"};
    more.AddField({"cur", FieldType::I32, 5});
    MYE_EXPECT(static_cast<bool>(reg.Register(more)));
    MYE_EXPECT(h.GetInt("cur") == 3);   // 기존 인스턴스 여전히 유효

    // 없는 스키마 인스턴스 = 빈 컴포넌트(안전).
    DynamicComponent none = reg.Instantiate("Nope");
    MYE_EXPECT(none.Schema() == nullptr);
    MYE_EXPECT(!none.SetInt("x", 1) && none.GetInt("x", 9) == 9);
}
