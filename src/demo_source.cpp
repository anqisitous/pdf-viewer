#include "page_source.hpp"

#include "bitmap_font.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

struct Rgba {
    float r, g, b, a;
};

struct Canvas {
    PageImage &img;

    void put(int x, int y, Rgba c) {
        if (x < 0 || y < 0 || x >= img.width || y >= img.height) return;
        uint8_t *p = &img.rgba[(static_cast<size_t>(y) * img.width + x) * 4];
        const float a = std::clamp(c.a, 0.0f, 1.0f);
        p[0] = static_cast<uint8_t>(std::clamp(p[0] * (1.0f - a) + c.r * 255.0f * a, 0.0f, 255.0f));
        p[1] = static_cast<uint8_t>(std::clamp(p[1] * (1.0f - a) + c.g * 255.0f * a, 0.0f, 255.0f));
        p[2] = static_cast<uint8_t>(std::clamp(p[2] * (1.0f - a) + c.b * 255.0f * a, 0.0f, 255.0f));
        p[3] = 255;
    }

    void fillRect(int x, int y, int w, int h, Rgba c) {
        const int x0 = std::max(0, x), y0 = std::max(0, y);
        const int x1 = std::min(img.width, x + w), y1 = std::min(img.height, y + h);
        for (int yy = y0; yy < y1; ++yy)
            for (int xx = x0; xx < x1; ++xx) put(xx, yy, c);
    }

    void rectOutline(int x, int y, int w, int h, int t, Rgba c) {
        fillRect(x, y, w, t, c);
        fillRect(x, y + h - t, w, t, c);
        fillRect(x, y, t, h, c);
        fillRect(x + w - t, y, t, h, c);
    }

    void text(int x, int y, const char *s, int px, Rgba c, int tracking = 1) {
        int cx = x;
        for (; *s; ++s) {
            if (*s == ' ') {
                cx += (5 + tracking) * px;
                continue;
            }
            const uint8_t *g = glyph5x7(*s);
            for (int col = 0; col < 5; ++col) {
                for (int row = 0; row < 7; ++row) {
                    if ((g[col] >> row) & 1) fillRect(cx + col * px, y + row * px, px, px, c);
                }
            }
            cx += (5 + tracking) * px;
        }
    }
};

float hash01(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return static_cast<float>(x & 0xFFFFFF) / 16777215.0f;
}

class DemoSource final : public PageSource {
public:
    int pageCount() const override { return 24; }
    float aspect(int) const override { return 0.707f; }
    std::string title() const override { return "DEMO BOOK / 24 PAGES"; }

    bool render(int page, int maxDim, PageImage &out) override {
        const int h = std::max(64, maxDim);
        const int w = std::max(48, static_cast<int>(std::lround(h * aspect(page))));
        out.width = w;
        out.height = h;
        out.rgba.assign(static_cast<size_t>(w) * h * 4, 0);

        const uint32_t seed = static_cast<uint32_t>(page * 2654435761u + 17u);
        const float hue = std::fmod(0.08f + page * 0.037f, 1.0f);
        const Rgba accent = hsv(hue, 0.55f, 0.72f);
        const Rgba accentDark = hsv(hue, 0.62f, 0.42f);
        const Rgba paper = {0.965f, 0.945f, 0.885f, 1.0f};
        const Rgba ink = {0.16f, 0.17f, 0.18f, 1.0f};
        const Rgba muted = {0.42f, 0.43f, 0.43f, 1.0f};

        Canvas c{out};
        c.fillRect(0, 0, w, h, paper);

        // Paper grain.
        for (int y = 0; y < h; y += 2) {
            for (int x = 0; x < w; x += 2) {
                const float n = hash01(seed + static_cast<uint32_t>(y * 131 + x * 17));
                c.put(x, y, {0.0f, 0.0f, 0.0f, (n - 0.5f) * 0.018f});
            }
        }

        // Binding shadow on the spine (left) edge.
        const int spineW = std::max(10, w / 12);
        for (int x = 0; x < spineW; ++x) {
            const float t = 1.0f - static_cast<float>(x) / spineW;
            c.fillRect(x, 0, 1, h, {0.10f, 0.08f, 0.06f, 0.20f * t * t});
        }

        const int margin = std::max(18, w / 12);
        const int headerH = std::max(46, h / 11);
        c.fillRect(margin, margin, w - margin * 2, headerH, accent);
        c.text(margin + 16, margin + 12, "PDF VIEWER", std::max(3, w / 150), {0.99f, 0.98f, 0.92f, 1.0f});
        c.text(margin + 16, margin + 12 + std::max(3, w / 150) * 9, "PHYSICAL BOOK / DEMO",
               std::max(2, w / 260), {0.99f, 0.98f, 0.92f, 0.78f});

        const int numY = margin + headerH + std::max(24, h / 24);
        char num[16];
        std::snprintf(num, sizeof(num), "%02d", page + 1);
        c.text(margin, numY, num, std::max(6, w / 55), accentDark);
        c.text(margin + std::max(6, w / 55) * 7, numY + std::max(6, w / 55) * 3,
               "A SHEET IN THE STACK", std::max(2, w / 240), muted);

        const int lineY = numY + std::max(6, w / 55) * 8 + std::max(20, h / 28);
        const char *titleText = page % 3 == 0 ? "THE BOOK REMEMBERS" : "ON TURNING PAGES";
        const int titleChars = static_cast<int>(std::strlen(titleText));
        const int titlePx = std::max(2, std::min(w / 95,
            (w - margin * 2) / std::max(1, titleChars * 6)));
        c.text(margin, lineY, titleText, titlePx, ink);

        int y = lineY + titlePx * 11;
        const int lineH = std::max(6, h / 80);
        const int gap = std::max(4, lineH / 2);
        for (int i = 0; i < 9; ++i) {
            const int width = static_cast<int>((w - margin * 2) * (0.62f + 0.33f * hash01(seed + i * 37)));
            c.fillRect(margin, y, width, lineH, {muted.r, muted.g, muted.b, 0.55f});
            y += lineH + gap;
        }

        // A small diagram block, different on every third page.
        const int figY = y + std::max(14, h / 40);
        const int figH = std::max(70, h / 8);
        c.fillRect(margin, figY, w - margin * 2, figH, {accent.r, accent.g, accent.b, 0.16f});
        c.rectOutline(margin, figY, w - margin * 2, figH, std::max(2, w / 380), accent);
        for (int i = 0; i < 7; ++i) {
            const float t = static_cast<float>(i) / 6.0f;
            const int bx = margin + std::max(12, w / 45) + static_cast<int>((w - margin * 2 - std::max(24, w / 22)) * t);
            const int bh = static_cast<int>(figH * (0.25f + 0.6f * hash01(seed + 900 + i)));
            c.fillRect(bx, figY + figH - bh - std::max(8, h / 90), std::max(4, w / 90), bh,
                       {accentDark.r, accentDark.g, accentDark.b, 0.65f});
        }

        // Footer.
        c.fillRect(margin, h - margin - std::max(2, h / 380), w - margin * 2, std::max(2, h / 380),
                   {0.18f, 0.17f, 0.15f, 0.35f});
        char foot[96];
        std::snprintf(foot, sizeof(foot), "M NEXT   C PREV   S/K GRAB   L/A GRIP");
        c.text(margin, h - margin - std::max(2, h / 380) - std::max(2, w / 260) * 8,
               foot, std::max(2, w / 300), muted);
        return true;
    }

private:
    static Rgba hsv(float h, float s, float v) {
        const float x = h * 6.0f;
        const int i = static_cast<int>(std::floor(x)) % 6;
        const float f = x - std::floor(x);
        const float p = v * (1.0f - s);
        const float q = v * (1.0f - s * f);
        const float t = v * (1.0f - s * (1.0f - f));
        switch (i) {
            case 0: return {v, t, p, 1.0f};
            case 1: return {q, v, p, 1.0f};
            case 2: return {p, v, t, 1.0f};
            case 3: return {p, q, v, 1.0f};
            case 4: return {t, p, v, 1.0f};
            default: return {v, p, q, 1.0f};
        }
    }
};

} // namespace

std::unique_ptr<PageSource> createDemoSource() {
    return std::make_unique<DemoSource>();
}
