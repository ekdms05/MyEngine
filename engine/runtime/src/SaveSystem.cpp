// mye/runtime/SaveSystem.cpp — 세이브/로드(슬롯·버전 헤더·ISaveParticipant) 구현 (M6-A)
//
// 파일 포맷(UTF-8 JSON 단일 오브젝트):
//   { "__header": { magic, version, timestampUnix, playTimeSec, title, thumbnailVpath },
//     "__payload": { "<sectionId>": {<참여자 JsonArchive Root>}, ... } }
//
// 원자적 쓰기: <root>/<saveDir>/<name>.save.tmp 로 전량 기록 후 최종 경로로 rename(교체).
//   부분 파일/크래시로 인한 손상 세이브를 방지한다(temp→rename 규약, docs/06).
//
// 경로 규약(동결): user://<saveDir>/{settings|quicksave|slotN}.save.
//   VFS 는 읽기 전용(마운트·우선순위) 이라 쓰기·삭제는 OS 파일 경로로 직접 수행한다.
//   user:// 마운트의 OS 루트를 SaveSystem 이 보유(SetUserRootDir 로 주입, 기본 = cwd/user).
//   존재 확인·읽기는 VFS 가 마운트돼 있으면 VFS, 아니면 OS 폴백 — 둘 다 동일 OS 경로를 본다.
//
// 마이그레이션: 헤더 version < kCurrentVersion 이면 FromVersion 오름차순 체인으로 payload JSON
//   문자열을 단계적 승격 후 참여자에게 Load 디스패치. version > kCurrentVersion 은 VersionMismatch.
#include "mye/runtime/SaveSystem.h"

#include "mye/asset/FileSystem.h"
#include "mye/core/Json.h"
#include "mye/ser/JsonArchive.h"
#include "mye/ser/Serialize.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace mye::runtime {
namespace {

namespace fs = std::filesystem;

// UTF-8 std::string → std::filesystem::path (u8 경계). Windows 에서 와이드 변환.
fs::path Utf8ToPath(std::string_view utf8) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

// OS 파일 전량 읽기(UTF-8). 실패/부재 시 Error.
Expected<std::string, Error> ReadWholeFile(const fs::path& p) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec))
        return MakeError(RuntimeError::NotFound, "세이브 파일 없음");
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return MakeError(RuntimeError::IoFailed, "세이브 파일 열기 실패");
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad())
        return MakeError(RuntimeError::IoFailed, "세이브 파일 읽기 실패");
    if (data.starts_with("\xEF\xBB\xBF")) data.erase(0, 3);   // UTF-8 BOM
    return data;
}

// 원자적 쓰기: temp 파일에 전량 기록 → fsync 대용 flush/close → 최종 경로로 rename.
Expected<void, Error> WriteFileAtomic(const fs::path& finalPath, std::string_view bytes) {
    std::error_code ec;
    fs::create_directories(finalPath.parent_path(), ec);   // 실패(이미 존재)는 무시

    fs::path tmp = finalPath;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return MakeError(RuntimeError::IoFailed, "세이브 temp 생성 실패");
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out)
            return MakeError(RuntimeError::IoFailed, "세이브 temp 쓰기 실패");
    } // ofstream 소멸 = close(핸들 해제 후 rename 가능)

    // rename 은 대상이 이미 있으면 실패할 수 있어(Windows) 먼저 제거 후 교체.
    fs::remove(finalPath, ec);
    fs::rename(tmp, finalPath, ec);
    if (ec) {
        fs::remove(tmp, ec);   // 실패 시 temp 정리
        return MakeError(RuntimeError::IoFailed, "세이브 원자적 교체(rename) 실패");
    }
    return Expected<void, Error>{};
}

// ---- 헤더 ↔ json::Value ----
json::Value HeaderToJson(const SaveHeader& h) {
    json::Value::Object o;
    o.emplace("magic", json::Value(static_cast<int64_t>(h.magic)));
    o.emplace("version", json::Value(static_cast<int64_t>(h.version)));
    o.emplace("timestampUnix", json::Value(static_cast<int64_t>(h.timestampUnix)));
    o.emplace("playTimeSec", json::Value(h.playTimeSec));
    o.emplace("title", json::Value(h.title));
    o.emplace("thumbnailVpath", json::Value(h.thumbnailVpath));
    return json::Value(std::move(o));
}

Expected<SaveHeader, Error> HeaderFromJson(const json::Value& v) {
    if (!v.IsObject())
        return MakeError(RuntimeError::ParseFailed, "세이브 헤더가 오브젝트가 아님");
    SaveHeader h;
    if (const auto* p = v.Find("magic"))          h.magic = static_cast<uint32_t>(p->AsInt());
    if (const auto* p = v.Find("version"))        h.version = static_cast<uint32_t>(p->AsInt());
    if (const auto* p = v.Find("timestampUnix"))  h.timestampUnix = p->AsInt();
    if (const auto* p = v.Find("playTimeSec"))    h.playTimeSec = p->AsDouble();
    if (const auto* p = v.Find("title"))          h.title = std::string(p->AsString());
    if (const auto* p = v.Find("thumbnailVpath")) h.thumbnailVpath = std::string(p->AsString());
    if (h.magic != SaveHeader{}.magic)
        return MakeError(RuntimeError::ParseFailed, "세이브 매직 불일치");
    return h;
}

} // namespace

struct SaveSystem::Impl {
    asset::VirtualFileSystem* vfs = nullptr;
    std::string saveDir = "saves";
    std::string userRootDir;                                 // user:// OS 루트(기본 cwd/user)
    std::vector<ISaveParticipant*> participants;             // 비소유
    std::vector<std::unique_ptr<ISaveMigration>> migrations; // 소유, FromVersion 오름차순

    // 슬롯 → user:// vpath("user://saves/slot0.save").
    std::string SlotVpath(SlotId slot) const {
        std::string name;
        if (slot == kSettingsSlot)       name = "settings";
        else if (slot == kQuicksaveSlot) name = "quicksave";
        else                             name = "slot" + std::to_string(slot.index);
        return "user://" + saveDir + "/" + name + ".save";
    }

    // 슬롯 → OS 절대 경로(쓰기·삭제·OS 폴백 읽기용). userRootDir/saveDir/name.save.
    fs::path SlotOsPath(SlotId slot) const {
        std::string name;
        if (slot == kSettingsSlot)       name = "settings";
        else if (slot == kQuicksaveSlot) name = "quicksave";
        else                             name = "slot" + std::to_string(slot.index);
        fs::path root = userRootDir.empty()
            ? (fs::current_path() / "user")
            : Utf8ToPath(userRootDir);
        return root / Utf8ToPath(saveDir) / Utf8ToPath(name + ".save");
    }

    // 슬롯 파일 텍스트 읽기(VFS 우선, 없으면 OS 폴백).
    Expected<std::string, Error> ReadSlotText(SlotId slot) const {
        if (vfs && vfs->Exists(SlotVpath(slot))) {
            auto blob = vfs->ReadAll(SlotVpath(slot));
            if (blob) {
                const auto& b = blob.Value();
                std::string s(reinterpret_cast<const char*>(b.data()), b.size());
                if (s.starts_with("\xEF\xBB\xBF")) s.erase(0, 3);
                return s;
            }
        }
        return ReadWholeFile(SlotOsPath(slot));
    }
};

SaveSystem::SaveSystem() : m_impl(std::make_unique<Impl>()) {}
SaveSystem::~SaveSystem() = default;

void SaveSystem::Initialize(asset::VirtualFileSystem* vfs, std::string saveDir) {
    m_impl->vfs = vfs;
    m_impl->saveDir = std::move(saveDir);
}
void SaveSystem::Shutdown() {
    m_impl->participants.clear();
    m_impl->migrations.clear();
    m_impl->vfs = nullptr;
}

void SaveSystem::SetUserRootDir(std::string osDir) {
    m_impl->userRootDir = std::move(osDir);
}

void SaveSystem::RegisterParticipant(ISaveParticipant* participant) {
    if (!participant) return;
    auto& v = m_impl->participants;
    if (std::find(v.begin(), v.end(), participant) == v.end()) v.push_back(participant);
}
void SaveSystem::UnregisterParticipant(ISaveParticipant* participant) {
    auto& v = m_impl->participants;
    v.erase(std::remove(v.begin(), v.end(), participant), v.end());
}
void SaveSystem::RegisterMigration(std::unique_ptr<ISaveMigration> migration) {
    if (!migration) return;
    auto& v = m_impl->migrations;
    v.push_back(std::move(migration));
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
        return a->FromVersion() < b->FromVersion();
    });
}

Expected<void, Error> SaveSystem::WriteSlot(SlotId slot, const SaveHeader& header) {
    // 헤더는 항상 현재 코드 포맷 버전으로 기록(호출자 header.version 무시하고 정본으로 고정).
    SaveHeader h = header;
    h.magic = SaveHeader{}.magic;
    h.version = kCurrentVersion;

    // 페이로드: 참여자별 섹션 오브젝트. JsonArchive.ForWrite → Root() 를 섹션 키에 중첩.
    json::Value::Object payload;
    for (ISaveParticipant* p : m_impl->participants) {
        if (!p) continue;
        auto ar = ser::JsonArchive::ForWrite();
        p->Serialize(ar);   // ar.IsReading()==false → 내보내기
        payload.emplace(std::string(p->SectionId()), ar.Root());
    }

    json::Value::Object rootObj;
    rootObj.emplace("__header", HeaderToJson(h));
    rootObj.emplace("__payload", json::Value(std::move(payload)));
    const std::string text = json::Stringify(json::Value(std::move(rootObj)));

    return WriteFileAtomic(m_impl->SlotOsPath(slot), text);
}

Expected<void, Error> SaveSystem::ReadSlot(SlotId slot) {
    auto textRes = m_impl->ReadSlotText(slot);
    if (!textRes) return textRes.GetError();

    auto parsed = json::Parse(textRes.Value());
    if (!parsed)
        return MakeError(RuntimeError::ParseFailed,
                         "세이브 파싱 실패: " + parsed.GetError().message);
    const json::Value& root = parsed.Value();

    const json::Value* headerVal = root.Find("__header");
    if (!headerVal)
        return MakeError(RuntimeError::ParseFailed, "세이브에 __header 없음");
    auto hres = HeaderFromJson(*headerVal);
    if (!hres) return hres.GetError();
    SaveHeader header = hres.Value();

    if (header.version > kCurrentVersion)
        return MakeError(RuntimeError::VersionMismatch,
                         "세이브 버전이 코드보다 최신(마이그레이션 불가)");

    const json::Value* payloadVal = root.Find("__payload");
    if (!payloadVal || !payloadVal->IsObject())
        return MakeError(RuntimeError::ParseFailed, "세이브에 __payload 오브젝트 없음");

    // 마이그레이션 체인: 구버전이면 payload JSON 문자열을 단계적으로 승격.
    //   현재 ver 을 처리하는 마이그레이션(FromVersion==ver)을 반복 탐색·적용해 ver 을 1씩 올린다.
    //   빈 구간(해당 ver 마이그레이션 부재)은 즉시 실패 — 체인이 현재 버전까지 닿지 못함.
    std::string payloadJson = json::Stringify(*payloadVal);
    uint32_t ver = header.version;
    while (ver < kCurrentVersion) {
        ISaveMigration* step = nullptr;
        for (const auto& m : m_impl->migrations) {
            if (m->FromVersion() == ver) { step = m.get(); break; }
        }
        if (!step)
            return MakeError(RuntimeError::VersionMismatch,
                             "버전 " + std::to_string(ver)
                                 + " 을 승격할 마이그레이션이 없음(체인 단절)");
        auto mres = step->Migrate(payloadJson);
        if (!mres) return mres.GetError();
        ++ver;   // 한 단계 승격(마이그레이션은 다음 버전 스키마로 payload 를 변형해야 함)
    }

    // 승격된 payload 를 다시 파싱해 섹션별 참여자에게 Load 디스패치.
    auto payloadParsed = json::Parse(payloadJson);
    if (!payloadParsed)
        return MakeError(RuntimeError::ParseFailed,
                         "마이그레이션 후 payload 파싱 실패: "
                             + payloadParsed.GetError().message);
    const json::Value& payload = payloadParsed.Value();
    if (!payload.IsObject())
        return MakeError(RuntimeError::ParseFailed, "payload 가 오브젝트가 아님");

    for (ISaveParticipant* p : m_impl->participants) {
        if (!p) continue;
        const json::Value* section = payload.Find(std::string(p->SectionId()));
        if (!section) continue;   // 구세이브에 해당 섹션 없음 → 참여자 미호출(스킵 규약)
        auto rd = ser::JsonArchive::ForRead(*section);
        p->Serialize(rd);         // ar.IsReading()==true → 채우기
    }
    return Expected<void, Error>{};
}

Expected<SlotInfo, Error> SaveSystem::ReadHeader(SlotId slot) const {
    SlotInfo info;
    info.slot = slot;
    info.exists = Exists(slot);
    if (!info.exists) return info;

    auto textRes = m_impl->ReadSlotText(slot);
    if (!textRes) { info.exists = false; return info; }
    auto parsed = json::Parse(textRes.Value());
    if (!parsed) return info;   // 손상 → 존재하되 헤더 미해독(기본값)
    const json::Value* headerVal = parsed.Value().Find("__header");
    if (!headerVal) return info;
    auto hres = HeaderFromJson(*headerVal);
    if (hres) info.header = hres.Value();
    return info;
}

std::vector<SlotInfo> SaveSystem::EnumerateSlots(int32_t maxGameSlots) const {
    std::vector<SlotInfo> out;
    for (int32_t i = 0; i < maxGameSlots; ++i) {
        auto r = ReadHeader(SlotId{i});
        if (r) out.push_back(r.Value());
    }
    return out;
}

bool SaveSystem::Exists(SlotId slot) const {
    if (m_impl->vfs && m_impl->vfs->Exists(m_impl->SlotVpath(slot))) return true;
    std::error_code ec;
    return fs::is_regular_file(m_impl->SlotOsPath(slot), ec);
}

Expected<void, Error> SaveSystem::DeleteSlot(SlotId slot) {
    std::error_code ec;
    const fs::path p = m_impl->SlotOsPath(slot);
    if (!fs::exists(p, ec))
        return MakeError(RuntimeError::NotFound, "삭제할 세이브 슬롯 없음");
    if (!fs::remove(p, ec) || ec)
        return MakeError(RuntimeError::IoFailed, "세이브 슬롯 삭제 실패");
    return Expected<void, Error>{};
}

} // namespace mye::runtime
