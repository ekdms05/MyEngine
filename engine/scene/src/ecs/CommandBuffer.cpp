// mye/ecs/CommandBuffer.cpp — 지연 구조변경 구현 (docs/03 §2)
#include "mye/ecs/CommandBuffer.h"
#include "mye/ecs/World.h"

#include <cstring>

namespace mye::ecs {

namespace {

enum class CmdKind { Create, Destroy, Add, Remove, SetParent };

struct Command {
    CmdKind kind;
    Entity  entity;
    Entity  parent;                     // SetParent
    bool    keepWorld = true;
    ComponentTypeId type = 0;

    // Add(템플릿): World에 적용하는 클로저.
    std::function<void(World&, Entity)> applyAdd;

    // AddDynamic: 복사 저장한 원소 바이트 + 서술자(값 보관 — Flush까지 원본 desc 수명에 의존 금지).
    bool               hasDesc = false;
    ComponentTypeDesc  desc{};
    std::vector<std::byte> valueBytes;
};

} // namespace

// reparent 후크(전역, scene 계층이 설치). 미설치 시 SetParent Flush는 no-op.
static CommandBuffer::ReparentHook g_reparentHook = nullptr;

struct CommandBuffer::Impl {
    World* world = nullptr;
    std::vector<Command> commands;
    // CreateDeferred가 예약한 엔티티 — Flush 시 World에 실제 생성 반영.
    std::vector<Entity>  reservedCreates;
};

CommandBuffer::CommandBuffer(World& world) : m_impl(std::make_unique<Impl>()) {
    m_impl->world = &world;
}
CommandBuffer::~CommandBuffer() = default;

Entity CommandBuffer::CreateDeferred() {
    // 즉시 유효 핸들을 반환하려면 World가 index/generation을 예약해야 한다.
    // M2-A 기준 구현: World::Create를 즉시 호출해 핸들을 확보한다(같은 프레임 참조 연결 가능).
    // 저장소상 엔티티는 이미 존재하되 컴포넌트 add는 Flush 순서로 적용된다.
    Entity e = m_impl->world->Create();
    Command c; c.kind = CmdKind::Create; c.entity = e;
    m_impl->commands.push_back(std::move(c));
    return e;
}

void CommandBuffer::Destroy(Entity e) {
    Command c; c.kind = CmdKind::Destroy; c.entity = e;
    m_impl->commands.push_back(std::move(c));
}

void CommandBuffer::AddDynamic(Entity e, const ComponentTypeDesc& desc, const void* valueBytes) {
    Command c; c.kind = CmdKind::Add; c.entity = e; c.type = desc.id;
    c.hasDesc = true; c.desc = desc;   // 값 보관 — 원본 desc가 Flush 전에 소멸해도 안전.
    c.valueBytes.resize(desc.size);
    std::memcpy(c.valueBytes.data(), valueBytes, desc.size);
    m_impl->commands.push_back(std::move(c));
}

void CommandBuffer::RemoveDynamic(Entity e, ComponentTypeId type) {
    Command c; c.kind = CmdKind::Remove; c.entity = e; c.type = type;
    m_impl->commands.push_back(std::move(c));
}

void CommandBuffer::SetParent(Entity child, Entity parent, bool keepWorld) {
    Command c; c.kind = CmdKind::SetParent; c.entity = child; c.parent = parent;
    c.keepWorld = keepWorld;
    m_impl->commands.push_back(std::move(c));
}

void CommandBuffer::RecordAddDynamic(Entity e, ComponentTypeId type,
                                     std::function<void(World&, Entity)> apply) {
    Command c; c.kind = CmdKind::Add; c.entity = e; c.type = type;
    c.applyAdd = std::move(apply);
    m_impl->commands.push_back(std::move(c));
}

void CommandBuffer::RecordRemoveDynamic(Entity e, ComponentTypeId type) {
    RemoveDynamic(e, type);
}

void CommandBuffer::SetReparentHook(ReparentHook hook) { g_reparentHook = hook; }

bool CommandBuffer::Empty() const { return m_impl->commands.empty(); }

void CommandBuffer::Flush() {
    World& w = *m_impl->world;
    for (auto& c : m_impl->commands) {
        switch (c.kind) {
        case CmdKind::Create:
            // 이미 CreateDeferred에서 생성됨 — no-op(순서 표식용).
            break;
        case CmdKind::Destroy:
            w.Destroy(c.entity);
            break;
        case CmdKind::Add:
            if (c.applyAdd) {
                c.applyAdd(w, c.entity);              // 템플릿 경로
            } else if (c.hasDesc) {
                w.RegisterComponent(c.desc);         // 동적 경로(값 보관 desc)
                void* dst = w.AddDynamic(c.entity, c.type);
                if (dst && c.desc.moveConstruct) {
                    // valueBytes → 슬롯으로 이동(복사 저장했던 값 이관).
                    c.desc.moveConstruct(dst, c.valueBytes.data());
                }
            }
            break;
        case CmdKind::Remove:
            w.RemoveDynamic(c.entity, c.type);
            break;
        case CmdKind::SetParent:
            // Parent/Children 갱신 + keepWorld 월드 보정은 scene::ApplyReparent가 수행.
            // ecs 계층은 scene에 직접 의존하지 않으므로 설치된 후크로 위임(미설치 시 no-op).
            if (g_reparentHook) g_reparentHook(w, c.entity, c.parent, c.keepWorld);
            break;
        }
    }
    m_impl->commands.clear();
}

} // namespace mye::ecs
