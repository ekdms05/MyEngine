// Compare the previous per-stack replay with the current full reconciliation.
// Run after a Release engine build; this measures CPU reconciliation, not game FPS.
#include "mye/persist/PersistenceService.h"
#include <chrono>
#include <iostream>

using namespace mye::persist;

static bool PreviousReconcile(const PersistenceService& service, CharacterId id) {
    const auto* record = service.Characters().Get(id);
    if (!record || record->gold != service.Ledger().GoldBalance(id)) return false;
    for (const auto& stack : record->items)
        if (service.Ledger().ItemBalance(id, stack.itemId) != stack.count) return false;
    return true;
}

int main() {
    PersistenceService service;
    // No account login or disk I/O in the timed region.
    const auto character = service.Characters().Create(1, "Bench");
    if (!character) return 1;
    constexpr int kItems = 100, kEntries = 10'000, kRepeats = 100;
    for (int i = 0; i < kEntries; ++i)
        if (!service.Ledger().Grant(character.Value(), static_cast<uint32_t>(i % kItems + 1), 1)) return 1;
    for (int i = 1; i <= kItems; ++i)
        service.Characters().GetMutable(character.Value())->items.push_back({static_cast<uint32_t>(i), kEntries / kItems});
    if (!PreviousReconcile(service, character.Value()) || !service.Reconcile(character.Value()).matches) return 1;
    auto measure = [&](auto reconcile) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kRepeats; ++i) if (!reconcile()) return -1.0;
        return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / kRepeats;
    };
    for (int round = 0; round < 3; ++round) {
        const auto previous = measure([&] { return PreviousReconcile(service, character.Value()); });
        const auto current = measure([&] { return service.Reconcile(character.Value()).matches; });
        std::cout << "round=" << round + 1 << " previous_us=" << previous << " current_us=" << current << '\n';
        if (previous < 0 || current < 0) return 1;
    }
}
