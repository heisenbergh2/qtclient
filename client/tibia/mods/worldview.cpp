#include "mods/worldview.h"

#include <algorithm>

namespace tibia::mods {
namespace {

WorldLightingDriver* g_lighting = nullptr;
std::vector<WorldOverlayFactory*> g_overlays;
WorldFrameHooks g_frameHooks;

} // namespace

void setWorldLightingDriver(WorldLightingDriver* driver)
{
    g_lighting = driver;
}

WorldLightingDriver* worldLightingDriver()
{
    return g_lighting;
}

void addWorldOverlayFactory(WorldOverlayFactory* factory)
{
    if (!factory)
        return;
    g_overlays.push_back(factory);
    std::stable_sort(g_overlays.begin(), g_overlays.end(),
                     [](WorldOverlayFactory* a, WorldOverlayFactory* b) {
                         return a->order() < b->order();
                     });
}

void clearWorldOverlayFactories()
{
    g_overlays.clear();
}

void abandonWorldOverlayNodes()
{
    for (WorldOverlayFactory* factory : g_overlays) {
        if (factory)
            factory->abandonNode();
    }
}

const std::vector<WorldOverlayFactory*>& worldOverlayFactories()
{
    return g_overlays;
}

void setWorldFrameHooks(WorldFrameHooks hooks)
{
    g_frameHooks = hooks;
}

const WorldFrameHooks& worldFrameHooks()
{
    return g_frameHooks;
}

} // namespace tibia::mods
