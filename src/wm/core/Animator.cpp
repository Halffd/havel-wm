#include <wm/Animator.hpp>
#include <algorithm>

namespace havel {

Animator::Animator() = default;
Animator::~Animator() = default;

Animator::AnimationPtr Animator::create() {
    auto anim = std::make_shared<Animation>();
    m_animations.push_back(anim);
    return anim;
}

Animator::AnimationPtr Animator::create(const AnimationConfig& config) {
    auto anim = std::make_shared<Animation>();
    anim->setDuration(config.durationMs);
    anim->setEasing(config.easing);
    m_animations.push_back(anim);
    return anim;
}

Animator::AnimationPtr Animator::fade(float from, float to, 
                                       std::function<void(float)> setter) {
    auto anim = create(AnimationPresets::fade());
    anim->setOnUpdate([=, from = from, to = to, setter = setter](float t) {
        float value = from + (to - from) * t;
        setter(value);
    });
    anim->start();
    return anim;
}

Animator::AnimationPtr Animator::slide(int fromX, int fromY, int toX, int toY,
                                        std::function<void(int, int)> setter) {
    auto anim = create(AnimationPresets::slide());
    anim->setOnUpdate([=, setter = setter](float t) {
        int x = fromX + static_cast<int>((toX - fromX) * t);
        int y = fromY + static_cast<int>((toY - fromY) * t);
        setter(x, y);
    });
    anim->start();
    return anim;
}

Animator::AnimationPtr Animator::scale(float from, float to,
                                        std::function<void(float)> setter) {
    auto anim = create(AnimationPresets::scale());
    anim->setOnUpdate([=, from = from, to = to, setter = setter](float t) {
        float value = from + (to - from) * t;
        setter(value);
    });
    anim->start();
    return anim;
}

Animator::AnimationPtr Animator::move(int fromX, int fromY, int toX, int toY,
                                       std::function<void(int, int)> setter) {
    auto anim = create(AnimationPresets::move());
    anim->setOnUpdate([=, setter = setter](float t) {
        int x = fromX + static_cast<int>((toX - fromX) * t);
        int y = fromY + static_cast<int>((toY - fromY) * t);
        setter(x, y);
    });
    anim->start();
    return anim;
}

Animator::AnimationPtr Animator::resize(int fromW, int fromH, int toW, int toH,
                                         std::function<void(int, int)> setter) {
    auto anim = create(AnimationPresets::resize());
    anim->setOnUpdate([=, setter = setter](float t) {
        int w = fromW + static_cast<int>((toW - fromW) * t);
        int h = fromH + static_cast<int>((toH - fromH) * t);
        setter(w, h);
    });
    anim->start();
    return anim;
}

void Animator::update() {
    for (auto& anim : m_animations) {
        if (anim->isRunning()) {
            anim->update();
        }
    }
}

void Animator::cleanup() {
    m_animations.erase(
        std::remove_if(m_animations.begin(), m_animations.end(),
            [](const AnimationPtr& anim) {
                return anim->state() == AnimationState::Completed ||
                       anim->state() == AnimationState::Cancelled;
            }),
        m_animations.end()
    );
}

void Animator::cancelAll() {
    for (auto& anim : m_animations) {
        anim->cancel();
    }
    cleanup();
}

void Animator::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled) {
        cancelAll();
    }
}

} // namespace havel
