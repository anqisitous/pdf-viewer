#pragma once

#include <vector>

struct Leaf {
    int index = 0;
    float turn = 0.0f;          // 0 = right stack, 1 = left stack
    float from = 0.0f;
    float to = 0.0f;
    float delay = 0.0f;
    float duration = 0.55f;
    float elapsed = 0.0f;
    bool animating = false;
    float bowScale = 1.0f;

    bool grabbed = false;
    bool grabRight = true;
    int grabOrder = 0;
    int grabCount = 1;
    float grabLift = 0.0f;
    float grabLiftTarget = 0.0f;
};

class Book {
public:
    explicit Book(int pageCount);

    void update(float dt);

    // Page turning.  The logical boundary moves immediately and the leaves
    // animate; this lets fast repeated presses overlap without losing input.
    void nextPage();
    void prevPage();
    void flickRight();   // j: one page from the right, independent of the grip
    void flickLeft();    // d: symmetric from the left

    // Hands.
    void toggleRightGrab();  // k
    void toggleLeftGrab();   // s
    void decreaseRightGrip(); // l
    void decreaseLeftGrip();  // a

    int pageCount() const { return pageCount_; }
    int current() const { return current_; }
    int rightGrip() const { return rightGrip_; }
    int leftGrip() const { return leftGrip_; }
    int rightHeldCount() const { return rightHeldCount_; }
    int leftHeldCount() const { return leftHeldCount_; }
    bool rightHeld() const { return rightHeld_; }
    bool leftHeld() const { return leftHeld_; }
    bool anyAnimation() const;

    const std::vector<Leaf> &leaves() const { return leaves_; }

private:
    void flipForward(int count, float duration, float bowScale);
    void flipBackward(int count, float duration, float bowScale);
    void startTurn(int index, float to, float delay, float duration, float bowScale);
    void releaseRightGrab();
    void releaseLeftGrab();

    int pageCount_ = 0;
    int current_ = 0;
    int maxGrip_ = 8;
    int rightGrip_ = 3;
    int leftGrip_ = 3;
    int rightHeldCount_ = 0;
    int leftHeldCount_ = 0;
    bool rightHeld_ = false;
    bool leftHeld_ = false;
    std::vector<Leaf> leaves_;
};
