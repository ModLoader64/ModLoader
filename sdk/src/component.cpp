#include "internal.h"

#include <modloader/detail/component.h>

#include <cstdlib>

namespace ModLoader::Runtime {

namespace {

const Component** sComponents;
u32 sCount;
u32 sCapacity;

} // namespace

void Register_Component(const Component* component) {
    if (sCount == sCapacity) {
        if (sCapacity > UINT32_MAX / 2) {
            abort();
        }
        u32 capacity = sCapacity != 0 ? sCapacity * 2 : 8;
        auto components = static_cast<const Component**>(realloc(sComponents, static_cast<usize>(capacity) * sizeof(*sComponents)));
        if (components == nullptr) {
            abort();
        }
        sComponents = components;
        sCapacity = capacity;
    }
    sComponents[sCount++] = component;
}

std::span<const Component* const> Components() {
    return {sComponents, sCount};
}

void Prepare_Components() {
#define MODLOADER_LIFECYCLE(field, arguments, pre, normal, post, event) \
    gComponentEvents.field.Prepare([](const Component* component) { return &component->field; });
#include <modloader/detail/lifecycle.def>
#undef MODLOADER_LIFECYCLE
    for (const Component* component : Components()) {
        if (component->prepare != nullptr) {
            component->prepare();
        }
    }
}

} // namespace ModLoader::Runtime

