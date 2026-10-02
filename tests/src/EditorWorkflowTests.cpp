// EditorWorkflowTests.cpp — editor authoring, persistence and Play workflows.
//
// 에디터에서 스프라이트·프랍·NPC 프리팹을 뷰포트에 배치 → 인스펙터로 값 수정 →
//   Ctrl+S 저장 → ▶Play 로 즉시 걸어보고 → ■Stop 으로 편집 상태 복귀. 모든 편집이 Ctrl+Z 로 되돌려짐.
//
// 이 테스트는 GUI 를 헤드리스로 대체해, 실제 에디터 진입점(InstantiateAssetToWorld 드롭 처리,
//   PropertyEditCommand 인스펙터 편집, SceneSerializer::SaveToFile 저장, PlayModeController Play/Stop,
//   CommandStack Undo)을 그대로 호출해 각 완료기준을 왕복 검증한다. 패널 위젯(ImGui)만 우회하고,
//   그 아래 커맨드/직렬화/플레이모드 경로는 프로덕션 코드 그대로다.
#include "TestFramework.h"

#include "mye/editor/BuiltinPanels.h"
#include "mye/editor/AnimEditing.h"
#include "mye/editor/Command.h"
#include "mye/editor/CommandStack.h"
#include "mye/editor/EditorContext.h"
#include "mye/editor/PlayMode.h"
#include "mye/editor/SceneSerializer.h"
#include "mye/editor/Selection.h"
#include "mye/editor/EditorApp.h"
#include "mye/editor/EditorModule.h"
#include "mye/editor/ExtensionRegistry.h"
#include "mye/editor/PlayWindow.h"
#include "mye/editor/Project.h"
#include "mye/editor/Viewport.h"
#include "mye/runtime/ObjectComponents.h"
#include "mye/runtime/GameInput.h"
#include <Windows.h>
#include "mye/phys/Collision.h"
#include "mye/asset/AssetDatabase.h"
#include "mye/asset/AssetManager.h"
#include "mye/asset/FileSystem.h"
#include "mye/asset/Importer.h"
#include "imgui.h"
#include "imgui_internal.h"

#include "mye/core/Events.h"
#include "mye/core/Json.h"
#include "mye/core/JsonFile.h"
#include "mye/core/Module.h"
#include "mye/core/platform/Win32Window.h"
#include "mye/scene/SceneModule.h"
#include "mye/ecs/World.h"
#include "mye/refl/TypeBuilder.h"
#include "mye/refl/TypeRegistry.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/Camera3D.h"
#include "mye/scene/Camera2D.h"
#include "mye/runtime/ObjectSystem.h"

#include <cstdio>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <Windows.h>

using namespace mye;
using namespace mye::editor;

namespace {
class TestEditorViewport final : public IEditorViewport {
public:
    ViewportCamera camera;
    uint32_t width = 640, height = 360;
    void SetViewportSize(uint32_t w, uint32_t h) override { width = w; height = h; }
    void SetCamera(const ViewportCamera& value) override { camera = value; }
    ViewportCamera Camera() const override { return camera; }
    void* ColorTextureId() const override { return nullptr; }
    uint32_t RenderWidth() const override { return width; }
    uint32_t RenderHeight() const override { return height; }
    Vec2 ScreenToWorld(Vec2 pixel) const override { return ViewportPointOnPlane(camera, pixel, width, height); }
    Vec2 WorldToScreen(Vec2 world) const override { return ProjectViewportPoint(camera, {world.x, world.y, 0}, width, height); }
};
}

// -----------------------------------------------------------------------------
// 테스트용 스프라이트 엔티티 컴포넌트 — 씬 저장이 왕복시키려면 리플렉션 이름 == 컴포넌트 이름
//   규약(refl::TypeId == ComponentTypeId)이 성립해야 한다. 실 SpriteRenderer 는 씬 모듈에서
//   리플렉션 미등록이라(M4-B 현황), 여기서는 저장 왕복을 검증할 수 있는 리플렉션 컴포넌트로
//   동등 시나리오를 구성한다. LocalTransform 도 저장 대상이 되도록 짧은 이름으로 리플렉션 등록.
// -----------------------------------------------------------------------------
namespace ewtest {
struct Sprite {
    MYE_COMPONENT(Sprite);
    std::uint64_t guid = 0;
    float         tintR = 1.0f;
};
struct Xform {   // LocalTransform 대체(저장 왕복용 위치 컴포넌트)
    MYE_COMPONENT(Xform);
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};
} // namespace ewtest

MYE_REFLECT_NAME(ewtest::Sprite, "Sprite");
MYE_REFLECT_NAME(ewtest::Xform, "Xform");
namespace mye::refl {
template <> void Reflect<ewtest::Sprite>(TypeBuilder<ewtest::Sprite>& b);
template <> void Reflect<ewtest::Xform>(TypeBuilder<ewtest::Xform>& b);
}
template <> void mye::refl::Reflect(TypeBuilder<ewtest::Sprite>& b) {
    b.Version(1).Field("guid", &ewtest::Sprite::guid).Field("tintR", &ewtest::Sprite::tintR);
}
template <> void mye::refl::Reflect(TypeBuilder<ewtest::Xform>& b) {
    b.Version(1)
     .Field("x", &ewtest::Xform::x)
     .Field("y", &ewtest::Xform::y)
     .Field("z", &ewtest::Xform::z);
}

namespace {

template <typename C>
void RegisterPool(ecs::World& world) {
    world.RegisterComponent<C>(refl::GetType<C>()->Name().data());
}

// EditorContext 하네스(편집 World 하나 + 커맨드 스택 + 플레이모드).
struct WFHarness {
    EventBus          bus;
    ecs::World        world;
    SelectionManager  selection{&bus};
    PlayModeController playMode;
    CommandStack      commands{nullptr};   // ctx 는 아래에서 배선
    EditorContext     ctx{};

    WFHarness() : commands(&ctxRef()) {
        playMode.SetEditWorld(&world);
        playMode.SetEventBus(&bus);
        ctx.events = &bus;
        ctx.selection = &selection;
        ctx.playMode = &playMode;     // activeWorld() → Edit World(플레이 중엔 Play World)
        ctx.commands = &commands;
    }
    EditorContext& ctxRef() { return ctx; }
};

ValueBlob F32Blob(float v) {
    std::ostringstream os;
    os << v;
    return ValueBlob{os.str()};
}

std::string SlurpFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

// -----------------------------------------------------------------------------
// (a) 배치 — 에셋 드롭(InstantiateAssetToWorld) 이 스프라이트 생성 커맨드를 발행, Undo 가능.
// -----------------------------------------------------------------------------
MYE_TEST(WorkflowPlaceSpriteViaDrop) {
    WFHarness h;
    // 실 드롭 경로: 뷰포트가 "MYE_ASSET" 페이로드(vpath) + 월드좌표로 이 함수를 호출한다.
    //   project·engine 이 없어도 스프라이트는 빈 AssetRef 로 생성된다(안전 fallback).
    InstantiateAssetToWorld(h.ctx, "assets://chars/hero.png", Vec2{3.0f, -2.0f});

    // 생성 확인: SpriteRenderer + LocalTransform 엔티티 1개.
    int spriteCount = 0;
    Vec3 pos{};
    h.world.Query<scene::SpriteRenderer, scene::LocalTransform>().Each(
        [&](ecs::Entity, scene::SpriteRenderer&, scene::LocalTransform& lt) {
            ++spriteCount;
            pos = lt.position;
        });
    MYE_EXPECT(spriteCount == 1);
    MYE_EXPECT(pos.x == 3.0f && pos.y == -2.0f);
    MYE_EXPECT(h.commands.CanUndo());

    // Undo — 배치 취소.
    h.commands.Undo();
    int after = 0;
    h.world.Query<scene::SpriteRenderer>().Each([&](ecs::Entity, scene::SpriteRenderer&) { ++after; });
    MYE_EXPECT(after == 0);

    // Redo — 다시 배치.
    h.commands.Redo();
    after = 0;
    h.world.Query<scene::SpriteRenderer>().Each([&](ecs::Entity, scene::SpriteRenderer&) { ++after; });
    MYE_EXPECT(after == 1);
}

// -----------------------------------------------------------------------------
// (b)+(e) 편집 — 인스펙터 값 변경(PropertyEditCommand) → Undo 왕복.
// -----------------------------------------------------------------------------
MYE_TEST(WorkflowEditPropertyAndUndo) {
    WFHarness h;
    RegisterPool<ewtest::Xform>(h.world);
    ecs::Entity e = h.world.Create();
    h.world.Add<ewtest::Xform>(e).x = 0.0f;

    const refl::TypeInfo* type = refl::GetType<ewtest::Xform>();
    ObjectRef target = ObjectRef::Component(e, *type);
    auto path = refl::PropertyPath::Parse("x");
    MYE_EXPECT((bool)path);

    // 인스펙터가 x 를 0 → 42 로 편집(PropertyEditCommand).
    h.commands.Push(std::make_unique<PropertyEditCommand>(
        target, path.Value(), F32Blob(0.0f), F32Blob(42.0f), "set x"));
    MYE_EXPECT(h.world.TryGet<ewtest::Xform>(e)->x == 42.0f);

    // Ctrl+Z 로 원복.
    h.commands.Undo();
    MYE_EXPECT(h.world.TryGet<ewtest::Xform>(e)->x == 0.0f);
    h.commands.Redo();
    MYE_EXPECT(h.world.TryGet<ewtest::Xform>(e)->x == 42.0f);
}

// -----------------------------------------------------------------------------
// (c) 저장 — SaveActive 경로(SceneSerializer::SaveToFile) 가 씬 JSON 파일을 실제로 쓰고,
//   내용에 배치·편집 결과가 반영되는지 파일 텍스트로 검증.
// -----------------------------------------------------------------------------
MYE_TEST(WorkflowSaveSceneToFile) {
    WFHarness h;
    RegisterPool<ewtest::Xform>(h.world);
    RegisterPool<ewtest::Sprite>(h.world);

    // 배치 + 편집: Xform(x=7) + Sprite(guid=123) 엔티티.
    ecs::Entity e = h.world.Create();
    auto& xf = h.world.Add<ewtest::Xform>(e);
    xf.x = 7.0f; xf.y = 8.0f;
    auto& sp = h.world.Add<ewtest::Sprite>(e);
    sp.guid = 123;

    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "mye_m4_wf";
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path scenePath = dir / "untitled.scene";
    fs::remove(scenePath, ec);

    SceneSerializer ser;
    auto saved = ser.SaveToFile(h.world, scenePath.string());
    MYE_EXPECT((bool)saved);
    MYE_EXPECT(fs::exists(scenePath));

    // 파일 내용에 컴포넌트·값이 텍스트로 존재하는지 확인(JSON 씬).
    std::string text = SlurpFile(scenePath.string());
    MYE_EXPECT(!text.empty());
    MYE_EXPECT(text.find("Xform") != std::string::npos);
    MYE_EXPECT(text.find("Sprite") != std::string::npos);
    MYE_EXPECT(text.find("123") != std::string::npos);   // sprite guid
    MYE_EXPECT(text.find("entities") != std::string::npos);

    // 로드 왕복: 빈 World 로 다시 읽어 값 복원 확인.
    ecs::World loaded;
    RegisterPool<ewtest::Xform>(loaded);
    RegisterPool<ewtest::Sprite>(loaded);
    auto roots = ser.LoadFromFile(loaded, scenePath.string());
    MYE_EXPECT((bool)roots);
    int found = 0; float lx = -1.0f; std::uint64_t lg = 0;
    loaded.Query<ewtest::Xform, ewtest::Sprite>().Each(
        [&](ecs::Entity, ewtest::Xform& x, ewtest::Sprite& s) { ++found; lx = x.x; lg = s.guid; });
    MYE_EXPECT(found == 1);
    MYE_EXPECT(lx == 7.0f);
    MYE_EXPECT(lg == 123);

    fs::remove(scenePath, ec);
}

// -----------------------------------------------------------------------------
// (d) 플레이 — PlayMode Start → Play World 에서 tick 시뮬레이션(이동) → Stop → 편집 상태 원복.
// -----------------------------------------------------------------------------
MYE_TEST(WorkflowPlayTickStopRestore) {
    WFHarness h;
    RegisterPool<ewtest::Xform>(h.world);
    ecs::Entity e = h.world.Create();
    h.world.Add<ewtest::Xform>(e).x = 0.0f;   // 편집 상태 위치.

    // ▶Play — 편집 World 스냅샷 → Play World.
    MYE_EXPECT(h.playMode.Play());
    ecs::World* pw = h.playMode.ActiveWorld();
    MYE_EXPECT(pw != nullptr && pw != &h.world);

    // Play World 를 tick(캐릭터 이동 시뮬레이션): x += 5 (게이팅된 시스템이 하는 일의 대역).
    int moved = 0;
    pw->Query<ewtest::Xform>().Each([&](ecs::Entity, ewtest::Xform& x) { x.x += 5.0f; ++moved; });
    MYE_EXPECT(moved == 1);
    pw->Query<ewtest::Xform>().Each([&](ecs::Entity, ewtest::Xform& x) { MYE_EXPECT(x.x == 5.0f); });

    // 편집 World 는 플레이 중 수정에 영향받지 않음.
    MYE_EXPECT(h.world.TryGet<ewtest::Xform>(e)->x == 0.0f);

    // ■Stop — Play World 파기, 편집 상태 원복.
    h.playMode.Stop();
    MYE_EXPECT(h.playMode.State() == PlayState::Edit);
    MYE_EXPECT(h.playMode.ActiveWorld() == &h.world);
    MYE_EXPECT(h.world.TryGet<ewtest::Xform>(e)->x == 0.0f);   // 편집 상태 그대로.
}

// -----------------------------------------------------------------------------
// (e) 전체 편집 왕복 — 배치·값편집·삭제·재부모화가 모두 Undo 가능(단일 스택 순차 왕복).
// -----------------------------------------------------------------------------
MYE_TEST(WorkflowUndoAllEditKinds) {
    WFHarness h;
    RegisterPool<ewtest::Xform>(h.world);

    // 1) 배치(에셋 드롭 → 스프라이트 엔티티).
    InstantiateAssetToWorld(h.ctx, "assets://chars/hero.png", Vec2{0.0f, 0.0f});
    ecs::Entity sprite = ecs::Entity::Null();
    h.world.Query<scene::SpriteRenderer>().Each([&](ecs::Entity en, scene::SpriteRenderer&) { sprite = en; });
    MYE_EXPECT(!sprite.IsNull());

    // 2) 부모 엔티티 생성(구조 커맨드).
    auto createParent = std::make_unique<CreateEntityCommand>();
    CreateEntityCommand* cpPtr = createParent.get();
    h.commands.Push(std::move(createParent));
    ecs::Entity parent = cpPtr->Created();
    MYE_EXPECT(!parent.IsNull());

    // 3) 재부모화(스프라이트를 parent 밑으로).
    h.commands.Push(std::make_unique<ReparentCommand>(sprite, parent, /*keepWorld*/ true));
    {
        scene::Parent* pc = h.world.TryGet<scene::Parent>(sprite);
        MYE_EXPECT(pc && pc->parent == parent);
    }

    // 4) 별도 엔티티 생성 후 삭제(구조 커맨드).
    ecs::Entity victim = h.world.Create();
    h.world.Add<ewtest::Xform>(victim).x = 99.0f;
    h.commands.Push(std::make_unique<DestroyEntityCommand>(victim));
    MYE_EXPECT(!h.world.Valid(victim));

    // ---- 전부 Ctrl+Z 로 역순 되돌리기 ----
    // Undo 4: 삭제 취소(victim 복원).
    h.commands.Undo();
    int restored = 0;
    h.world.Query<ewtest::Xform>().Each([&](ecs::Entity, ewtest::Xform& x) { if (x.x == 99.0f) ++restored; });
    MYE_EXPECT(restored == 1);

    // Undo 3: 재부모화 취소(루트로 복귀).
    h.commands.Undo();
    {
        scene::Parent* pc = h.world.TryGet<scene::Parent>(sprite);
        MYE_EXPECT(!pc || pc->parent.IsNull());
    }

    // Undo 2: 부모 생성 취소.
    h.commands.Undo();
    MYE_EXPECT(!h.world.Valid(parent));

    // Undo 1: 배치 취소(스프라이트 제거).
    h.commands.Undo();
    int sprites = 0;
    h.world.Query<scene::SpriteRenderer>().Each([&](ecs::Entity, scene::SpriteRenderer&) { ++sprites; });
    MYE_EXPECT(sprites == 0);
    MYE_EXPECT(!h.commands.CanUndo());   // 스택 소진.
}

// -----------------------------------------------------------------------------
// (d) M4 리뷰 후속 실 해결 — DestroyEntityCommand.Undo 가 원래 핸들(index/generation)로
//     복원돼, 파괴 전에 잡아둔 EntityRef 가 Undo 후에도 그대로 유효(dangling 아님).
//     World::CreateWithId(nav 에이전트) 를 SceneSerializer preserveHandles 경로로 소비.
// -----------------------------------------------------------------------------
MYE_TEST(WorkflowDestroyUndoPreservesHandle) {
    WFHarness h;
    RegisterPool<ewtest::Xform>(h.world);

    ecs::Entity victim = h.world.Create();
    h.world.Add<ewtest::Xform>(victim).x = 77.0f;

    // 파괴 전에 외부에서 핸들을 보관(선택·다른 시스템이 참조하는 상황을 모사).
    const ecs::Entity heldRef = victim;
    const std::uint32_t vIdx = victim.index;
    const std::uint32_t vGen = victim.generation;

    // 삭제 → 보관 핸들 무효화.
    h.commands.Push(std::make_unique<DestroyEntityCommand>(victim));
    MYE_EXPECT(!h.world.Valid(victim));
    MYE_EXPECT(!h.world.Valid(heldRef));

    // Undo → 원래 핸들로 복원.
    h.commands.Undo();

    // 핸들 동일성: 옛 index/generation 이 그대로 되살아나 heldRef 가 다시 유효.
    MYE_EXPECT(h.world.Valid(heldRef));
    MYE_EXPECT(heldRef.index == vIdx);
    MYE_EXPECT(heldRef.generation == vGen);

    // 타 컴포넌트 참조 살아있음: 보관 핸들로 컴포넌트 값 그대로 조회 가능.
    const ewtest::Xform* xf = h.world.TryGet<ewtest::Xform>(heldRef);
    MYE_EXPECT(xf != nullptr);
    MYE_EXPECT(xf->x == 77.0f);
}

// (e) 서브트리 자손이 선택된 상태에서 루트 파괴 → Undo. 선택이 루트가 아니라 임의 자손을
//     가리켜도 복원된 자손 핸들로 재결선돼야 한다(핸들 보존 성공 시 옛==새라 항등 재선택).
MYE_TEST(WorkflowDestroyUndoRelinksDescendantSelection) {
    WFHarness h;

    // 부모 + 자식(구조 커맨드로 트랜스폼·계층 구성).
    auto createParent = std::make_unique<CreateEntityCommand>();
    CreateEntityCommand* cpPtr = createParent.get();
    h.commands.Push(std::move(createParent));
    ecs::Entity parent = cpPtr->Created();

    auto createChild = std::make_unique<CreateEntityCommand>(parent);
    CreateEntityCommand* ccPtr = createChild.get();
    h.commands.Push(std::move(createChild));
    ecs::Entity child = ccPtr->Created();
    MYE_EXPECT(!parent.IsNull() && !child.IsNull());
    {
        scene::Parent* pc = h.world.TryGet<scene::Parent>(child);
        MYE_EXPECT(pc && pc->parent == parent);
    }

    // 자손(child)을 선택.
    h.selection.Select(SelectableRef::OfEntity(child), SelectMode::Replace);
    MYE_EXPECT(h.selection.IsSelected(SelectableRef::OfEntity(child)));

    // 루트(parent) 파괴 → child 도 함께 파괴, 선택 dangling.
    h.commands.Push(std::make_unique<DestroyEntityCommand>(parent));
    MYE_EXPECT(!h.world.Valid(parent));
    MYE_EXPECT(!h.world.Valid(child));

    // Undo → 서브트리 복원. 선택이 복원된 자손 핸들을 가리켜야 한다.
    h.commands.Undo();
    MYE_EXPECT(h.world.Valid(child));
    MYE_EXPECT(h.selection.IsSelected(SelectableRef::OfEntity(child)));
}

namespace {
std::filesystem::path ProjectTestDirectory(const char* name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = Utf8Path(MYE_TEST_DATA_DIR) / "editor-projects" / (std::string(name) + std::to_string(stamp));
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    MYE_EXPECT(!ec);
    return path;
}

class EditorTestContext final : public EngineContext {
public:
    EventBus events;
    EnginePaths paths;
    std::unordered_map<ServiceId, void*> services;
    explicit EditorTestContext(const std::filesystem::path& root) {
        paths.userDir = Utf8String(root / "user");
        services[HashFnv1a64("EventBus")] = &events;
    }
    void* GetServiceRaw(ServiceId id) override {
        auto it = services.find(id);
        return it == services.end() ? nullptr : it->second;
    }
    void RegisterServiceRaw(ServiceId id, void* service) override { services[id] = service; }
    void UnregisterServiceRaw(ServiceId id) override { services.erase(id); }
    uint32_t GetEngineVersion() const override { return 1; }
    const EnginePaths& GetPaths() const override { return paths; }
};

struct EditorGuiScope {
    EditorGuiScope() {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1600, 900);
        io.DeltaTime = 1.0f / 60;
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~EditorGuiScope() { ImGui::DestroyContext(); }
};

struct EditorProcess {
    PROCESS_INFORMATION process{};
    ~EditorProcess() {
        if (process.hProcess) {
            if (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
                TerminateProcess(process.hProcess, 2); // Only a failed test's own child process.
                WaitForSingleObject(process.hProcess, 5000);
            }
            CloseHandle(process.hProcess);
            CloseHandle(process.hThread);
        }
    }
    bool Start(const std::filesystem::path& directory, std::wstring_view arguments) {
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (ec) return false;
        // The app writes logs/layout under LOCALAPPDATA; keep every test write under build/.
        auto* inherited = GetEnvironmentStringsW();
        if (!inherited) return false;
        std::vector<std::wstring> entries;
        for (const wchar_t* entry = inherited; *entry; entry += std::wcslen(entry) + 1)
            if (_wcsnicmp(entry, L"LOCALAPPDATA=", 13) != 0) entries.emplace_back(entry);
        FreeEnvironmentStringsW(inherited);
        entries.emplace_back(L"LOCALAPPDATA=" + (directory / "localappdata").wstring());
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
        std::vector<wchar_t> environment;
        for (const auto& entry : entries) {
            environment.insert(environment.end(), entry.begin(), entry.end());
            environment.push_back(L'\0');
        }
        environment.push_back(L'\0');
        const auto executable = Widen(MYE_EDITOR_EXE);
        auto command = L"\"" + executable + L"\" " + std::wstring(arguments);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        return CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, environment.data(), directory.c_str(), &startup, &process) != FALSE;
    }
};

struct EditorProcessWindow {
    DWORD processId;
    HWND hwnd = nullptr;
};

BOOL CALLBACK FindEditorProcessWindow(HWND hwnd, LPARAM parameter) {
    auto& result = *reinterpret_cast<EditorProcessWindow*>(parameter);
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    wchar_t className[64]{};
    GetClassNameW(hwnd, className, static_cast<int>(std::size(className)));
    if (processId == result.processId && std::wcscmp(className, L"MyEngineWindowClass") == 0) {
        result.hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}
}

MYE_TEST(EditorExecutableRecoversInteractiveProjectButFailsAutomatedProject) {
    const auto root = ProjectTestDirectory("startup-process");
    const auto broken = root / "broken" / "project.myeproj";
    std::filesystem::create_directories(broken.parent_path());
    { std::ofstream file(broken); file << "{"; }
    const auto projectArgument = L"--project \"" + broken.wstring() + L"\"";
    int index = 0;
    for (const auto* flags : {L" --headless --frames 1", L" --frames 1"}) {
        EditorProcess child;
        const auto started = child.Start(root / std::to_string(index++), projectArgument + flags);
        MYE_EXPECT(started);
        if (!started) continue;
        MYE_EXPECT(WaitForSingleObject(child.process.hProcess, 15000) == WAIT_OBJECT_0);
        DWORD exitCode = 0;
        MYE_EXPECT(GetExitCodeProcess(child.process.hProcess, &exitCode) && exitCode == 1);
    }
    EditorProcess interactive;
    const auto directory = root / "interactive";
    const auto started = interactive.Start(directory, projectArgument);
    MYE_EXPECT(started);
    if (!started) return;
    const auto logPath = directory / "localappdata" / "MyEngine" / "broken" / "logs" / "engine.log";
    bool enteredLoop = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline && WaitForSingleObject(interactive.process.hProcess, 25) == WAIT_TIMEOUT) {
        std::ifstream log(logPath);
        const std::string text((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
        if (text.find("start (frames=") != std::string::npos) { enteredLoop = true; break; }
    }
    MYE_EXPECT(enteredLoop && WaitForSingleObject(interactive.process.hProcess, 0) == WAIT_TIMEOUT);
    EditorProcessWindow window{interactive.process.dwProcessId};
    EnumWindows(FindEditorProcessWindow, reinterpret_cast<LPARAM>(&window));
    MYE_EXPECT(window.hwnd);
    if (window.hwnd) MYE_EXPECT(PostMessageW(window.hwnd, WM_CLOSE, 0, 0));
    MYE_EXPECT(WaitForSingleObject(interactive.process.hProcess, 15000) == WAIT_OBJECT_0);
    DWORD exitCode = 1;
    MYE_EXPECT(GetExitCodeProcess(interactive.process.hProcess, &exitCode) && exitCode == 0);
    std::ifstream file(broken);
    const std::string preserved((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    MYE_EXPECT(preserved == "{");
}

MYE_TEST(EditorExecutableRejectsMissingRenderAsset) {
    const auto root = ProjectTestDirectory("missing-render-asset");
    EditorTestContext engine(root);
    EditorApp app;
    MYE_EXPECT(app.Initialize(engine, ""));
    MYE_EXPECT(app.CreateProject("Missing asset", Utf8String(root / "project")));
    auto* document = app.Project().Active();
    MYE_EXPECT(document);
    if (!document) { app.Shutdown(); return; }
    auto& world = document->World();
    const auto entity = world.Create();
    world.Add<scene::ObjectName>(entity).value = "Missing building";
    world.Add<scene::MeshRenderer>(entity).mesh.guid = asset::AssetGuid::Generate();
    MYE_EXPECT(app.SaveScene(document->Path()));
    app.Shutdown();
    const auto projectArgument = L"--project \"" + (root / "project/project.myeproj").wstring() + L"\"";
    int index = 0;
    for (const auto* flags : {L" --headless --frames 1", L" --headless --play --frames 1", L" --frames 1"}) {
        EditorProcess child;
        MYE_EXPECT(child.Start(root / std::to_string(index++), projectArgument + flags));
        if (!child.process.hProcess) continue;
        MYE_EXPECT(WaitForSingleObject(child.process.hProcess, 15000) == WAIT_OBJECT_0);
        DWORD exitCode = 0;
        MYE_EXPECT(GetExitCodeProcess(child.process.hProcess, &exitCode) && exitCode == 1);
    }
}

MYE_TEST(EditorInspectorAssetClearUndoRestoresGuidAndType) {
    EditorGuiScope gui;
    const auto root = ProjectTestDirectory("asset-ref-undo");
    EditorTestContext engine(root);
    EditorApp app;
    MYE_EXPECT(app.Initialize(engine, ""));
    MYE_EXPECT(app.CreateProject("Assets", Utf8String(root / "project")));
    auto& world = app.Project().Active()->World();
    const auto entity = world.Create();
    auto& sprite = world.Add<scene::SpriteRenderer>(entity);
    const asset::AssetRef original{asset::AssetGuid::Generate(), 0xf123456789abcdefULL};
    sprite.sprite = original;
    const auto* type = refl::TypeRegistry::Get().Find(scene::SpriteRenderer::kComponentTypeId);
    MYE_EXPECT(type);
    if (!type) { app.Shutdown(); return; }
    auto frame = [&]() {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(500, 700));
        ImGui::Begin("asset-ref-test");
        app.Inspector().DrawReflected(app.Context(), ObjectRef::Component(entity, *type), *type, &sprite, {});
        ImGui::End();
        ImGui::Render();
    };
    frame();
    auto* window = ImGui::FindWindowByName("asset-ref-test");
    MYE_EXPECT(window);
    if (!window) { app.Shutdown(); return; }
    ImGui::ActivateItemByID(window->GetID("비우기##sprite"));
    frame();
    MYE_EXPECT(!sprite.sprite.guid.IsValid() && sprite.sprite.type == 0);
    MYE_EXPECT(app.Commands().Position() == 1 && app.Commands().IsDirty());
    app.Commands().Undo();
    MYE_EXPECT(sprite.sprite.guid == original.guid && sprite.sprite.type == original.type);
    MYE_EXPECT(!app.Commands().IsDirty());
    app.Commands().Redo();
    MYE_EXPECT(!sprite.sprite.guid.IsValid() && sprite.sprite.type == 0);
    app.Commands().MarkSaved();
    ImGui::ActivateItemByID(window->GetID("비우기##sprite"));
    frame();
    MYE_EXPECT(app.Commands().Position() == 1 && !app.Commands().IsDirty());
    app.Shutdown();
}

MYE_TEST(EditorInvalidStartupReturnsToLauncherAndCanOpenProject) {
    EditorGuiScope gui;
    const auto root = ProjectTestDirectory("invalid-startup");
    const auto broken = root / "broken.myeproj";
    { std::ofstream file(broken); file << "{"; }
    EditorTestContext engine(root);
    EditorApp app;
    MYE_EXPECT(app.Initialize(engine, Utf8String(broken)));
    auto frame = [&]() { ImGui::NewFrame(); app.OnFrame(); ImGui::Render(); };
    frame();
    const auto* launcher = ImGui::FindWindowByName("프로젝트###project_launcher");
    MYE_EXPECT(!app.Project().IsOpen() && launcher && launcher->Active);
    MYE_EXPECT(app.CreateProject("Recovery", Utf8String(root / "recovered")));
    frame();
    MYE_EXPECT(app.Project().IsOpen());
    MYE_EXPECT(launcher && !launcher->Active);
    const auto* hierarchy = ImGui::FindWindowByName("하이어라키###mye.hierarchy");
    MYE_EXPECT(hierarchy && hierarchy->Active);
    std::ifstream file(broken);
    const std::string preserved((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    MYE_EXPECT(preserved == "{");
    app.Shutdown();
}

MYE_TEST(EditorInspectorAssetDropUndoAndUnchangedDrop) {
    EditorGuiScope gui;
    const auto root = ProjectTestDirectory("asset-ref-drop");
    EditorTestContext engine(root);
    EditorApp app;
    MYE_EXPECT(app.Initialize(engine, "", MYE_STARTER_SOURCE_DIR));
    MYE_EXPECT(app.CreateProject("Assets", Utf8String(root / "project"), false, true));
    const auto assets = Utf8Path(app.Project().RootDir()) / "assets";
    asset::VirtualFileSystem vfs;
    vfs.Mount("assets", std::make_unique<asset::LooseFileSystem>(Utf8String(assets)), 0);
    asset::AssetManager manager(vfs, nullptr);
    manager.RegisterImporter(std::make_unique<asset::TextureImporter>());
    asset::AssetDatabase database(manager, nullptr);
    MYE_EXPECT(database.ScanDirectory(Utf8String(assets)));
    engine.RegisterServiceRaw(asset::AssetDatabase::kServiceId, &database);
    constexpr char assetPath[] = "assets://characters/novice.png";
    const auto assigned = database.GuidFromPath(assetPath);
    MYE_EXPECT(assigned.IsValid());
    auto& world = app.Project().Active()->World();
    const auto entity = world.Create();
    auto& sprite = world.Add<scene::SpriteRenderer>(entity);
    const asset::AssetRef original{asset::AssetGuid::Generate(), 0xf123456789abcdefULL};
    sprite.sprite = original;
    const auto* type = refl::TypeRegistry::Get().Find(scene::SpriteRenderer::kComponentTypeId);
    MYE_EXPECT(type);
    if (!type) { engine.UnregisterServiceRaw(asset::AssetDatabase::kServiceId); app.Shutdown(); return; }
    ImVec2 target{};
    auto frame = [&](bool source) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(20, 20));
        ImGui::SetNextWindowSize(ImVec2(500, 700));
        ImGui::Begin("asset-drop-test");
        const auto cursor = ImGui::GetCursorScreenPos();
        target = ImVec2(cursor.x + 120, cursor.y + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeight() * .5f);
        if (source && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern | ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
            ImGui::SetDragDropPayload("MYE_ASSET", assetPath, sizeof(assetPath));
            ImGui::EndDragDropSource();
        }
        app.Inspector().DrawReflected(app.Context(), ObjectRef::Component(entity, *type), *type, &sprite, {});
        ImGui::End();
        ImGui::Render();
    };
    frame(false);
    ImGui::GetIO().AddMousePosEvent(target.x, target.y);
    frame(false);
    frame(true);  // Preview the external payload over the real inspector drop target.
    frame(false); // Ending the source delivers it once.
    MYE_EXPECT(sprite.sprite.guid == assigned && sprite.sprite.type == 0);
    MYE_EXPECT(app.Commands().Position() == 1 && app.Commands().IsDirty());
    app.Commands().Undo();
    MYE_EXPECT(sprite.sprite.guid == original.guid && sprite.sprite.type == original.type);
    app.Commands().Redo();
    MYE_EXPECT(sprite.sprite.guid == assigned && sprite.sprite.type == 0);
    app.Commands().MarkSaved();
    frame(true); frame(false);
    MYE_EXPECT(app.Commands().Position() == 1 && !app.Commands().IsDirty());
    engine.UnregisterServiceRaw(asset::AssetDatabase::kServiceId);
    app.Shutdown();
}

MYE_TEST(EditorFrameReacquiresBackbufferAfterUiResize) {
    const auto root = ProjectTestDirectory("frame-resize");
    EditorTestContext engine(root);
    WindowDesc desc;
    desc.title = "Editor frame resize regression";
    desc.clientSize = {640, 420};
    auto window = win32::Win32Window::Create(desc, engine.events);
    MYE_EXPECT(window);
    if (!window) return;
    engine.RegisterServiceRaw(kMainWindowServiceId, static_cast<IWindow*>(window.Value().get()));
    ModuleRegistry modules;
    engine.RegisterServiceRaw(ModuleRegistry::kServiceId, &modules);
    modules.Register(std::make_unique<scene::SceneModule>());
    auto editor = std::make_unique<EditorModule>();
    auto* module = editor.get();
    modules.Register(std::move(editor));
    const auto initialized = modules.InitializeAll(engine);
    MYE_EXPECT(initialized);
    if (!initialized) return;
    MYE_EXPECT(module->App()->CreateProject("Resize", Utf8String(root / "project")));

    struct ResizePanel : IEditorPanel {
        PanelDesc desc{"test.resize", "Resize regression", false, DockSlot::Floating};
        bool& resized;
        explicit ResizePanel(bool& value) : resized(value) {}
        const PanelDesc& Desc() const override { return desc; }
        void OnGui(EditorContext& ctx) override {
            if (!resized) {
                const auto hwnd = static_cast<HWND>(ctx.engine->MainWindow().GetNativeHandle());
                resized = SetWindowPos(hwnd, nullptr, 0, 0, 820, 560, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
            }
        }
    };
    struct ResizeFactory : IEditorPanelFactory {
        PanelDesc desc{"test.resize", "Resize regression", false, DockSlot::Floating};
        bool& resized;
        explicit ResizeFactory(bool& value) : resized(value) {}
        const PanelDesc& Desc() const override { return desc; }
        std::unique_ptr<IEditorPanel> Create() override { return std::make_unique<ResizePanel>(resized); }
    };
    bool resized = false;
    module->App()->Panels().RegisterFactory(std::make_unique<ResizeFactory>(resized));
    module->App()->Panels().Open("test.resize");
    const auto output = root / "resized.bmp";
    bool finished = false;
    module->SetCliControl(true, 1, true, Utf8String(output), [&](int code) { MYE_EXPECT(code == 0); finished = true; });
    window.Value()->PumpMessages();
    modules.Tick(UpdatePhase::PreRender, TimeStep{});
    MYE_EXPECT(resized && finished);
    std::ifstream file(output, std::ios::binary);
    std::array<char, 26> header{};
    file.read(header.data(), static_cast<std::streamsize>(header.size()));
    MYE_EXPECT(file && header[0] == 'B' && header[1] == 'M');
    if (file) {
        int32_t width = 0, height = 0;
        std::memcpy(&width, header.data() + 18, sizeof(width));
        std::memcpy(&height, header.data() + 22, sizeof(height));
        const auto size = window.Value()->GetClientSize();
        MYE_EXPECT(width == size.x && std::abs(height) == size.y);
    }
    modules.ShutdownAll(engine);
}

MYE_TEST(EditorProjectCreateEditSaveReopenPlay) {
    const auto root = ProjectTestDirectory("roundtrip");
    const auto project = root / Utf8Path("한글 프로젝트");
    EditorTestContext engine(root);
    EditorApp app;
    MYE_EXPECT(app.Initialize(engine, ""));
    auto created = app.CreateProject("한글 프로젝트", Utf8String(project));
    MYE_EXPECT(created);
    if (!created) return;
    MYE_EXPECT(std::filesystem::exists(project / "project.myeproj"));
    auto* doc = app.Project().Active();
    MYE_EXPECT(doc && !doc->IsDirty());
    MYE_EXPECT(app.Context().activeWorld() == &doc->World());
    auto createParent = std::make_unique<CreateEntityCommand>();
    auto* parentCommand = createParent.get();
    app.Commands().Push(std::move(createParent));
    const auto parent = parentCommand->Created();
    auto createChild = std::make_unique<CreateEntityCommand>(parent);
    auto* childCommand = createChild.get();
    app.Commands().Push(std::move(createChild));
    const auto child = childCommand->Created();
    doc->World().TryGet<scene::LocalTransform>(child)->position.x = 7.0f;
    MYE_EXPECT(doc->IsDirty());
    MYE_EXPECT(app.SaveProject());
    MYE_EXPECT(!doc->IsDirty());
    auto opened = app.OpenProject(Utf8String(project / "project.myeproj"));
    MYE_EXPECT(opened);
    if (!opened) return;
    auto* world = app.Context().activeWorld();
    int count = 0, parentLinks = 0;
    world->Query<scene::LocalTransform>().Each([&](ecs::Entity, scene::LocalTransform&) { ++count; });
    world->Query<scene::Parent, scene::LocalTransform>().Each(
        [&](ecs::Entity, scene::Parent& p, scene::LocalTransform& t) {
            if (!p.parent.IsNull()) { ++parentLinks; MYE_EXPECT(world->Valid(p.parent)); MYE_EXPECT(t.position.x == 7.0f); }
        });
    MYE_EXPECT(count == 2 && parentLinks == 1);
    MYE_EXPECT(app.PlayMode().Play());
    app.RefreshDocumentContext();
    MYE_EXPECT(app.Context().activeWorld() != world);
    int playLinks = 0;
    app.Context().activeWorld()->Query<scene::Parent>().Each(
        [&](ecs::Entity, scene::Parent& p) { if (!p.parent.IsNull()) ++playLinks; });
    MYE_EXPECT(playLinks == 1);
    MYE_EXPECT(!app.SaveProject());
    app.PlayMode().Stop();
    app.RefreshDocumentContext();
    MYE_EXPECT(app.Context().activeWorld() == world);
    ecs::Entity loadedParent{}, loadedChild{};
    world->Query<scene::Parent>().Each([&](ecs::Entity e, scene::Parent& p) {
        if (!p.parent.IsNull()) { loadedParent = p.parent; loadedChild = e; }
    });
    world->Add<scene::Parent>(loadedParent).parent = loadedChild;
    MYE_EXPECT(!app.PlayMode().Play());
    MYE_EXPECT(app.PlayMode().State() == PlayState::Edit && app.PlayMode().ActiveWorld() == world);
    world->TryGet<scene::Parent>(loadedParent)->parent = {};
    MYE_EXPECT(app.SaveScene("assets/scenes/renamed.scene"));
    MYE_EXPECT(!app.Project().Active()->IsDirty() && app.Project().HasUnsavedChanges());
    MYE_EXPECT(!app.OpenProject(Utf8String(project / "project.myeproj")));
    const auto projectStaging = project / "project.myeproj.tmp";
    MYE_EXPECT(WriteJsonFile(projectStaging, json::Value(std::string("project recovery data"))));
    MYE_EXPECT(!app.SaveProject() && app.Project().HasUnsavedChanges());
    const auto oldMetadata = ReadJsonFile(project / "project.myeproj");
    MYE_EXPECT(oldMetadata && oldMetadata.Value().Find("mainScene")->AsString() == "assets/scenes/main.scene");
    std::error_code ec;
    std::filesystem::remove(projectStaging, ec);
    MYE_EXPECT(app.SaveProject());
    MYE_EXPECT(!app.Project().HasUnsavedChanges());
    MYE_EXPECT(app.OpenProject(Utf8String(project / "project.myeproj")));
    MYE_EXPECT(Utf8Path(app.Project().Active()->Path()).filename() == "renamed.scene");
    app.Shutdown();
}

MYE_TEST(EditorProjectDocumentsAndFailedWritesPreserveData) {
    const auto root = ProjectTestDirectory("preservation");
    EditorTestContext engine(root);
    EditorApp app;
    MYE_EXPECT(app.Initialize(engine, ""));
    MYE_EXPECT(app.CreateProject("A", Utf8String(root / "A")));
    auto* first = app.Project().Active();
    if (!first) return;
    const auto firstId = first->Id();
    const std::string firstPath(first->Path());
    const auto original = ReadJsonFile(Utf8Path(firstPath));
    MYE_EXPECT(original);
    app.Commands().Push(std::make_unique<CreateEntityCommand>());
    MYE_EXPECT(first->IsDirty());
    std::error_code ec;
    const auto staging = Utf8Path(firstPath + ".tmp");
    std::filesystem::create_directory(staging, ec);
    MYE_EXPECT(!app.SaveScene(firstPath));
    MYE_EXPECT(first->IsDirty() && first->Path() == firstPath);
    auto unchanged = ReadJsonFile(Utf8Path(firstPath));
    MYE_EXPECT(unchanged && json::Stringify(unchanged.Value()) == json::Stringify(original.Value()));
    std::filesystem::remove(staging, ec);
    MYE_EXPECT(WriteJsonFile(staging, json::Value(std::string("existing temporary data"))));
    MYE_EXPECT(!app.SaveScene(firstPath));
    auto temporary = ReadJsonFile(staging);
    MYE_EXPECT(temporary && temporary.Value().AsString() == "existing temporary data");
    std::filesystem::remove(staging, ec);
    MYE_EXPECT(!app.CreateProject("overwrite", Utf8String(root / "A"), true));
    MYE_EXPECT(!app.OpenProject(Utf8String(root / "missing"), true));
    MYE_EXPECT(app.Project().Active() == first && first->IsDirty());

    auto* second = app.Project().NewScene();
    app.ActivateDocument(second->Id());
    MYE_EXPECT(app.Context().activeWorld() == &second->World());
    int count = 0;
    second->World().Query<scene::LocalTransform>().Each([&](ecs::Entity, scene::LocalTransform&) { ++count; });
    MYE_EXPECT(count == 0);
    MYE_EXPECT(!app.SaveProject()); // Name selection is required before any project save.
    MYE_EXPECT(!app.SaveScene(firstPath)); // Another document owns this file.
    MYE_EXPECT(!app.SaveScene("assets/scenes/MAIN.scene")); // Windows case aliases share ownership.
    MYE_EXPECT(!app.SaveScene(Utf8String(root / "outside.scene")));
    MYE_EXPECT(second->Path().empty() && second->IsDirty());
    MYE_EXPECT(app.SaveScene("assets/scenes/second.scene"));
    app.Selection().Select(SelectableRef::OfEntity(ecs::Entity{0, 1}));
    app.ActivateDocument(firstId);
    MYE_EXPECT(app.Context().activeWorld() == &first->World());
    MYE_EXPECT(app.Selection().Empty() && !app.Selection().CanNavigateBack());
    MYE_EXPECT(first->IsDirty() && !second->IsDirty());
    MYE_EXPECT(app.SaveProject());
    const auto duplicate = app.Project().OpenScene("assets/scenes/./main.scene");
    MYE_EXPECT(duplicate && duplicate.Value() == first);
    const auto caseDuplicate = app.Project().OpenScene("assets/scenes/MAIN.scene");
    MYE_EXPECT(caseDuplicate && caseDuplicate.Value() == first);
    MYE_EXPECT(app.Project().Documents().size() == 2);
    const auto layoutStaging = Utf8Path(app.Project().SessionJsonPath() + ".tmp");
    MYE_EXPECT(WriteJsonFile(layoutStaging, json::Value(std::string("layout recovery data"))));
    app.Commands().Push(std::make_unique<CreateEntityCommand>());
    MYE_EXPECT(app.SaveProject()); // Content succeeds even if local layout preferences cannot be written.
    MYE_EXPECT(!app.Project().HasUnsavedChanges());
    auto layoutRecovery = ReadJsonFile(layoutStaging);
    MYE_EXPECT(layoutRecovery && layoutRecovery.Value().AsString() == "layout recovery data");
    std::filesystem::remove(layoutStaging, ec);
    app.Shutdown();
}

MYE_TEST(EditorProjectRejectsBrokenSceneAndMetadata) {
    const auto root = ProjectTestDirectory("invalid");
    ProjectContext project;
    auto created = project.Create("Valid", Utf8String(root / "valid"));
    MYE_EXPECT(created);
    if (!created) return;
    auto* original = project.Active();
    const auto scenePath = root / "valid" / "assets" / "scenes" / "broken.scene";
    const std::string invalid[] = {
        "{",
        R"({"__version":2,"entities":[]})",
        R"({"__version":1,"entities":[{"id":1,"components":{"UnknownComponent":{}}}]})",
        R"({"__version":1,"entities":[{"id":1,"components":{"LocalTransform":42}}]})",
        R"({"__version":1,"entities":[{"id":1,"parent":2,"components":{}},{"id":2,"parent":1,"components":{}}]})",
        R"({"__version":1,"entities":[{"id":1,"parent":7,"components":{}}]})",
        R"({"__version":1,"entities":[{"id":1,"components":{}},{"id":1,"components":{}}]})"
    };
    for (const auto& text : invalid) {
        { std::ofstream file(scenePath, std::ios::binary | std::ios::trunc); file << text; }
        auto opened = project.OpenScene(Utf8String(scenePath));
        MYE_EXPECT(!opened);
        MYE_EXPECT(project.Active() == original && project.Documents().size() == 1);
    }
    MYE_EXPECT(!project.OpenScene("../outside.scene"));
    const auto metadata = root / "valid" / "bad.myeproj";
    MYE_EXPECT(WriteJsonFile(metadata, json::Value(json::Value::Object{
        {"version", json::Value(int64_t{2})}, {"name", json::Value(std::string("Future"))},
        {"mainScene", json::Value(std::string(""))}})));
    MYE_EXPECT(!project.Open(Utf8String(metadata)));
    MYE_EXPECT(project.Active() == original);
    project.NewScene();
    MYE_EXPECT(!project.Open(Utf8String(root / "valid" / "project.myeproj")));
    MYE_EXPECT(project.HasUnsavedChanges());
}

MYE_TEST(EditorLauncherCreatesStarterExplicitlyAndPreservesUserEdits) {
    const auto root = ProjectTestDirectory("starter-startup");
    EditorTestContext engine(root);
    EditorApp app;
    MYE_EXPECT(app.Initialize(engine, "", MYE_STARTER_SOURCE_DIR));
    MYE_EXPECT(!app.Project().IsOpen());
    MYE_EXPECT(app.CreateProject("초원마을", Utf8String(root / "project"), false, true));
    MYE_EXPECT(app.Project().IsOpen() && app.Project().Name() == "초원마을");
    const auto projectFile = std::string(app.Project().ProjectFilePath());
    const auto mainPath = std::string(app.Project().Active()->Path());
    const auto initial = ReadJsonFile(Utf8Path(mainPath));
    MYE_EXPECT(initial);
    const auto initialCount = initial.Value().Find("entities")->AsArray().size();
    app.Commands().Push(std::make_unique<CreateEntityCommand>());
    MYE_EXPECT(app.SaveProject());
    const auto saved = ReadJsonFile(Utf8Path(mainPath));
    MYE_EXPECT(saved && saved.Value().Find("entities")->AsArray().size() == initialCount + 1);
    MYE_EXPECT(app.OpenAnimation("assets/animations/novice_idle.anim"));
    auto* animation = app.AnimationDocument();
    if (animation) {
        auto after = animation->Animation();
        after.clip.frameDurations[0] = .25f;
        animation->Commands().Push(std::make_unique<AnimAssetEditCommand>(animation->Animation(), after, "duration"));
        app.SetAnimationFocused();
        app.SaveActive();
        MYE_EXPECT(!animation->IsDirty()); // Save targets the focused animation document.
    }
    app.Shutdown();
    EditorApp reopened;
    MYE_EXPECT(reopened.Initialize(engine, projectFile, MYE_STARTER_SOURCE_DIR));
    MYE_EXPECT(reopened.Project().ProjectFilePath() == projectFile);
    const auto retained = ReadJsonFile(Utf8Path(mainPath));
    MYE_EXPECT(retained && retained.Value().Find("entities")->AsArray().size() == initialCount + 1);
    reopened.Shutdown();
}


MYE_TEST(PlayWindowReopensWithoutRetainingInputOrResources) {
    auto device = rhi::CreateDevice(rhi::Backend::DX11, {});
    MYE_EXPECT(device);
    if (!device) return;
    PlayWindow window;
    MYE_EXPECT(window.Open(*device.Value()));
    MYE_EXPECT(window.IsOpen() && !window.CloseRequested());
    window.Input().SetKeyboardSuppressed(false);
    window.Input().OnKey(KeyCode::D, true);
    MYE_EXPECT(window.Input().IsDown(KeyCode::D));
    const auto handle = FindWindowW(nullptr, L"MyEngine — Play");
    DWORD processId = 0; GetWindowThreadProcessId(handle, &processId);
    MYE_EXPECT(handle && processId == GetCurrentProcessId());
    if (handle && processId == GetCurrentProcessId()) {
        SendMessageW(handle, WM_SETFOCUS, 0, 0);
        auto bindings = runtime::DefaultGameInputMap();
        for (auto& action : bindings.actions) if (action.name == "interact")
            action.bindings = {{InputDevice::MouseButton, static_cast<int>(MouseButton::X1)}};
        runtime::GameInputBuffer buffer; MYE_EXPECT(buffer.Configure(bindings));
        window.Input().OnKey(KeyCode::D, false);
        window.Input().NewFrame();
        SendMessageW(handle, WM_XBUTTONDOWN, MAKEWPARAM(MK_XBUTTON1, XBUTTON1), MAKELPARAM(10,10));
        SendMessageW(handle, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON1), MAKELPARAM(10,10));
        SendMessageW(handle, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
        SendMessageW(handle, WM_KEYDOWN, 'W', LPARAM{0x11} << 16);
        buffer.Capture(window.Input(), true);
        const auto controls = buffer.ConsumeTick();
        MYE_EXPECT(controls.interact && controls.cameraZoomSteps == 1 && controls.movement == Vec2{0,1});
        MYE_EXPECT(!buffer.ConsumeTick().interact && buffer.ConsumeTick().cameraZoomSteps == 0);
        SendMessageW(handle, WM_KILLFOCUS, 0, 0);
        MYE_EXPECT(!window.Input().IsDown(KeyCode::W) && window.Input().IsMouseSuppressed());
        SendMessageW(handle, WM_SETFOCUS, 0, 0);
        window.Input().NewFrame(); buffer.Capture(window.Input(), true);
        MYE_EXPECT(buffer.ConsumeTick().movement == Vec2{});
    }
    window.Close();
    MYE_EXPECT(!window.IsOpen() && !window.HasFocus() && !window.Input().IsDown(KeyCode::D));
    MYE_EXPECT(window.Open(*device.Value()));
    MYE_EXPECT(!window.Input().IsDown(KeyCode::D));
    window.Close();
}

MYE_TEST(EditorWorkspacePreservesDocumentsAndSwitchesProjection) {
    const auto root = ProjectTestDirectory("workspace");
    EditorTestContext engine(root);
    EditorApp app;
    TestEditorViewport viewport;
    MYE_EXPECT(app.Initialize(engine, ""));
    app.SetViewport(&viewport);
    MYE_EXPECT(!app.RequestInputSettings());
    MYE_EXPECT(!app.CreateScene(false));
    MYE_EXPECT(app.CreateProject("Workspace", Utf8String(root / "project")));
    MYE_EXPECT(app.RequestInputSettings());
    const auto* initial = app.Project().Active();
    const auto count = app.Project().Documents().size();
    const auto position = app.Commands().Position();
    viewport.camera.center = {4, 7}; viewport.camera.zoom = 2;
    app.SelectWorkspace(EditorApp::Workspace::Scene3D);
    MYE_EXPECT(viewport.camera.perspective && viewport.camera.center.x == 4 && viewport.camera.zoom == 2);
    app.SelectWorkspace(EditorApp::Workspace::Lua);
    MYE_EXPECT(app.CentralPanelId() == "mye.lua");
    MYE_EXPECT(app.Project().Active() == initial && app.Project().Documents().size() == count && app.Commands().Position() == position);
    MYE_EXPECT(app.CreateScene(true));
    MYE_EXPECT(app.CurrentWorkspace() == EditorApp::Workspace::Scene3D && viewport.camera.perspective);
    MYE_EXPECT(app.Project().Documents().size() == count + 1);
    MYE_EXPECT(app.PlayMode().Play());
    MYE_EXPECT(!app.CreateScene(false));
    MYE_EXPECT(!app.CreateSceneElement(EditorApp::SceneElement::Object));
    app.PlayMode().Stop();
    app.Shutdown();
}

MYE_TEST(EditorSceneElementsUndoRedoAndValidation) {
    const auto root = ProjectTestDirectory("elements");
    EditorTestContext engine(root);
    EditorApp app;
    MYE_EXPECT(app.Initialize(engine, ""));
    MYE_EXPECT(app.CreateProject("Elements", Utf8String(root / "project")));
    auto& world = app.Project().Active()->World();
    const auto parent = app.CreateSceneElement(EditorApp::SceneElement::Object);
    MYE_EXPECT(parent);
    const auto child = app.CreateSceneElement(EditorApp::SceneElement::Sprite, parent.Value());
    MYE_EXPECT(child && world.Has<scene::SpriteRenderer>(child.Value()));
    const auto name = world.TryGet<scene::ObjectName>(child.Value())->value;
    app.Commands().Undo();
    MYE_EXPECT(!world.Valid(child.Value()));
    const auto* children = world.TryGet<scene::Children>(parent.Value());
    MYE_EXPECT(!children || children->list.empty());
    app.Commands().Redo();
    MYE_EXPECT(world.Valid(child.Value()) && world.Has<scene::SpriteRenderer>(child.Value()));
    MYE_EXPECT(world.TryGet<scene::ObjectName>(child.Value())->value == name);
    MYE_EXPECT(world.TryGet<scene::Parent>(child.Value())->parent == parent.Value());
    app.Commands().Undo(); app.Commands().Redo();
    MYE_EXPECT(world.Valid(child.Value()) && world.Has<scene::SpriteRenderer>(child.Value()));
    const auto trigger = app.CreateSceneElement(EditorApp::SceneElement::Trigger);
    MYE_EXPECT(trigger && world.TryGet<phys::Collider2D>(trigger.Value())->isTrigger);
    MYE_EXPECT(world.Has<runtime::ObjectBehavior>(trigger.Value()));
    const auto player = app.CreateSceneElement(EditorApp::SceneElement::Character);
    MYE_EXPECT(player && world.Has<runtime::CharacterController2D>(player.Value()) && world.Has<phys::KinematicBody2D>(player.Value()));
    const auto position = app.Commands().Position();
    MYE_EXPECT(!app.CreateSceneElement(EditorApp::SceneElement::Character));
    MYE_EXPECT(!app.CreateSceneElement(EditorApp::SceneElement::Character, parent.Value()));
    MYE_EXPECT(!app.CreateSceneElement(static_cast<EditorApp::SceneElement>(255)));
    MYE_EXPECT(app.Commands().Position() == position);
    const auto mesh = app.CreateSceneElement(EditorApp::SceneElement::Mesh);
    const auto billboard = app.CreateSceneElement(EditorApp::SceneElement::Billboard);
    const auto camera = app.CreateSceneElement(EditorApp::SceneElement::Camera);
    MYE_EXPECT(mesh && world.Has<scene::MeshRenderer>(mesh.Value()));
    MYE_EXPECT(billboard && world.Has<scene::BillboardRenderer>(billboard.Value()));
    MYE_EXPECT(camera && world.Has<scene::Camera3D>(camera.Value()));
    MYE_EXPECT(world.TryGet<scene::LocalTransform>(camera.Value())->position.z == -8);
    app.Commands().Undo(); MYE_EXPECT(!world.Valid(camera.Value()));
    app.Commands().Redo(); MYE_EXPECT(world.Has<scene::Camera3D>(camera.Value()));
    MYE_EXPECT(app.PlayMode().Play());
    auto view = scene::BuildGameView(*app.PlayMode().ActiveWorld(), render::Camera2D{});
    MYE_EXPECT(view && view.Value().geometryDepth);
    app.PlayMode().Pause(); app.PlayMode().Stop();
    MYE_EXPECT(app.PlayMode().ActiveWorld() == &world);
    MYE_EXPECT(runtime::ValidateObjectComponents(world));
    const auto path = Utf8String(root / "project/assets/scenes/elements.scene");
    MYE_EXPECT(app.SaveScene(path));
    app.Shutdown();
    EditorApp reopened;
    MYE_EXPECT(reopened.Initialize(engine, Utf8String(root / "project/project.myeproj")));
    MYE_EXPECT(reopened.OpenScene(path));
    bool sprite = false, character = false, cameraLoaded = false;
    auto& loaded = reopened.Project().Active()->World();
    loaded.Query<scene::ObjectName>().Each([&](ecs::Entity e, const auto& object) {
        if (object.value == name) sprite = loaded.Has<scene::SpriteRenderer>(e) && loaded.Has<scene::Parent>(e);
        if (object.value == "캐릭터") character = loaded.Has<runtime::CharacterController2D>(e);
        if (object.value == "게임 카메라 3D") cameraLoaded = loaded.Has<scene::Camera3D>(e);
    });
    MYE_EXPECT(sprite && character && cameraLoaded && runtime::ValidateObjectComponents(loaded));
    reopened.Shutdown();
}

MYE_TEST(EditorTwoDCameraElementUndoSaveReopenAndPlayIsolation) {
    const auto root = ProjectTestDirectory("camera2d");
    EditorTestContext engine(root);
    EditorApp app;
    MYE_EXPECT(app.Initialize(engine, ""));
    MYE_EXPECT(app.CreateProject("Camera", Utf8String(root / "project")));
    auto& world = app.Project().Active()->World();
    const auto camera = app.CreateSceneElement(EditorApp::SceneElement::Camera2D);
    MYE_EXPECT(camera && world.TryGet<scene::Camera2D>(camera.Value())->current);
    if (!camera) return;
    app.Commands().Undo(); MYE_EXPECT(!world.Valid(camera.Value()));
    app.Commands().Redo(); MYE_EXPECT(world.Has<scene::Camera2D>(camera.Value()));
    world.TryGet<scene::Camera2D>(camera.Value())->zoom = 2;
    const auto other = app.CreateSceneElement(EditorApp::SceneElement::Camera);
    MYE_EXPECT(other && !world.TryGet<scene::Camera3D>(other.Value())->current);
    MYE_EXPECT(app.PlayMode().Play());
    MYE_EXPECT(!app.RequestInputSettings());
    auto* playWorld = app.PlayMode().ActiveWorld();
    playWorld->Query<scene::Camera2D>().Each([](ecs::Entity, auto& c) { c.zoom = 3; });
    MYE_EXPECT(app.PlayMode().Tick(.02f, runtime::GameInput{}, ""));
    const auto view = scene::BuildGameView(*playWorld, app.PlayMode().DefaultCamera());
    MYE_EXPECT(view && !view.Value().geometryDepth);
    app.PlayMode().Stop();
    MYE_EXPECT(world.TryGet<scene::Camera2D>(camera.Value())->zoom == 2);
    const auto path = Utf8String(root / "project/assets/scenes/camera.scene");
    MYE_EXPECT(app.SaveScene(path));
    app.Shutdown();
    EditorApp reopened;
    MYE_EXPECT(reopened.Initialize(engine, Utf8String(root / "project/project.myeproj")));
    MYE_EXPECT(reopened.OpenScene(path));
    bool found = false;
    reopened.Project().Active()->World().Query<scene::Camera2D>().Each([&](ecs::Entity, const auto& c) {
        found = true; MYE_EXPECT(c.current && c.zoom == 2 && !c.initialized);
    });
    MYE_EXPECT(found);
    reopened.Shutdown();
}

MYE_TEST(EditorWorkspaceImGuiRoutingAndDialogs) {
    struct GuiScope {
        GuiScope() { ImGui::CreateContext(); }
        ~GuiScope() { ImGui::DestroyContext(); }
    } gui;
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1600, 900); io.DeltaTime = 1.0f / 60;
    unsigned char* pixels = nullptr; int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    const auto root = ProjectTestDirectory("workspace-ui");
    EditorTestContext engine(root);
    EditorApp app;
    TestEditorViewport viewport;
    MYE_EXPECT(app.Initialize(engine, ""));
    app.SetViewport(&viewport);
    auto frame = [&]() { ImGui::NewFrame(); app.OnFrame(); ImGui::Render(); };
    frame();
    MYE_EXPECT(!app.Project().IsOpen());
    MYE_EXPECT(ImGui::FindWindowByName("프로젝트###project_launcher")->Active);
    MYE_EXPECT(!ImGui::FindWindowByName("하이어라키###mye.hierarchy"));
    MYE_EXPECT(app.CreateProject("UI", Utf8String(root / "project")));
    frame(); frame();
    // Exercise the actual input widgets with ImGui activation, not a draft setter.
    const auto defaults = app.Project().InputSettings();
    MYE_EXPECT(app.RequestInputSettings()); frame(); frame();
    auto* settings = ImGui::FindWindowByName("입력 설정###MyEngineInputSettings");
    MYE_EXPECT(settings && settings->Active);
    if (settings) {
        ImGui::ActivateItemByID(settings->GetID("##ActionName")); frame();
        io.AddInputCharactersUTF8("attack"); frame();
        ImGui::ActivateItemByID(settings->GetID("조작 추가")); frame();
        MYE_EXPECT(app.Project().InputSettings() == defaults); // Still a draft.
        ImGui::ActivateItemByID(settings->GetID("프로젝트에 저장")); frame();
        MYE_EXPECT(app.Project().InputSettings().actions.size() == defaults.actions.size() + 1);
        MYE_EXPECT(app.Project().InputSettings().actions.back().name == "attack");
        MYE_EXPECT(app.RequestInputSettings()); frame(); frame();
        ImGuiWindow* actions = nullptr;
        for (auto* window : ImGui::GetCurrentContext()->Windows)
            if (window->ParentWindow == settings && window->ChildId == settings->GetID("Actions")) actions = window;
        MYE_EXPECT(actions);
        if (actions) {
            const auto scope = ImHashStr("attack", 0, actions->ID);
            const auto tree = ImHashStr("attack", 0, scope);
            if (!actions->StateStorage.GetBool(tree)) { ImGui::ActivateItemByID(tree); frame(); }
            ImGui::ActivateItemByID(ImHashStr("입력 추가", 0, tree)); frame();
            ImGui::ActivateItemByID(settings->GetID("프로젝트에 저장")); frame();
            MYE_EXPECT(app.Project().InputSettings().actions.back().bindings ==
                std::vector<InputBinding>{{InputDevice::Key, static_cast<int>(KeyCode::Space)}});
            MYE_EXPECT(app.RequestInputSettings()); frame(); frame();
            ImGui::ActivateItemByID(ImHashStr("조작 제거", 0, tree)); frame();
            MYE_EXPECT(app.Project().InputSettings().actions.size() == defaults.actions.size() + 1);
            ImGui::ActivateItemByID(settings->GetID("프로젝트에 저장")); frame();
            MYE_EXPECT(app.Project().InputSettings() == defaults);
        }
    }
    const auto* hierarchy = ImGui::FindWindowByName("하이어라키###mye.hierarchy");
    const auto* assets = ImGui::FindWindowByName("에셋###mye.assets");
    const auto* inspector = ImGui::FindWindowByName("인스펙터###mye.inspector");
    const auto* animation = ImGui::FindWindowByName("애니메이션###mye.anim");
    MYE_EXPECT(hierarchy && assets && inspector && animation);
    MYE_EXPECT(hierarchy->DockId != assets->DockId && inspector->DockId != animation->DockId);
    MYE_EXPECT(hierarchy->Pos.y < assets->Pos.y && inspector->Pos.y < animation->Pos.y);
    app.SelectWorkspace(EditorApp::Workspace::Lua); frame();
    MYE_EXPECT(ImGui::FindWindowByName("Lua###mye.lua")->Active);
    MYE_EXPECT(!ImGui::FindWindowByName("씬 뷰포트###mye.viewport")->Active);
    app.NewScene(); frame();
    auto* newSceneDialog = ImGui::FindWindowByName("씬 만들기###new_scene");
    MYE_EXPECT(newSceneDialog && newSceneDialog->Active);
    const auto documentsBefore = app.Project().Documents().size();
    if (newSceneDialog) { ImGui::ActivateItemByID(newSceneDialog->GetID("3D 씬")); frame(); }
    MYE_EXPECT(app.Project().Documents().size() == documentsBefore + 1);
    MYE_EXPECT(app.CurrentWorkspace() == EditorApp::Workspace::Scene3D);
    frame();
    app.RequestAddElement(); frame();
    auto* addDialog = ImGui::FindWindowByName("요소 추가###add_scene_element");
    MYE_EXPECT(addDialog && addDialog->Active);
    frame(); // Keyboard focus requested when the dialog appears is applied on the next frame.
    MYE_EXPECT(addDialog && ImGui::GetActiveID() == addDialog->GetID("##element_search"));
    io.AddInputCharactersUTF8("트리거"); frame();
    io.AddKeyEvent(ImGuiKey_Enter, true); frame();
    io.AddKeyEvent(ImGuiKey_Enter, false); frame();
    const auto added = app.Selection().Primary();
    auto& world = app.Project().Active()->World();
    MYE_EXPECT(added.IsEntity() && world.Has<phys::Collider2D>(added.AsEntity()));
    if (added.IsEntity()) {
        const auto* collider = world.TryGet<phys::Collider2D>(added.AsEntity());
        MYE_EXPECT(collider && collider->isTrigger);
    }
    app.SelectWorkspace(EditorApp::Workspace::Lua); frame();
    auto* lua = ImGui::FindWindowByName("Lua###mye.lua");
    MYE_EXPECT(lua && lua->Active);
    if (lua) { ImGui::ActivateItemByID(lua->GetID("기본 코드 만들기")); frame(); }
    const auto* behavior = world.TryGet<runtime::ObjectBehavior>(added.AsEntity());
    MYE_EXPECT(behavior && !behavior->luaSource.empty());
    if (behavior) {
        const auto source = behavior->luaSource;
        app.Commands().Undo();
        MYE_EXPECT(behavior->luaSource.empty());
        app.Commands().Redo();
        MYE_EXPECT(behavior->luaSource == source);
    }
    // Activate the real header button, so the test covers its action binding.
    auto* menu = ImGui::FindWindowByName("##MainMenuBar");
    MYE_EXPECT(menu);
    const auto menuScope = ImHashStr("##MenuBar", 0, menu->ID);
    const auto activate = [&](const char* label) { ImGui::ActivateItemByID(ImHashStr(label, 0, menuScope)); frame(); };
    activate("3D");
    MYE_EXPECT(app.CurrentWorkspace() == EditorApp::Workspace::Scene3D && viewport.camera.perspective);
    activate("##run");
    MYE_EXPECT(app.PlayMode().State() == PlayState::Playing);
    activate("##pause");
    MYE_EXPECT(app.PlayMode().State() == PlayState::Paused);
    activate("##run");
    MYE_EXPECT(app.PlayMode().State() == PlayState::Playing);
    activate("##stop");
    MYE_EXPECT(app.PlayMode().State() == PlayState::Edit);
    io.DisplaySize = ImVec2(640, 480); frame();
    app.SelectWorkspace(EditorApp::Workspace::Lua);
    app.Shutdown();
    EditorApp reopened;
    MYE_EXPECT(reopened.Initialize(engine, Utf8String(root / "project/project.myeproj")));
    MYE_EXPECT(reopened.CurrentWorkspace() == EditorApp::Workspace::Lua);
    reopened.Shutdown();
}
