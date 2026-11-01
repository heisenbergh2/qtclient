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

#pragma once

#include "thing.h"

 // @bindclass
class Effect final : public Thing
{
public:
    void draw(const Point& /*dest*/, bool drawThings = true, LightSink* = nullptr) override;
    void setId(uint32_t id) override;
    void setPosition(const Position& position, uint8_t stackPos = 0) override;
    void setSource(Otc::MagicEffectSources source) { m_source = source; }
    Otc::MagicEffectSources getSource() const { return m_source; }
    void setPermanent(bool permanent) { m_permanent = permanent; }
    void setUnderCreatures(bool underCreatures) { m_underCreatures = underCreatures; }
    // The server effect id it was made from when that is an effect variant (ThingTypeManager::
    // registerEffectVariant), else 0: renderers without the shader recolour it by this id.
    void setVariantId(uint16_t variantId) { m_variantId = variantId; }
    uint16_t getVariantId() const { return m_variantId; }

    bool isEffect() const override { return true; }
    bool isPermanent() const { return m_permanent; }
    bool isUnderCreatures() const { return m_underCreatures; }
    bool waitFor(const EffectPtr&);
    // The frame draw() shows now; false while the effect is not drawn (yet).
    bool getDrawFrame(int& animationPhase, int& xPattern, int& yPattern);

    EffectPtr asEffect() { return static_self_cast<Effect>(); }

protected:
    void onAppear() override;
    ThingType* getThingType() const override;

private:
    Timer m_animationTimer;

    uint16_t m_duration{ 0 };
    uint16_t m_timeToStartDrawing{ 0 };
    uint16_t m_variantId{ 0 };
    Otc::MagicEffectSources m_source{ Otc::ME_SOURCE_DEFAULT };
    bool m_permanent{ false };
    bool m_underCreatures{ false };
};
