/*
 * Copyright (c) 2024-2026 qtclient contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "attachableobject.h"

#include "tibia/world/attachedeffect.h"
#include "client.h"
#include "tibia/world/map.h"
#include "framework/core/eventdispatcher.h"
#include "framework/ui/uiwidget.h"

AttachableObject::~AttachableObject()
{
    clearAttachedEffects(true);
    clearAttachedParticlesEffect();
    clearAttachedWidgets(false);
}

void AttachableObject::attachEffect(const AttachedEffectPtr& obj)
{
    if (!obj)
        return;

    onStartAttachEffect(obj);

    if (obj->isHidedOwner())
        ++m_ownerHidden;

    if (obj->getDuration() > 0) {
        tibia::schedule::after([self = std::static_pointer_cast<AttachableObject>(shared_from_this()), effect = obj] {
            self->detachEffect(effect);
        }, obj->getDuration());
    }

    getData()->attachedEffects.emplace_back(obj);
    tibia::schedule::post([effect = obj, self = std::static_pointer_cast<AttachableObject>(shared_from_this())] {
        self->onDispatcherAttachEffect(effect);
        effect->callLuaField("onAttach", self->attachedObjectToLuaObject());
    });
}

bool AttachableObject::detachEffect(const AttachedEffectPtr& obj) {
    if (!hasAttachedEffects()) return false;
    const auto it = std::find(m_data->attachedEffects.begin(), m_data->attachedEffects.end(), obj);

    if (it == m_data->attachedEffects.end())
        return false;

    onDetachEffect(*it);
    m_data->attachedEffects.erase(it);

    return true;
}

bool AttachableObject::detachEffectById(uint16_t id)
{
    if (!hasAttachedEffects()) return false;
    const auto it = std::find_if(m_data->attachedEffects.begin(), m_data->attachedEffects.end(),
                                 [id](const AttachedEffectPtr& obj) { return obj->getId() == id; });

    if (it == m_data->attachedEffects.end())
        return false;

    onDetachEffect(*it);
    m_data->attachedEffects.erase(it);

    return true;
}

void AttachableObject::onDetachEffect(const AttachedEffectPtr& effect, const bool callEvent)
{
    if (effect->isHidedOwner())
        --m_ownerHidden;

    onStartDetachEffect(effect);

    if (callEvent)
        effect->callLuaField("onDetach", attachedObjectToLuaObject());
}

void AttachableObject::clearAttachedEffects(const bool ignoreLuaEvent)
{
    if (!hasAttachedEffects()) return;
    for (const auto& e : m_data->attachedEffects)
        onDetachEffect(e, !ignoreLuaEvent);
    m_data->attachedEffects.clear();
}

void AttachableObject::clearTemporaryAttachedEffects()
{
    if (!hasAttachedEffects()) return;
    std::erase_if(m_data->attachedEffects,
                  [this](const AttachedEffectPtr& obj) {
        if (!obj->isPermanent()) {
            onDetachEffect(obj);
            return true;
        }
        return false;
    });
}

void AttachableObject::clearPermanentAttachedEffects()
{
    if (!hasAttachedEffects()) return;
    std::erase_if(m_data->attachedEffects,
                  [this](const AttachedEffectPtr& obj) {
        if (obj->isPermanent()) {
            onDetachEffect(obj);
            return true;
        }
        return false;
    });
}

AttachedEffectPtr AttachableObject::getAttachedEffectById(uint16_t id)
{
    if (!hasAttachedEffects()) return nullptr;
    const auto it = std::find_if(m_data->attachedEffects.begin(), m_data->attachedEffects.end(),
                                 [id](const AttachedEffectPtr& obj) { return obj->getId() == id; });

    if (it == m_data->attachedEffects.end())
        return nullptr;

    return *it;
}

void AttachableObject::drawAttachedEffect(const Point& originalDest, const Point& dest, LightSink* lightView, const bool isOnTop)
{
    if (!hasAttachedEffects()) return;
    for (const auto& effect : m_data->attachedEffects) {
        effect->draw(effect->isFollowingOwner() ? dest : originalDest, isOnTop, lightView);
        if (effect->getLoop() == 0) {
            tibia::schedule::post([self = std::static_pointer_cast<AttachableObject>(shared_from_this()), effect] {
                self->detachEffect(effect);
            });
        }
    }
}

void AttachableObject::drawAttachedLightEffect(const Point& dest, LightSink* lightView) {
    if (!hasAttachedEffects()) return;
    for (const auto& effect : m_data->attachedEffects)
        effect->drawLight(dest, lightView);
}

void AttachableObject::attachParticleEffect(const std::string& name)
{
    if (!name.empty())
        getData()->attachedParticles.emplace_back(name);
}

void AttachableObject::clearAttachedParticlesEffect()
{
    if (m_data)
        m_data->attachedParticles.clear();
}

bool AttachableObject::detachParticleEffectByName(const std::string& name)
{
    if (!hasAttachedParticles()) return false;

    auto findFunc = [name](const std::string& obj) { return obj == name; };
    const auto it = std::find_if(m_data->attachedParticles.begin(), m_data->attachedParticles.end(), findFunc);

    if (it == m_data->attachedParticles.end())
        return false;

    m_data->attachedParticles.erase(it);

    return true;
}

void AttachableObject::drawAttachedParticlesEffect(const Point& dest)
{
    (void)dest;
}


void AttachableObject::updateAndAttachParticlesEffects(std::vector<std::string>& newElements)
{
    if (!hasAttachedParticles()) return;

    std::vector<std::string> toRemove;
    toRemove.reserve(m_data->attachedParticles.size());

    for (const auto& effect : m_data->attachedParticles) {
        auto findPos = std::ranges::find(newElements, effect);
        if (findPos == newElements.end())
            toRemove.emplace_back(effect);
        else
            newElements.erase(findPos);
    }

    for (const auto& name : toRemove)
        detachParticleEffectByName(name);

    for (const auto& name : newElements)
        attachParticleEffect(name);
}

// Without OTUI nothing can create widgets, so none are ever attached.
bool AttachableObject::isWidgetAttached(const UIWidgetPtr&) { return false; }
void AttachableObject::attachWidget(const UIWidgetPtr&) {}
bool AttachableObject::detachWidgetById(const std::string&) { return false; }
bool AttachableObject::detachWidget(const UIWidgetPtr) { return false; }
void AttachableObject::clearAttachedWidgets(const bool) {}
UIWidgetPtr AttachableObject::getAttachedWidgetById(const std::string&) { return nullptr; }
