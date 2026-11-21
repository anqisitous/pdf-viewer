#include "book.hpp"

#include "math3d.hpp"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kStagger = 0.045f;
constexpr float kTurnDuration = 0.56f;
constexpr float kFlickDuration = 0.40f;
} // namespace

Book::Book(int pageCount) : pageCount_(std::max(0, pageCount)), leaves_(pageCount_) {
    for (int i = 0; i < pageCount_; ++i) {
        leaves_[i].index = i;
        leaves_[i].turn = 0.0f;
    }
}

void Book::startTurn(int index, float to, float delay, float duration, float bowScale) {
    if (index < 0 || index >= pageCount_) return;
    Leaf &l = leaves_[index];
    l.from = l.turn;
    l.to = to;
    l.delay = delay;
    l.duration = std::max(0.05f, duration);
    l.elapsed = 0.0f;
    l.animating = true;
    l.bowScale = bowScale;
}

void Book::flipForward(int count, float duration, float bowScale) {
    if (rightHeld_ || leftHeld_) return;
    const int n = std::min(count, pageCount_ - current_);
    if (n <= 0) return;
    for (int k = 0; k < n; ++k) {
        startTurn(current_ + k, 1.0f, k * 0.035f, duration, bowScale);
    }
    current_ += n;
}

void Book::flipBackward(int count, float duration, float bowScale) {
    if (rightHeld_ || leftHeld_) return;
    const int n = std::min(count, current_);
    if (n <= 0) return;
    for (int k = 0; k < n; ++k) {
        startTurn(current_ - 1 - k, 0.0f, k * 0.035f, duration, bowScale);
    }
    current_ -= n;
}

void Book::nextPage() { flipForward(1, kTurnDuration, 1.0f); }
void Book::prevPage() { flipBackward(1, kTurnDuration, 1.0f); }
void Book::flickRight() { flipForward(1, kFlickDuration, 1.65f); }
void Book::flickLeft() { flipBackward(1, kFlickDuration, 1.65f); }

void Book::toggleRightGrab() {
    if (rightHeld_) {
        releaseRightGrab();
        return;
    }
    // One physical hand at a time keeps the block order unambiguous.
    if (leftHeld_ || current_ >= pageCount_) return;

    int g = std::min(rightGrip_, pageCount_ - current_);
    int atRest = 0;
    for (int k = 0; k < g; ++k) {
        const Leaf &l = leaves_[current_ + k];
        if (l.grabbed || l.animating || l.turn > 0.02f) break;
        ++atRest;
    }
    if (atRest <= 0) return;
    g = atRest;
    rightHeldCount_ = g;
    for (int k = 0; k < g; ++k) {
        Leaf &l = leaves_[current_ + k];
        l.grabbed = true;
        l.grabRight = true;
        l.grabOrder = k;
        l.grabCount = g;
        l.grabLiftTarget = 1.0f;
    }
    rightHeld_ = true;
}

void Book::releaseRightGrab() {
    if (!rightHeld_) return;
    const int start = current_;
    const int g = rightHeldCount_;
    for (int k = 0; k < g; ++k) {
        const int idx = start + k;
        if (idx < 0 || idx >= pageCount_) continue;
        Leaf &l = leaves_[idx];
        if (!l.grabbed || !l.grabRight) continue;
        l.grabbed = false;
        l.grabLiftTarget = 0.0f;
        // Top page leads; the last page of the chunk settles on the left.
        startTurn(idx, 1.0f, k * kStagger, kTurnDuration + k * 0.025f, 1.15f);
    }
    current_ = std::min(pageCount_, current_ + g);
    rightHeld_ = false;
    rightHeldCount_ = 0;
}

void Book::toggleLeftGrab() {
    if (leftHeld_) {
        releaseLeftGrab();
        return;
    }
    if (rightHeld_ || current_ <= 0) return;

    int g = std::min(leftGrip_, current_);
    int atRest = 0;
    for (int k = 0; k < g; ++k) {
        const Leaf &l = leaves_[current_ - 1 - k];
        if (l.grabbed || l.animating || l.turn < 0.98f) break;
        ++atRest;
    }
    if (atRest <= 0) return;
    g = atRest;
    leftHeldCount_ = g;
    for (int k = 0; k < g; ++k) {
        Leaf &l = leaves_[current_ - 1 - k];
        l.grabbed = true;
        l.grabRight = false;
        l.grabOrder = k;
        l.grabCount = g;
        l.grabLiftTarget = 1.0f;
    }
    leftHeld_ = true;
}

void Book::releaseLeftGrab() {
    if (!leftHeld_) return;
    const int g = leftHeldCount_;
    for (int k = 0; k < g; ++k) {
        const int idx = current_ - 1 - k;
        if (idx < 0 || idx >= pageCount_) continue;
        Leaf &l = leaves_[idx];
        if (!l.grabbed || l.grabRight) continue;
        l.grabbed = false;
        l.grabLiftTarget = 0.0f;
        // Top of the left stack leads; the deepest page lands on top on the right.
        startTurn(idx, 0.0f, k * kStagger, kTurnDuration + k * 0.025f, 1.15f);
    }
    current_ = std::max(0, current_ - g);
    leftHeld_ = false;
    leftHeldCount_ = 0;
}

void Book::decreaseRightGrip() {
    if (rightGrip_ <= 1) rightGrip_ = maxGrip_;
    else --rightGrip_;
}

void Book::decreaseLeftGrip() {
    if (leftGrip_ <= 1) leftGrip_ = maxGrip_;
    else --leftGrip_;
}

bool Book::anyAnimation() const {
    for (const Leaf &l : leaves_) {
        if (l.animating || l.grabbed) return true;
    }
    return false;
}

void Book::update(float dt) {
    dt = std::clamp(dt, 0.0f, 0.1f);
    const float liftRate = 1.0f - std::exp(-15.0f * dt);
    for (Leaf &l : leaves_) {
        if (l.animating) {
            l.elapsed += dt;
            const float local = l.elapsed - l.delay;
            if (local <= 0.0f) {
                l.turn = l.from;
            } else {
                const float u = local / l.duration;
                if (u >= 1.0f) {
                    l.turn = l.to;
                    l.animating = false;
                    l.bowScale = 1.0f;
                } else {
                    const float e = m3::smoothstep01(u);
                    l.turn = l.from + (l.to - l.from) * e;
                }
            }
        }
        l.grabLift += (l.grabLiftTarget - l.grabLift) * liftRate;
        if (std::fabs(l.grabLift - l.grabLiftTarget) < 0.001f) {
            l.grabLift = l.grabLiftTarget;
        }
    }
}
