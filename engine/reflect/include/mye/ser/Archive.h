// mye/ser/Archive.h — 대칭 직렬화 인터페이스 + 스트림 (docs/04 §직렬화)
//
// 하나의 리플렉션 워크를 두 아카이브가 소비한다. M3-A는 **JsonArchive 우선**(에디터·.meta·
// 프리팹·씬 — VCS diff 친화). BinaryArchive(캐시·pak·세이브)는 계약만 전방선언하고 구현은 후속.
//
// 대칭성: IsReading()으로 방향을 분기하되, 리플렉션 워커는 Key/Value/Begin*/End* 를 방향과
//   무관하게 호출한다(읽기면 채우고, 쓰기면 내보낸다). 사용자 코드는 보통 직접 안 쓴다.
//
// 파일/메모리 대칭: 두 아카이브 모두 MemoryStream을 대상으로 생성 가능 —
//   07 플레이 모드 인메모리 스냅샷·Undo 값 blob이 메모리 경로의 공식 소비자.
#pragma once

#include "mye/core/Base.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mye::asset { struct AssetRef; }   // IArchive::Value(AssetRef&) 특별 취급

namespace mye::ser {

// 성장 가능한 인메모리 버퍼(읽기 커서 + 쓰기 append). BinaryArchive·인메모리 JSON 대상.
class MemoryStream {
public:
    MemoryStream() = default;
    explicit MemoryStream(std::vector<std::byte> initial) : m_buffer(std::move(initial)) {}

    // 쓰기 — 끝에 append.
    void Write(const void* data, std::size_t size);
    // 읽기 — 커서에서 size 바이트. 부족하면 Error.
    Expected<void, Error> Read(void* out, std::size_t size);

    std::span<const std::byte> Bytes() const { return m_buffer; }
    std::size_t Size() const { return m_buffer.size(); }
    std::size_t Cursor() const { return m_cursor; }
    void Seek(std::size_t pos) { m_cursor = pos; }
    void Clear() { m_buffer.clear(); m_cursor = 0; }

private:
    std::vector<std::byte> m_buffer;
    std::size_t            m_cursor = 0;
};

// 읽기/쓰기 대칭 인터페이스. 리플렉션 워커가 호출하는 프리미티브.
class IArchive {
public:
    virtual ~IArchive() = default;

    virtual bool IsReading() const = 0;

    // 다음 Value/Begin* 가 속할 필드 키를 지정(오브젝트 내부에서). 배열 원소엔 불필요.
    virtual void Key(std::string_view name) = 0;

    // 프리미티브 값 — 읽기면 채우고 쓰기면 내보낸다. 정수는 i64/부동은 f64로 협의.
    virtual void Value(bool& v) = 0;
    virtual void Value(std::int64_t& v) = 0;
    virtual void Value(double& v) = 0;
    virtual void Value(std::string& v) = 0;

    // AssetRef 특별 취급 — 직렬화기가 의존성 목록에 자동 수집(로드·쿡 포함 판단).
    virtual void Value(mye::asset::AssetRef& v) = 0;

    // 오브젝트 프레이밍. version은 쓰기 시 in, 읽기 시 out(구버전 마이그레이션 트리거).
    virtual void BeginObject(std::string_view typeName, std::uint32_t& version) = 0;
    virtual void EndObject() = 0;

    // 배열 프레이밍. count는 쓰기 시 in(원소 수), 읽기 시 out(리사이즈 근거).
    virtual void BeginArray(std::size_t& count) = 0;
    virtual void EndArray() = 0;

    // 읽기 중 현재 오브젝트에 해당 키가 존재하는지(기본값 주입·미지 필드 판정용).
    //   쓰기 아카이브는 항상 false 반환 가능.
    virtual bool HasKey(std::string_view name) const = 0;

    // 마지막 프리미티브/오브젝트 연산이 성공했는지(누적 에러 상태). false면 이후 무시.
    virtual bool Ok() const = 0;
};

} // namespace mye::ser
