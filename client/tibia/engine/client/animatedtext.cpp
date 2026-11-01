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

#include "animatedtext.h"
#include "tibia/runtime/host.h"

#include "tibia/world/game.h"
#include "gameconfig.h"
#include "tibia/world/map.h"
#include "framework/core/eventdispatcher.h"
#include "framework/core/graphicalapplication.h"

AnimatedText::AnimatedText()
{
    m_cachedText.setAlign(Fw::AlignLeft);
}

void AnimatedText::drawText(const Point& dest, const Rect& visibleRect)
{
    (void)dest; (void)visibleRect;
}


void AnimatedText::onAppear()
{
    m_animationTimer.restart();

    uint16_t textDuration = g_gameConfig.getAnimatedTextDuration();
    if (tibia::host::app().mustOptimize())
        textDuration /= 2;

    // schedule removal
    tibia::schedule::after([self = asAnimatedText()] { g_map.removeAnimatedText(self); }, textDuration);
}

bool AnimatedText::merge(const AnimatedTextPtr& other)
{
    if (other->getColor() != m_color)
        return false;

    if (other->getCachedText().getFont() != m_cachedText.getFont())
        return false;

    if (m_animationTimer.ticksElapsed() > g_gameConfig.getAnimatedTextDuration() / 2.5)
        return false;

    try {
        const int number = stdext::safe_cast<int>(m_cachedText.getText());
        const int otherNumber = stdext::safe_cast<int>(other->getCachedText().getText());
        m_cachedText.setText(fmt::format("{}", number + otherNumber));
        return true;
    } catch (...) {
        return false;
    }
}