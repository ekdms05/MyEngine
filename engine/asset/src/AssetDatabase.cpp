// AssetDatabase.cpp — 에셋 DB·리임포트 파이프라인 (docs/04 §핫 리로드, M3-A)
//
// 파이프라인: 파일 워처(워커 스레드) → 변경 큐(뮤텍스) → ImportDirty(메인 틱): 디바운스 →
//   AssetManager::ReimportPath(슬롯 in-place 스왑) → AssetReloadedEvent{guid,type} 발행 →
//   역의존 전파(위상 정렬·중복 제거, IReloadConsumer 순서 의존 소비자 배치 호출).
//
// 스레드 안전: 워처 콜백은 임의 스레드에서 온다. 콜백은 큐에만 push(뮤텍스). 리임포트·슬롯 스왑·
//   이벤트 발행은 전부 메인 스레드(ImportDirty) — AssetManager 슬롯 테이블 규약(단일스레드) 준수.
//
// 경로 매핑: 워처는 OS 절대 경로를 준다. rootDir 기준 상대경로를 vpath(기본 "assets://<rel>")로
//   환원해 AssetManager 캐시 키와 맞춘다(SetVpathMount 로 마운트 스킴 변경 가능).
#include "mye/asset/AssetDatabase.h"

#include "mye/asset/AssetManager.h"
#include "mye/asset/Importer.h"
#include "mye/core/Events.h"
#include "mye/core/Log.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mye::asset {

namespace {
struct GuidHash {
    std::size_t operator()(const AssetGuid& g) const noexcept {
        return static_cast<std::size_t>(g.hi ^ (g.lo * 1099511628211ull));
    }
};

std::string NormalizeSlashesLower(std::string s) {
    for (char& c : s) {
        if (c == '\\') c = '/';
    }
    return s;
}

// 소스 확장자만 리임포트 대상(파생 캐시·.meta 변경은 무시).
bool IsImportableSource(std::string_view path) {
    // 마지막 '.' 이후 확장자.
    size_t dot = path.find_last_of('.');
    if (dot == std::string_view::npos) return false;
    std::string ext(path.substr(dot));
    for (char& c : ext) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    // .meta 사이드카 자체 변경은 리임포트 트리거에서 제외(무한 루프 방지).
    if (ext == ".meta") return false;
    return true;
}
}  // namespace

struct AssetDatabase::Impl {
    AssetManager*  manager = nullptr;
    mye::EventBus* bus = nullptr;
    std::vector<std::unique_ptr<IAssetImporter>> importers;
    std::unique_ptr<IFileWatcher> watcher;

    std::string rootDir;       // OS 절대 경로('/' 정규화, 후행 슬래시 없음)
    std::string vpathMount = "assets";   // vpath 스킴(기본 assets://)

    // 역방향 의존성 간선: to → [from...] (from 이 to 를 참조).
    std::unordered_map<AssetGuid, std::vector<AssetGuid>, GuidHash> referencers;
    // 순서 의존 파생 상태 소비자: producer GUID → [consumers...].
    std::unordered_map<AssetGuid, std::vector<IReloadConsumer*>, GuidHash> reloadConsumers;

    // 경로 ↔ GUID 인덱스(스캔·워처 매핑). 소문자/정규화된 vpath 키.
    std::unordered_map<std::string, AssetGuid> pathToGuid;
    std::unordered_map<AssetGuid, std::string, GuidHash> guidToPath;

    // ---- 디바운스 큐(워처 스레드 push, 메인 틱 drain) ----
    std::mutex queueMutex;
    struct PendingChange {
        std::string vpath;
        std::chrono::steady_clock::time_point when;
    };
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> pendingByPath;
    std::chrono::milliseconds debounce{80};

    // OS 절대 경로 → vpath("assets://<rel>"). 루트 밖이면 빈 문자열.
    std::string OsPathToVpath(std::string_view osPath) const {
        std::string p = NormalizeSlashesLower(std::string(osPath));
        // 대소문자 무시 접두 비교(win32 파일시스템은 대소문자 무구분).
        auto lower = [](std::string s) {
            for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            return s;
        };
        std::string pl = lower(p);
        std::string rl = lower(rootDir);
        if (rl.empty() || pl.size() < rl.size() || pl.compare(0, rl.size(), rl) != 0) {
            return {};
        }
        std::string rel = p.substr(rl.size());
        while (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());
        if (rel.empty()) return {};
        return vpathMount + "://" + rel;
    }

    void EnqueueChange(std::string vpath) {
        std::lock_guard<std::mutex> lock(queueMutex);
        pendingByPath[std::move(vpath)] = std::chrono::steady_clock::now();
    }
};

AssetDatabase::AssetDatabase(AssetManager& manager, mye::EventBus* bus)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->manager = &manager;
    m_impl->bus = bus;
}
AssetDatabase::~AssetDatabase() {
    // 워처를 먼저 정지해 콜백이 파괴 중인 Impl 을 만지지 못하게 한다.
    if (m_impl->watcher) m_impl->watcher->StopAll();
}

void AssetDatabase::RegisterImporter(std::unique_ptr<IAssetImporter> importer) {
    if (importer) m_impl->importers.push_back(std::move(importer));
}
void AssetDatabase::UnregisterImporter(const IAssetImporter* importer) {
    auto& v = m_impl->importers;
    for (auto it = v.begin(); it != v.end(); ++it) {
        if (it->get() == importer) { v.erase(it); break; }
    }
}

Expected<void, Error> AssetDatabase::ScanDirectory(std::string_view rootDir) {
    // M3-A: 인메모리 GUID 인덱스 구축(영속화 없음). 실제 .meta 파싱·서브에셋은 후속.
    // 여기서는 rootDir 정규화만 기록해 워처 경로 매핑의 기준을 세운다.
    m_impl->rootDir = NormalizeSlashesLower(std::string(rootDir));
    while (!m_impl->rootDir.empty() && m_impl->rootDir.back() == '/') m_impl->rootDir.pop_back();
    return {};
}

AssetGuid AssetDatabase::GuidFromPath(std::string_view assetPath) const {
    auto it = m_impl->pathToGuid.find(NormalizeSlashesLower(std::string(assetPath)));
    return it == m_impl->pathToGuid.end() ? AssetGuid{} : it->second;
}
std::string AssetDatabase::PathFromGuid(AssetGuid guid) const {
    auto it = m_impl->guidToPath.find(guid);
    return it == m_impl->guidToPath.end() ? std::string{} : it->second;
}

Expected<void, Error> AssetDatabase::StartWatching(std::unique_ptr<IFileWatcher> watcher,
                                                   std::string_view rootDir) {
    m_impl->watcher = std::move(watcher);
    if (!m_impl->watcher) return Error{"StartWatching: null watcher", -1};

    // rootDir 기준을 세운다(ScanDirectory 를 별도로 안 불렀어도 매핑이 동작하도록).
    if (m_impl->rootDir.empty()) {
        m_impl->rootDir = NormalizeSlashesLower(std::string(rootDir));
        while (!m_impl->rootDir.empty() && m_impl->rootDir.back() == '/') m_impl->rootDir.pop_back();
    }

    Impl* impl = m_impl.get();
    return m_impl->watcher->Watch(rootDir, [impl](const FileChange& ch) {
        // 워커 스레드: 변경을 vpath 로 환원해 큐잉만 한다(리임포트는 메인 틱).
        if (ch.kind == FileChangeKind::Removed) return;   // 제거는 리임포트 대상 아님(M3-A).
        if (!IsImportableSource(ch.path)) return;
        std::string vpath = impl->OsPathToVpath(ch.path);
        if (vpath.empty()) return;
        impl->EnqueueChange(std::move(vpath));
    });
}

void AssetDatabase::ImportDirty() {
    // 1) 디바운스 경과분만 뽑아낸다(워처 스레드와 뮤텍스로 격리).
    std::vector<std::string> ready;
    {
        std::lock_guard<std::mutex> lock(m_impl->queueMutex);
        const auto now = std::chrono::steady_clock::now();
        for (auto it = m_impl->pendingByPath.begin(); it != m_impl->pendingByPath.end();) {
            if (now - it->second >= m_impl->debounce) {
                ready.push_back(it->first);
                it = m_impl->pendingByPath.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (ready.empty()) return;

    // 2) 각 소스를 리임포트 → 슬롯 스왑 → 이벤트 발행.
    std::vector<AssetGuid> reloadedRoots;
    for (const std::string& vpath : ready) {
        AssetManager::ReimportResult r = m_impl->manager->ReimportPath(vpath);
        if (!r.swapped) continue;

        if (m_impl->bus) {
            AssetReloadedEvent ev;
            ev.guid = r.guid;
            ev.type = r.type;
            m_impl->bus->Publish(ev);   // 즉시 디스패치(메인 스레드) — 파생 상태 갱신 훅.
        }
        reloadedRoots.push_back(r.guid);
    }

    // 3) 역의존 전파(위상: BFS + 방문 집합으로 중복 제거). 순서 의존 소비자를 배치 호출.
    if (!reloadedRoots.empty()) {
        PropagateReloads(reloadedRoots);
    }
}

void AssetDatabase::ReimportAll(AssetTypeId /*type*/) {
    // M3-A: 인덱싱된 모든 경로 강제 리임포트. 인덱스가 비면 no-op(전체 스캔은 후속).
    std::vector<AssetGuid> roots;
    for (const auto& [path, guid] : m_impl->pathToGuid) {
        AssetManager::ReimportResult r = m_impl->manager->ReimportPath(path);
        if (r.swapped) {
            if (m_impl->bus) {
                AssetReloadedEvent ev{}; ev.guid = r.guid; ev.type = r.type;
                m_impl->bus->Publish(ev);
            }
            roots.push_back(r.guid);
        }
    }
    if (!roots.empty()) PropagateReloads(roots);
}

// 역의존 전파: reloaded 루트에서 시작해 순서 의존 소비자(IReloadConsumer)를 위상 순서로
//   호출한다. 각 소비자가 반환한 추가 dirty GUID 를 다시 큐에 넣되, 방문 집합으로 중복 제거해
//   사이클·중복 처리를 막는다(한 프레임에 몰아서).
void AssetDatabase::PropagateReloads(const std::vector<AssetGuid>& roots) {
    std::unordered_set<AssetGuid, GuidHash> visited(roots.begin(), roots.end());
    std::vector<AssetGuid> frontier = roots;

    while (!frontier.empty()) {
        std::vector<AssetGuid> next;
        for (const AssetGuid& g : frontier) {
            auto it = m_impl->reloadConsumers.find(g);
            if (it == m_impl->reloadConsumers.end()) continue;
            for (IReloadConsumer* consumer : it->second) {
                std::vector<AssetGuid> further;
                consumer->OnDependencyReloaded(g, further);
                for (const AssetGuid& fg : further) {
                    if (visited.insert(fg).second) {
                        next.push_back(fg);
                        // 파생 대상도 리로드 이벤트로 알린다(GUID→type 미상이면 0).
                        if (m_impl->bus) {
                            AssetReloadedEvent ev{}; ev.guid = fg; ev.type = 0;
                            m_impl->bus->Publish(ev);
                        }
                    }
                }
            }
        }
        frontier.swap(next);
    }
}

void AssetDatabase::AddDependency(AssetGuid from, AssetGuid to) {
    m_impl->referencers[to].push_back(from);
}
std::vector<AssetGuid> AssetDatabase::FindReferencers(AssetGuid guid) const {
    auto it = m_impl->referencers.find(guid);
    return it == m_impl->referencers.end() ? std::vector<AssetGuid>{} : it->second;
}

void AssetDatabase::SetVpathMount(std::string_view mount) {
    m_impl->vpathMount = std::string(mount);
}
void AssetDatabase::MapPathGuid(std::string_view vpath, AssetGuid guid) {
    std::string key = NormalizeSlashesLower(std::string(vpath));
    m_impl->pathToGuid[key] = guid;
    m_impl->guidToPath[guid] = key;
}

void AssetDatabase::RegisterReloadConsumer(AssetGuid producer, IReloadConsumer* consumer) {
    if (consumer) m_impl->reloadConsumers[producer].push_back(consumer);
}
void AssetDatabase::UnregisterReloadConsumer(IReloadConsumer* consumer) {
    for (auto& [producer, list] : m_impl->reloadConsumers) {
        list.erase(std::remove(list.begin(), list.end(), consumer), list.end());
    }
}

}  // namespace mye::asset
