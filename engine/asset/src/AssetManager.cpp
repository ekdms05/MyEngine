// mye/asset/AssetManager.cpp — 슬롯 테이블·임포터 레지스트리·동기 로드 (docs/04 §AssetManager)
//
// M1 슬림 범위:
//  - RegisterImporter: 확장자 → 임포터 매핑(나중 등록이 오버라이드 + 경고).
//  - LoadSync<T>(vpath): 캐시 확인 → VFS ReadAll → 임포터 Import → 슬롯 커밋 → Loaded 핸들.
//  - 슬롯 테이블: index+generation 간접. refCount 0이면 즉시 언로드(지연 GC는 M1-B).
//  - vpath→슬롯 캐시로 중복 로드 공유(같은 경로 재요청 시 refcount만 증가).
//
// GPU 리소스 파괴 경계: 언로드 시 임포터의 DestroyWithDevice(obj, device)로 위임한다.
// 임포터가 Import에서 device로 만든 GPU 핸들을 스스로 device로 반납하므로, AssetManager는
// 타입(Texture 등)을 하드코딩하지 않는다. GPU 리소스가 없는 임포터는 기본 구현이 Destroy만 부른다.
//
// 스레드 안전: 슬롯 테이블(slots/freeSlots/pathToSlot/slotImporter) 조작은 단일스레드 전용이다.
// AssetSlot::refCount/generation은 atomic이나, AllocSlot/DestroySlot/pathToSlot 갱신은 비원자적
// 복합 연산이므로 멀티스레드 로드·릴리스가 동시에 일어나면 자료구조가 경합한다(M1은 단일스레드).
// 멀티스레드 로딩(M1-B/M3)에서는 이 연산들을 뮤텍스로 보호하거나 명령 큐로 직렬화할 것.
#include "mye/asset/AssetManager.h"
#include "mye/asset/Texture.h"
#include "mye/asset/Mesh.h"
#include "mye/asset/AudioClip.h"

#include "mye/core/Events.h"
#include "mye/core/Jobs.h"
#include "mye/core/Log.h"
#include "mye/rhi/Rhi.h"

#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace mye::asset {

namespace {

// 경로에서 소문자 확장자(".png" 등)를 추출. 없으면 빈 문자열.
std::string ExtractExtLower(std::string_view path) {
    // 마지막 '/' 이후에서 '.'을 찾는다(디렉터리의 '.'은 무시).
    size_t slash = path.find_last_of('/');
    size_t searchFrom = (slash == std::string_view::npos) ? 0 : slash + 1;
    size_t dot = path.find_last_of('.');
    if (dot == std::string_view::npos || dot < searchFrom) return {};
    std::string ext(path.substr(dot));
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return ext;
}

} // namespace

struct AssetManager::Impl {
    VirtualFileSystem* vfs = nullptr;
    rhi::IDevice* device = nullptr;

    // ---- 비동기 로딩 배선 (M2-A: 계약 + 스텁) ----
    JobSystem* jobs = nullptr;
    EventBus*  bus = nullptr;
    // 진행 중 비동기 요청 수 — 소멸자가 0이 될 때까지 배수(dangling this 방지).
    std::atomic<int32_t> inFlight{0};
    // 타입 → 비동기 로더. M2-A는 등록만 하고 실제 파이프라인 구동은 후속 구현 에이전트 몫.
    std::unordered_map<AssetTypeId, std::unique_ptr<IAsyncAssetLoader>> asyncLoaders;

    std::vector<std::unique_ptr<IAssetImporter>> importers;
    // 확장자(".png") → importers 인덱스.
    std::unordered_map<std::string, size_t> extToImporter;

    // AssetSlot은 std::atomic 멤버(이동 불가) 때문에 vector 재할당이 불가하다.
    // deque는 요소 주소가 안정적이고 push_back 시 기존 요소를 이동하지 않으므로 적합하다.
    std::deque<AssetSlot> slots;
    std::vector<uint32_t> freeSlots;             // 재사용 대기 인덱스
    // vpath → 슬롯 인덱스(로드 공유). 언로드 시 제거.
    std::unordered_map<std::string, uint32_t> pathToSlot;
    // 슬롯 인덱스 → 그 슬롯을 만든 임포터(언로드 시 Destroy 호출용). null이면 파괴 스킵.
    std::vector<IAssetImporter*> slotImporter;
    // 슬롯 인덱스 → 비동기 로더(async 경로로 만들어진 슬롯의 Unload용). null이면 async 아님.
    std::vector<IAsyncAssetLoader*> slotAsyncLoader;

    IAssetImporter* FindImporterForExt(const std::string& ext) {
        auto it = extToImporter.find(ext);
        return it == extToImporter.end() ? nullptr : importers[it->second].get();
    }

    // 슬롯 확보(신규 또는 free 재사용). 초기 상태 Unloaded.
    uint32_t AllocSlot(const AssetGuid& guid, AssetTypeId type) {
        uint32_t idx;
        if (!freeSlots.empty()) {
            idx = freeSlots.back();
            freeSlots.pop_back();
            AssetSlot& s = slots[idx];
            s.guid = guid;
            s.type = type;
            s.object = nullptr;
            s.refCount.store(0);
            // generation은 재사용 시 이미 증가되어 있음(Free에서 bump).
            s.state = AssetState::Unloaded;
            s.pinned = false;
            slotAsyncLoader[idx] = nullptr;
        } else {
            idx = static_cast<uint32_t>(slots.size());
            slots.emplace_back();
            slotImporter.emplace_back(nullptr);
            slotAsyncLoader.emplace_back(nullptr);
            AssetSlot& s = slots[idx];
            s.guid = guid;
            s.type = type;
            // generation 기본 1(헤더 초기값).
        }
        return idx;
    }

    // 슬롯의 에셋 객체를 파괴하고 슬롯을 free 목록으로 되돌린다(generation bump).
    void DestroySlot(uint32_t idx) {
        AssetSlot& s = slots[idx];
        if (s.object) {
            DestroyAssetObject(idx);
            s.object = nullptr;
        }
        // pathToSlot에서 이 슬롯을 참조하는 항목 제거.
        for (auto it = pathToSlot.begin(); it != pathToSlot.end();) {
            if (it->second == idx) {
                it = pathToSlot.erase(it);
            } else {
                ++it;
            }
        }
        s.state = AssetState::Unloaded;
        s.generation.fetch_add(1);   // dangling 핸들 검출
        slotImporter[idx] = nullptr;
        slotAsyncLoader[idx] = nullptr;
        freeSlots.push_back(idx);
    }

    // GPU 리소스 반납 + CPU 객체 파괴를 임포터에 위임한다(타입 하드코딩 특례 제거).
    // 임포터가 DestroyWithDevice(obj, device)에서 자신이 만든 GPU 핸들을 device로 반납한 뒤
    // CPU 객체를 해제한다. slotImporter가 null이면(임포터 없이 만들어진 Failed 슬롯 등) 스킵.
    void DestroyAssetObject(uint32_t idx) {
        AssetSlot& s = slots[idx];
        if (IAsyncAssetLoader* loader = slotAsyncLoader[idx]) {
            loader->Unload(s.object, device);   // async 경로: 로더가 GPU 반납 책임
        } else if (IAssetImporter* imp = slotImporter[idx]) {
            imp->DestroyWithDevice(s.object, device);
        }
    }
};

AssetManager::AssetManager(VirtualFileSystem& vfs, rhi::IDevice* device)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->vfs = &vfs;
    m_impl->device = device;
}

AssetManager::~AssetManager() {
    // 진행 중 비동기 요청 배수 — 워커/IO 잡이 this(self)를 참조하므로, 그 잡들이 만들어낸
    // 메인 스레드 Finalize 클로저까지 모두 실행돼 in-flight 카운트가 0이 될 때까지 대기한다.
    // (JobSystem은 AssetManager보다 오래 살아야 한다 — 규약. jobs 워커는 아직 살아 있다.)
    if (m_impl->jobs) {
        while (m_impl->inFlight.load() > 0) {
            m_impl->jobs->PumpMainThreadTasks();
            std::this_thread::yield();
        }
        // 마지막으로 남은 메인 태스크(이벤트 등) 한 번 더 소진.
        m_impl->jobs->PumpMainThreadTasks();
    }

    // 남은 로드 객체 정리(테스트·셧다운에서 릭 방지).
    for (uint32_t i = 0; i < m_impl->slots.size(); ++i) {
        if (m_impl->slots[i].object) {
            m_impl->DestroyAssetObject(i);
            m_impl->slots[i].object = nullptr;
        }
    }
}

void AssetManager::RegisterImporter(std::unique_ptr<IAssetImporter> importer) {
    if (!importer) return;
    const size_t idx = m_impl->importers.size();
    IAssetImporter* raw = importer.get();
    m_impl->importers.push_back(std::move(importer));

    for (std::string_view extView : raw->SourceExtensions()) {
        std::string ext(extView);
        for (char& c : ext) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        auto [it, inserted] = m_impl->extToImporter.insert_or_assign(ext, idx);
        if (!inserted) {
            MYE_LOG_WARN("Asset", "RegisterImporter: '{}' overrides existing importer for '{}'",
                         raw->Name(), ext);
        } else {
            MYE_LOG_INFO("Asset", "RegisterImporter: '{}' handles '{}'", raw->Name(), ext);
        }
    }
}

IAssetImporter* AssetManager::FindImporterForPath(std::string_view path) const {
    std::string ext = ExtractExtLower(path);
    if (ext.empty()) return nullptr;
    return m_impl->FindImporterForExt(ext);
}

uint32_t AssetManager::AcquireSlot(const AssetGuid& guid, AssetTypeId type) {
    return m_impl->AllocSlot(guid, type);
}

AssetSlot* AssetManager::GetSlot(uint32_t index, uint32_t generation) {
    if (index >= m_impl->slots.size()) return nullptr;
    AssetSlot& s = m_impl->slots[index];
    if (s.generation.load() != generation) return nullptr;   // 재사용된 슬롯 — dangling
    return &s;
}

void AssetManager::RetainSlot(uint32_t index, uint32_t generation) {
    AssetSlot* s = GetSlot(index, generation);
    if (s) s->refCount.fetch_add(1);
}

void AssetManager::ReleaseSlot(uint32_t index, uint32_t generation) {
    AssetSlot* s = GetSlot(index, generation);
    if (!s) return;
    const int32_t prev = s->refCount.fetch_sub(1);
    if (prev <= 1 && !s->pinned) {
        // refCount 0 도달 — M1 슬림은 즉시 언로드(지연 GC 예산화는 M1-B).
        m_impl->DestroySlot(index);
    }
}

template <typename T>
AssetHandle<T> AssetManager::LoadSync(std::string_view vpath) {
    // 캐시 히트: 기존 슬롯의 핸들을 반환(refcount는 핸들 생성자에서 증가).
    std::string key(vpath);
    if (auto it = m_impl->pathToSlot.find(key); it != m_impl->pathToSlot.end()) {
        const uint32_t idx = it->second;
        return AssetHandle<T>(this, idx, m_impl->slots[idx].generation.load());
    }

    IAssetImporter* importer = FindImporterForPath(vpath);
    if (!importer) {
        MYE_LOG_ERROR("Asset", "LoadSync: no importer for '{}'", key);
        // Failed 슬롯을 만들어 상태 조회가 가능한 핸들을 돌려준다.
        const uint32_t idx = AcquireSlot(AssetGuid::Generate(), T::kAssetTypeId);
        m_impl->slots[idx].state = AssetState::Failed;
        m_impl->pathToSlot.emplace(key, idx);
        return AssetHandle<T>(this, idx, m_impl->slots[idx].generation.load());
    }

    auto blob = m_impl->vfs->ReadAll(vpath);
    if (!blob) {
        MYE_LOG_ERROR("Asset", "LoadSync: read failed '{}': {}", key, blob.GetError().message);
        const uint32_t idx = AcquireSlot(AssetGuid::Generate(), importer->ProducedType());
        m_impl->slots[idx].state = AssetState::Failed;
        m_impl->pathToSlot.emplace(key, idx);
        return AssetHandle<T>(this, idx, m_impl->slots[idx].generation.load());
    }

    ImportContext ctx;
    ctx.sourcePath = vpath;
    ctx.sourceBytes = std::move(blob.Value());
    ctx.device = m_impl->device;
    ctx.settings = nullptr;   // M1 슬림: .meta settings 주입은 M1-B(임포트 설정 UI와 함께).

    auto imported = importer->Import(ctx);

    const uint32_t idx = AcquireSlot(AssetGuid::Generate(), importer->ProducedType());
    m_impl->pathToSlot.emplace(key, idx);
    AssetSlot& slot = m_impl->slots[idx];

    if (!imported) {
        MYE_LOG_ERROR("Asset", "LoadSync: import failed '{}': {}", key,
                      imported.GetError().message);
        slot.state = AssetState::Failed;
        return AssetHandle<T>(this, idx, slot.generation.load());
    }

    slot.object = imported.Value();
    slot.state = AssetState::Loaded;
    m_impl->slotImporter[idx] = importer;
    MYE_LOG_INFO("Asset", "LoadSync: loaded '{}' (slot {})", key, idx);
    return AssetHandle<T>(this, idx, slot.generation.load());
}

// ---- 비동기 로딩 (M2-A 구현) --------------------------------------------------
// 파이프라인: LoadAsync(메인) → 슬롯 즉시 확보(Queued) → IO 잡(VFS 블롭 읽기) →
//   Compute 잡(loader->Parse, 순수 CPU) → RunOnMainThread(loader->Finalize + 슬롯 커밋 +
//   AssetLoadedEvent). 슬롯 테이블 조작은 전부 메인 스레드(Finalize 클로저) — 워커/IO는
//   블롭·로더 Parse(순수)만 만진다. 잡 시스템·EventBus 미배선이면 동기 폴백.
//
// 스레드 안전: 요청 문맥(AsyncRequest)은 shared_ptr로 잡 클로저들이 공유한다. 슬롯 커밋 전
//   핸들이 릴리스돼 슬롯이 재사용될 수 있으므로, Finalize에서 generation을 재검증해 stale
//   커밋을 막는다(재사용됐으면 만든 객체를 Unload로 폐기).

void AssetManager::RegisterAsyncLoader(std::unique_ptr<IAsyncAssetLoader> loader) {
    if (!loader) return;
    const AssetTypeId type = loader->Type();
    if (m_impl->asyncLoaders.count(type)) {
        MYE_LOG_WARN("Asset", "RegisterAsyncLoader: overriding loader for type {}", type);
    }
    m_impl->asyncLoaders[type] = std::move(loader);
}

void AssetManager::SetAsyncServices(JobSystem* jobs, EventBus* bus) {
    m_impl->jobs = jobs;
    m_impl->bus = bus;
}

namespace {

// 한 건의 비동기 로드 요청 문맥. 잡 클로저들이 shared_ptr로 공유.
struct AsyncRequest {
    std::string         vpath;
    uint32_t            slot = 0;
    uint32_t            generation = 0;   // 커밋 시 재검증(슬롯 재사용 검출)
    AssetGuid           guid;
    AssetTypeId         type = 0;
    IAsyncAssetLoader*  loader = nullptr;
    LoadContext         loadCtx;          // Parse가 하드/소프트 의존성을 기록
    ParsedAsset         parsed;           // Parse 산출물(워커→메인 이송)
};

} // namespace

// 슬롯 커밋(메인 스레드 전용) — Finalize 결과 또는 실패를 슬롯에 반영하고 이벤트 발행.
// generation 불일치(핸들 릴리스로 슬롯 재사용됨)면 만든 객체를 폐기하고 커밋 스킵.
void AssetManager::CommitAsync(void* reqRaw, void* objectOrNull, bool success) {
    auto* req = static_cast<AsyncRequest*>(reqRaw);
    AssetSlot* s = GetSlot(req->slot, req->generation);

    if (!s) {
        // 슬롯이 이미 재사용됨 — 이번 로드는 버려진다. 만든 객체가 있으면 반납.
        if (objectOrNull && req->loader) req->loader->Unload(objectOrNull, m_impl->device);
        MYE_LOG_INFO("Asset", "LoadAsync: '{}' committed to stale slot — discarded", req->vpath);
        m_impl->inFlight.fetch_sub(1);
        return;
    }

    if (success && objectOrNull) {
        s->object = objectOrNull;
        s->state = AssetState::Loaded;
        m_impl->slotAsyncLoader[req->slot] = req->loader;
        MYE_LOG_INFO("Asset", "LoadAsync: loaded '{}' (slot {})", req->vpath, req->slot);
    } else {
        s->state = AssetState::Failed;
        if (objectOrNull && req->loader) req->loader->Unload(objectOrNull, m_impl->device);
        MYE_LOG_ERROR("Asset", "LoadAsync: failed '{}'", req->vpath);
    }

    if (m_impl->bus) {
        AssetLoadedEvent ev;
        ev.guid = req->guid;
        ev.type = req->type;
        ev.success = (s->state == AssetState::Loaded);
        m_impl->bus->Enqueue(ev);   // trivially copyable — 스레드 무관하나 여기선 메인.
    }
    m_impl->inFlight.fetch_sub(1);
}

void AssetManager::Update() {
    // 메인 스레드 작업(Finalize 클로저 등) 소진. 지연 GC 예산화는 M1-B.
    if (m_impl->jobs) m_impl->jobs->PumpMainThreadTasks();
}

// ---- 핫 리로드(M3-A): 소스 재임포트 → 슬롯 object in-place 스왑 --------------
// 메인 스레드 전용(슬롯 테이블 write). 캐시에 없는 vpath 는 스왑 없이 false 반환(로드된 적 없음).
// 임포트 실패 시 기존 object 를 그대로 두어 게임이 이전 데이터로 계속 동작하게 한다.
AssetManager::ReimportResult AssetManager::ReimportPath(std::string_view vpath) {
    ReimportResult result;
    const std::string key(vpath);

    auto it = m_impl->pathToSlot.find(key);
    if (it == m_impl->pathToSlot.end()) {
        // 아직 로드된 적 없는 소스 — 리로드 대상 아님(다음 로드 때 새로 임포트됨).
        return result;
    }
    const uint32_t idx = it->second;
    AssetSlot& slot = m_impl->slots[idx];
    result.guid = slot.guid;
    result.type = slot.type;

    // 이 슬롯을 만든 임포터(동기 경로) 우선. async 로더로 만든 슬롯은 M3-A 스왑 미지원(로그).
    IAssetImporter* importer = m_impl->slotImporter[idx];
    if (!importer) {
        // 확장자로 재조회(Failed 슬롯에서 처음 임포터가 없었던 경우 등).
        importer = FindImporterForPath(vpath);
    }
    if (!importer) {
        MYE_LOG_WARN("Asset", "ReimportPath: no importer for '{}' — skip swap", key);
        return result;
    }

    auto blob = m_impl->vfs->ReadAll(vpath);
    if (!blob) {
        MYE_LOG_ERROR("Asset", "ReimportPath: read failed '{}': {}", key, blob.GetError().message);
        return result;
    }

    ImportContext ctx;
    ctx.sourcePath = vpath;
    ctx.sourceBytes = std::move(blob.Value());
    ctx.device = m_impl->device;
    ctx.settings = nullptr;   // .meta settings 주입은 M3-B(임포트 설정 UI).

    auto imported = importer->Import(ctx);
    if (!imported) {
        MYE_LOG_ERROR("Asset", "ReimportPath: import failed '{}': {} (keeping old data)", key,
                      imported.GetError().message);
        return result;
    }

    // in-place 스왑: 새 객체 커밋 후 기존 객체 파괴. generation 은 유지(핸들 유효).
    // 기존 객체는 그것을 만든 경로(async 로더 or 동기 임포터)로 파괴한다(GPU 반납 포함, 릭 방지).
    void* oldObject = slot.object;
    IAssetImporter*    oldImporter = m_impl->slotImporter[idx];
    IAsyncAssetLoader* oldAsyncLoader = m_impl->slotAsyncLoader[idx];
    slot.object = imported.Value();
    slot.state = AssetState::Loaded;
    m_impl->slotImporter[idx] = importer;
    m_impl->slotAsyncLoader[idx] = nullptr;   // 리로드는 동기 임포터 경로로 소유권 이전.

    if (oldObject) {
        if (oldAsyncLoader) {
            oldAsyncLoader->Unload(oldObject, m_impl->device);
        } else if (oldImporter) {
            oldImporter->DestroyWithDevice(oldObject, m_impl->device);
        }
    }

    result.swapped = true;
    MYE_LOG_INFO("Asset", "ReimportPath: swapped '{}' (slot {})", key, idx);
    return result;
}

template <typename T>
AssetHandle<T> AssetManager::LoadAsync(std::string_view vpath, LoadPriority pri) {
    const std::string key(vpath);

    // 캐시 히트: 기존 슬롯(로딩 중 포함)의 핸들 공유.
    if (auto it = m_impl->pathToSlot.find(key); it != m_impl->pathToSlot.end()) {
        const uint32_t idx = it->second;
        return AssetHandle<T>(this, idx, m_impl->slots[idx].generation.load());
    }

    // 배선(잡+버스)과 타입 로더가 모두 있어야 진짜 비동기. 아니면 동기 폴백.
    auto loaderIt = m_impl->asyncLoaders.find(T::kAssetTypeId);
    if (!m_impl->jobs || loaderIt == m_impl->asyncLoaders.end()) {
        AssetHandle<T> handle = LoadSync<T>(vpath);
        if (m_impl->bus) {
            AssetLoadedEvent ev;
            ev.guid = handle.Guid();
            ev.type = T::kAssetTypeId;
            ev.success = handle.IsLoaded();
            m_impl->bus->Enqueue(ev);
        }
        return handle;
    }

    // 슬롯 즉시 확보(State=Queued) → 핸들 즉시 반환.
    const AssetGuid guid = AssetGuid::Generate();
    const uint32_t idx = AcquireSlot(guid, T::kAssetTypeId);
    m_impl->pathToSlot.emplace(key, idx);
    AssetSlot& slot = m_impl->slots[idx];
    slot.state = AssetState::Queued;

    auto req = std::make_shared<AsyncRequest>();
    req->vpath = key;
    req->slot = idx;
    req->generation = slot.generation.load();
    req->guid = guid;
    req->type = T::kAssetTypeId;
    req->loader = loaderIt->second.get();
    req->loadCtx.guid = guid;

    // in-flight 카운트 증가 — CommitAsync가 정확히 한 번 감소시킨다(모든 종료 경로).
    m_impl->inFlight.fetch_add(1);

    AssetHandle<T> handle(this, idx, req->generation);

    // Blocking 우선순위: 지금 동기적으로 IO→Parse→Finalize 수행(부트스트랩 강제 로드).
    if (pri == LoadPriority::Blocking) {
        RunBlocking(req.get());
        return handle;
    }

    // --- IO 잡: VFS 블롭 읽기(IO 큐 — 컴퓨트 워커를 굶기지 않음) ---
    AssetManager* self = this;
    m_impl->jobs->Schedule([self, req]() {
        auto blob = self->m_impl->vfs->ReadAll(req->vpath);
        if (!blob) {
            self->m_impl->jobs->RunOnMainThread(
                [self, req]() { self->CommitAsync(req.get(), nullptr, false); });
            return;
        }
        // 블롭을 요청에 이송(Parse가 span으로 소비). shared_ptr로 수명 유지.
        auto blobOwned = std::make_shared<Blob>(std::move(blob.Value()));

        // --- 컴퓨트 잡: Parse(순수 CPU, GPU 금지) ---
        self->m_impl->jobs->Schedule([self, req, blobOwned]() {
            auto parsed = req->loader->Parse(req->loadCtx,
                                             std::span<const std::byte>(*blobOwned));
            if (!parsed) {
                self->m_impl->jobs->RunOnMainThread(
                    [self, req]() { self->CommitAsync(req.get(), nullptr, false); });
                return;
            }
            req->parsed = parsed.Value();

            // --- 메인 스레드: Finalize(GPU 업로드) + 슬롯 커밋 ---
            self->m_impl->jobs->RunOnMainThread([self, req]() {
                // Queued → Loading(메인 스레드에서만 슬롯 write). 슬롯 재사용 시 스킵.
                if (AssetSlot* s = self->GetSlot(req->slot, req->generation))
                    s->state = AssetState::Loading;
                auto obj = req->loader->Finalize(req->loadCtx, req->parsed, self->m_impl->device);
                if (!obj) {
                    req->loader->DiscardParsed(req->parsed);
                    self->CommitAsync(req.get(), nullptr, false);
                    return;
                }
                self->CommitAsync(req.get(), obj.Value(), true);
            });
        }, QueueKind::Compute);
    }, QueueKind::IO);

    return handle;
}

// Blocking 경로: 현재 스레드(메인)에서 IO→Parse→Finalize를 순차 수행.
void AssetManager::RunBlocking(void* reqRaw) {
    auto* req = static_cast<AsyncRequest*>(reqRaw);
    auto blob = m_impl->vfs->ReadAll(req->vpath);
    if (!blob) { CommitAsync(req, nullptr, false); return; }

    auto parsed = req->loader->Parse(req->loadCtx, std::span<const std::byte>(blob.Value()));
    if (!parsed) { CommitAsync(req, nullptr, false); return; }
    req->parsed = parsed.Value();

    auto obj = req->loader->Finalize(req->loadCtx, req->parsed, m_impl->device);
    if (!obj) {
        req->loader->DiscardParsed(req->parsed);
        CommitAsync(req, nullptr, false);
        return;
    }
    CommitAsync(req, obj.Value(), true);
}

// 명시적 인스턴스화 — M1/M2 소비 타입.
template AssetHandle<Texture> AssetManager::LoadSync<Texture>(std::string_view);
template AssetHandle<Texture> AssetManager::LoadAsync<Texture>(std::string_view, LoadPriority);
template AssetHandle<Mesh> AssetManager::LoadSync<Mesh>(std::string_view);   // M2-C bridge_demo
template AssetHandle<Mesh> AssetManager::LoadAsync<Mesh>(std::string_view, LoadPriority);
template AssetHandle<AudioClip> AssetManager::LoadSync<AudioClip>(std::string_view);  // M3-A 오디오
template AssetHandle<AudioClip> AssetManager::LoadAsync<AudioClip>(std::string_view, LoadPriority);

} // namespace mye::asset
