// Serialize.cpp — 리플렉션 기반 제네릭 직렬화 워커 (04 §직렬화)
//
// SerializeDynamic 이 refl::TypeInfo 를 재귀적으로 걷어 IArchive 프리미티브를 호출한다.
//   - Primitive : Kind 별 프리미티브 Value() 호출(정수는 i64, 부동은 f64로 협의)
//   - Enum      : 하부 정수 ↔ 이름 문자열 왕복(EnumInfo 테이블)
//   - Struct    : 필드 순회(Key + 재귀). 커스텀 훅이 있으면 그걸 우선.
//   - Vector    : BeginArray/EndArray + 원소 재귀(읽기 시 count 로 리사이즈)
//   - AssetRef  : 아카이브의 AssetRef 특별 취급 경로로 위임(의존성 수집)
//
// Serialize<T> 템플릿(헤더 선언)의 명시적 인스턴스는 소비자가 자기 타입으로 만든다.
#include "mye/ser/Serialize.h"

#include "mye/refl/TypeId.h"
#include "mye/refl/TypeInfo.h"
#include "mye/refl/TypeRegistry.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace mye::asset { struct AssetRef; }

namespace mye::ser {

using refl::Kind;
using refl::TypeInfo;
using refl::EnumInfo;

namespace {

// 원시형 하나를 아카이브에 왕복. TypeInfo::Name() 으로 C++ 하부 표현을 판정.
void SerializePrimitive(IArchive& ar, const TypeInfo& t, void* obj) {
    const std::string_view n = t.Name();
    if (n == "bool") {
        ar.Value(*static_cast<bool*>(obj));
    } else if (n == "f32") {
        float* f = static_cast<float*>(obj);
        double d = static_cast<double>(*f);
        ar.Value(d);
        if (ar.IsReading()) *f = static_cast<float>(d);
    } else if (n == "f64") {
        ar.Value(*static_cast<double*>(obj));
    } else if (n == "string") {
        ar.Value(*static_cast<std::string*>(obj));
    } else {
        // 정수 계열 — 하부 바이트폭·부호를 이름으로 판정해 i64 로 왕복.
        std::int64_t v = 0;
        auto load = [&](auto* p) { v = static_cast<std::int64_t>(*p); };
        auto store = [&](auto* p) { *p = static_cast<std::remove_reference_t<decltype(*p)>>(v); };
        if      (n == "i8")  { auto* p = static_cast<std::int8_t*>(obj);   load(p); ar.Value(v); if (ar.IsReading()) store(p); }
        else if (n == "i16") { auto* p = static_cast<std::int16_t*>(obj);  load(p); ar.Value(v); if (ar.IsReading()) store(p); }
        else if (n == "i32") { auto* p = static_cast<std::int32_t*>(obj);  load(p); ar.Value(v); if (ar.IsReading()) store(p); }
        else if (n == "i64") { auto* p = static_cast<std::int64_t*>(obj);  load(p); ar.Value(v); if (ar.IsReading()) store(p); }
        else if (n == "u8")  { auto* p = static_cast<std::uint8_t*>(obj);  load(p); ar.Value(v); if (ar.IsReading()) store(p); }
        else if (n == "u16") { auto* p = static_cast<std::uint16_t*>(obj); load(p); ar.Value(v); if (ar.IsReading()) store(p); }
        else if (n == "u32") { auto* p = static_cast<std::uint32_t*>(obj); load(p); ar.Value(v); if (ar.IsReading()) store(p); }
        else if (n == "u64") { auto* p = static_cast<std::uint64_t*>(obj); load(p); ar.Value(v); if (ar.IsReading()) store(p); }
    }
}

// enum — 하부 정수를 읽어 이름 문자열로, 또는 이름을 정수로. 하부 폭은 Size() 로 판정.
void SerializeEnum(IArchive& ar, const TypeInfo& t, void* obj) {
    const EnumInfo* e = t.AsEnum();
    if (!e) return;

    // 하부 정수 로드(부호 무관 — enum 값 매칭은 정확 비교).
    auto readUnderlying = [&]() -> std::int64_t {
        std::int64_t v = 0;
        std::memcpy(&v, obj, t.Size() <= sizeof(v) ? t.Size() : sizeof(v));
        // 부호 확장(작은 폭의 음수 처리) — 최상위 비트 판정.
        if (t.Size() < sizeof(std::int64_t)) {
            const std::int64_t signBit = std::int64_t(1) << (t.Size() * 8 - 1);
            if (v & signBit) v |= ~((std::int64_t(1) << (t.Size() * 8)) - 1);
        }
        return v;
    };
    auto writeUnderlying = [&](std::int64_t v) {
        std::memcpy(obj, &v, t.Size() <= sizeof(v) ? t.Size() : sizeof(v));
    };

    if (ar.IsReading()) {
        std::string name;
        ar.Value(name);
        std::int64_t v = 0;
        if (e->ValueOf(name, v)) {
            writeUnderlying(v);
        } else {
            // 이름 매칭 실패 — 숫자 문자열 폴백(견고성). 실패면 0 유지.
            char* end = nullptr;
            long long parsed = std::strtoll(name.c_str(), &end, 10);
            if (end && *end == '\0') writeUnderlying(static_cast<std::int64_t>(parsed));
        }
    } else {
        std::int64_t v = readUnderlying();
        std::string_view name;
        std::string out;
        if (e->NameOf(v, name)) out.assign(name.begin(), name.end());
        else                    out = std::to_string(v);   // 미등록 값 — 숫자 문자열
        ar.Value(out);
    }
}

} // namespace

Expected<void, Error> SerializeDynamic(IArchive& ar, const TypeInfo& type, void* obj) {
    if (!obj) return Error{"SerializeDynamic: null instance", 1};

    switch (type.GetKind()) {
    case Kind::Primitive:
        SerializePrimitive(ar, type, obj);
        break;

    case Kind::Enum:
        SerializeEnum(ar, type, obj);
        break;

    case Kind::AssetRef:
        ar.Value(*static_cast<mye::asset::AssetRef*>(obj));
        break;

    case Kind::Vector: {
        const TypeInfo* elem = type.ElementType();
        if (!elem) return Error{"SerializeDynamic: vector without element type", 2};
        std::size_t count = ar.IsReading() ? 0 : type.VectorSize(obj);
        ar.BeginArray(count);
        if (ar.IsReading()) type.VectorResize(obj, count);
        for (std::size_t i = 0; i < count; ++i) {
            void* e = type.VectorElementAt(obj, i);
            auto r = SerializeDynamic(ar, *elem, e);
            if (!r) { ar.EndArray(); return r; }
        }
        ar.EndArray();
        break;
    }

    case Kind::Struct: {
        // 쓰기: 현재 버전을 내보낸다. 읽기: BeginObject 가 데이터의 버전을 version 으로 채운다.
        std::uint32_t version = type.Version();
        ar.BeginObject(type.Name(), version);
        // 커스텀 직렬화 훅이 있으면 기본 필드 순회를 대신한다.
        if (auto fn = type.CustomSerialize()) {
            fn(ar, obj);
        } else {
            const bool reading = ar.IsReading();
            for (const auto& f : type.Fields()) {
                std::string_view key = f.Name();
                if (reading) {
                    // 버전드 rename 폴백: 현재 이름 키가 없고 구버전(version < RenamedSince)
                    //   데이터면 옛 이름 키로 읽는다. 둘 다 없으면 기본값 유지(읽기 skip).
                    if (!ar.HasKey(key) && !f.RenamedFrom().empty() &&
                        version < f.RenamedSince() && ar.HasKey(f.RenamedFrom())) {
                        key = f.RenamedFrom();
                    }
                    // 어떤 키도 없으면 필드를 건너뛴다(기본값 주입 = 인스턴스 초기값 유지).
                    if (!ar.HasKey(key)) continue;
                }
                ar.Key(key);
                auto r = SerializeDynamic(ar, f.Type(), f.GetPtr(obj));
                if (!r) { ar.EndObject(); return r; }
            }
        }
        ar.EndObject();
        break;
    }
    }

    if (!ar.Ok()) return Error{"SerializeDynamic: archive error", 3};
    return {};
}

} // namespace mye::ser
