// mye/ser/JsonArchive.h — 리플렉션 타입 ↔ json::Value (docs/04 §직렬화 JsonArchive)
//
// 에디터·.meta·프리팹·씬의 VCS diff 친화 포맷. core json::Value를 백엔드로 삼는다
// (04 리플렉션이 core의 최소 파서 위에서 동작 — 순환 의존 없음: reflect는 core 위 L1.5).
//
// 버전 필드: BeginObject가 "__version" 키를 쓰고/읽는다. 읽기 시 구버전이면 필드
//   RenamedFrom 폴백·기본값 주입을 적용(M3-A 최소 마이그레이션). 미지 필드는 경고 후 드롭.
//
// 파일/메모리 대칭: FromValue(읽기)·ToValue(쓰기) 외에 MemoryStream 텍스트 왕복 헬퍼 제공.
#pragma once

#include "mye/core/Json.h"
#include "mye/ser/Archive.h"

#include <memory>
#include <string>

namespace mye::ser {

// json::Value 트리를 대상으로 하는 대칭 아카이브.
//   쓰기: 빈 아카이브에 리플렉션 워크가 값을 채움 → Root()로 결과 json::Value 획득.
//   읽기: 기존 json::Value로 생성 → 리플렉션 워크가 인스턴스를 채움.
class JsonArchive final : public IArchive {
public:
    // 쓰기 모드(빈 트리 생성).
    static JsonArchive ForWrite();
    // 읽기 모드(기존 값 소비). root는 JsonArchive 수명 동안 유효해야 한다.
    static JsonArchive ForRead(const json::Value& root);

    JsonArchive(JsonArchive&&) noexcept;
    JsonArchive& operator=(JsonArchive&&) noexcept;
    ~JsonArchive() override;

    bool IsReading() const override;
    void Key(std::string_view name) override;
    void Value(bool& v) override;
    void Value(std::int64_t& v) override;
    void Value(double& v) override;
    void Value(std::string& v) override;
    void Value(mye::asset::AssetRef& v) override;
    void BeginObject(std::string_view typeName, std::uint32_t& version) override;
    void EndObject() override;
    void BeginArray(std::size_t& count) override;
    void EndArray() override;
    bool HasKey(std::string_view name) const override;
    bool Ok() const override;

    // 쓰기 모드에서 누적된 결과 트리(루트). 읽기 모드에서는 원본 루트를 참조.
    const json::Value& Root() const;

private:
    JsonArchive();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::ser
