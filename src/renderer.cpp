#include "renderer.hpp"

#include "bitmap_font.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr int kLogicalW = 1280;
constexpr int kLogicalH = 800;
constexpr int kPagePixelHeight = 1000;
constexpr size_t kMaxTextures = 32;
constexpr int kGlyphCellW = 6;
constexpr int kGlyphCellH = 8;
constexpr int kGlyphCols = 16;
constexpr int kGlyphRows = 8;
constexpr int kAtlasW = kGlyphCols * kGlyphCellW;
constexpr int kAtlasH = kGlyphRows * kGlyphCellH;

const m3::Vec3 kLightDir = m3::normalize({-0.38f, 1.0f, 0.42f});

struct Projected {
    float x = 0.0f;
    float y = 0.0f;
    float depth = 0.0f;
};

Projected projectPoint(const m3::Mat4 &view, const m3::Mat4 &viewProj, m3::Vec3 world) {
    const m3::Vec3 v = m3::transformPoint(view, world);
    const m3::Vec3 c = m3::transformPoint(viewProj, world);
    Projected p;
    p.x = (c.x * 0.5f + 0.5f) * kLogicalW;
    p.y = (1.0f - (c.y * 0.5f + 0.5f)) * kLogicalH;
    p.depth = v.z;
    return p;
}

float shadeFor(m3::Vec3 normal) {
    const float d = std::max(0.0f, m3::dot(m3::normalize(normal), kLightDir));
    return 0.52f + 0.48f * d;
}

SDL_FColor fcolor(Color c, float shade = 1.0f) {
    return {c.r * shade, c.g * shade, c.b * shade, c.a};
}

void addWorldQuad(std::vector<SDL_Vertex> &verts, std::vector<int> &indices,
                  const m3::Mat4 &view, const m3::Mat4 &viewProj,
                  m3::Vec3 p0, m3::Vec3 p1, m3::Vec3 p2, m3::Vec3 p3,
                  m3::Vec3 normal, Color color,
                  float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f) {
    const m3::Vec3 pts[4] = {p0, p1, p2, p3};
    const float uvs[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
    const SDL_FColor c = fcolor(color, shadeFor(normal));
    const int start = static_cast<int>(verts.size());
    for (int i = 0; i < 4; ++i) {
        const Projected p = projectPoint(view, viewProj, pts[i]);
        SDL_Vertex v;
        v.position = {p.x, p.y};
        v.color = c;
        v.tex_coord = {uvs[i][0], uvs[i][1]};
        verts.push_back(v);
    }
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; ++i) indices.push_back(start + order[i]);
}

int glyphIndex(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    const int count = static_cast<int>(sizeof(kGlyphs5x7) / sizeof(kGlyphs5x7[0]));
    for (int i = 0; i < count; ++i) {
        if (kGlyphs5x7[i].c == c) return i;
    }
    return 0;
}

float textWidth(const std::string &s, int px, int tracking = 1) {
    return static_cast<float>(s.size()) * (5 + tracking) * px;
}

} // namespace

bool Renderer::init(SDL_Window *window) {
    // Explicitly use SDL's GPU renderer (Metal on macOS), never the OpenGL
    // renderer.  The software renderer is the last-resort fallback.
    renderer_ = SDL_CreateRenderer(window, SDL_GPU_RENDERER);
    if (!renderer_) {
        renderer_ = SDL_CreateRenderer(window, "software");
    }
    if (!renderer_) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(renderer_, 1);
    SDL_SetRenderLogicalPresentation(renderer_, kLogicalW, kLogicalH,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);

    // Warm, slightly fibrous paper used for page backs and the page block.
    const int paperW = 256, paperH = 256;
    std::vector<unsigned char> paper(static_cast<size_t>(paperW) * paperH * 4);
    for (int y = 0; y < paperH; ++y) {
        for (int x = 0; x < paperW; ++x) {
            const uint32_t h = (static_cast<uint32_t>(x) * 374761393u) ^
                               (static_cast<uint32_t>(y) * 668265263u);
            const float n = static_cast<float>((h ^ (h >> 13)) & 0xFFFF) / 65535.0f;
            const float fiber = 0.012f * std::sin(static_cast<float>(y) * 0.55f + n * 9.0f);
            const float base = 0.938f + (n - 0.5f) * 0.012f + fiber;
            const size_t i = (static_cast<size_t>(y) * paperW + x) * 4;
            paper[i + 0] = static_cast<unsigned char>(std::clamp(base * 255.0f, 0.0f, 255.0f));
            paper[i + 1] = static_cast<unsigned char>(std::clamp((base - 0.010f) * 255.0f, 0.0f, 255.0f));
            paper[i + 2] = static_cast<unsigned char>(std::clamp((base - 0.035f) * 255.0f, 0.0f, 255.0f));
            paper[i + 3] = 255;
        }
    }
    paperTex_ = makeTexture(paperW, paperH, paper, SDL_BLENDMODE_NONE, SDL_SCALEMODE_LINEAR);

    // Fore-edge of the page stack: cream paper with horizontal leaf lines.
    const int edgeW = 64, edgeH = 256;
    std::vector<unsigned char> edge(static_cast<size_t>(edgeW) * edgeH * 4);
    for (int y = 0; y < edgeH; ++y) {
        for (int x = 0; x < edgeW; ++x) {
            const float base = 0.915f + 0.018f * std::sin(static_cast<float>(x) * 0.8f);
            const bool line = (y % 4 == 0) || (y % 9 == 0);
            const float v = line ? base - 0.055f : base;
            const size_t i = (static_cast<size_t>(y) * edgeW + x) * 4;
            edge[i + 0] = static_cast<unsigned char>(std::clamp(v * 255.0f, 0.0f, 255.0f));
            edge[i + 1] = static_cast<unsigned char>(std::clamp((v - 0.012f) * 255.0f, 0.0f, 255.0f));
            edge[i + 2] = static_cast<unsigned char>(std::clamp((v - 0.040f) * 255.0f, 0.0f, 255.0f));
            edge[i + 3] = 255;
        }
    }
    edgeTex_ = makeTexture(edgeW, edgeH, edge, SDL_BLENDMODE_NONE, SDL_SCALEMODE_LINEAR);

    // Soft radial shadow.
    const int shadowSize = 128;
    std::vector<unsigned char> shadow(static_cast<size_t>(shadowSize) * shadowSize * 4);
    for (int y = 0; y < shadowSize; ++y) {
        for (int x = 0; x < shadowSize; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f) / shadowSize * 2.0f - 1.0f;
            const float dy = (static_cast<float>(y) + 0.5f) / shadowSize * 2.0f - 1.0f;
            const float r = std::sqrt(dx * dx + dy * dy);
            const float a = std::clamp(1.0f - r, 0.0f, 1.0f);
            const size_t i = (static_cast<size_t>(y) * shadowSize + x) * 4;
            shadow[i + 0] = 255;
            shadow[i + 1] = 255;
            shadow[i + 2] = 255;
            shadow[i + 3] = static_cast<unsigned char>(a * a * 255.0f);
        }
    }
    shadowTex_ = makeTexture(shadowSize, shadowSize, shadow, SDL_BLENDMODE_BLEND,
                             SDL_SCALEMODE_LINEAR);

    // Font atlas from the built-in 5x7 glyphs.
    std::vector<unsigned char> atlas(static_cast<size_t>(kAtlasW) * kAtlasH * 4, 0);
    const int glyphCount = static_cast<int>(sizeof(kGlyphs5x7) / sizeof(kGlyphs5x7[0]));
    for (int g = 0; g < glyphCount; ++g) {
        const int cx = (g % kGlyphCols) * kGlyphCellW;
        const int cy = (g / kGlyphCols) * kGlyphCellH;
        for (int col = 0; col < 5; ++col) {
            for (int row = 0; row < 7; ++row) {
                if ((kGlyphs5x7[g].col[col] >> row) & 1) {
                    const size_t i = (static_cast<size_t>(cy + row) * kAtlasW + cx + col) * 4;
                    atlas[i + 0] = 255;
                    atlas[i + 1] = 255;
                    atlas[i + 2] = 255;
                    atlas[i + 3] = 255;
                }
            }
        }
    }
    fontTex_ = makeTexture(kAtlasW, kAtlasH, atlas, SDL_BLENDMODE_BLEND, SDL_SCALEMODE_NEAREST);

    return paperTex_ && edgeTex_ && shadowTex_ && fontTex_;
}

SDL_Texture *Renderer::makeTexture(int w, int h, const std::vector<unsigned char> &rgba,
                                   SDL_BlendMode blend, SDL_ScaleMode scale) {
    SDL_Texture *tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC, w, h);
    if (!tex) return nullptr;
    SDL_SetTextureBlendMode(tex, blend);
    SDL_SetTextureScaleMode(tex, scale);
    SDL_UpdateTexture(tex, nullptr, rgba.data(), w * 4);
    return tex;
}

void Renderer::shutdown() {
    for (auto &entry : textures_) {
        if (entry.second.tex) SDL_DestroyTexture(entry.second.tex);
    }
    textures_.clear();
    if (paperTex_) SDL_DestroyTexture(paperTex_);
    if (edgeTex_) SDL_DestroyTexture(edgeTex_);
    if (shadowTex_) SDL_DestroyTexture(shadowTex_);
    if (fontTex_) SDL_DestroyTexture(fontTex_);
    paperTex_ = edgeTex_ = shadowTex_ = fontTex_ = nullptr;
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
}

SDL_Texture *Renderer::ensureTexture(int page, PageSource &source) {
    auto it = textures_.find(page);
    if (it != textures_.end()) {
        it->second.lastUsed = frameCounter_;
        return it->second.tex;
    }
    PageImage img;
    if (!source.render(page, kPagePixelHeight, img) || img.width <= 0 || img.height <= 0) {
        return nullptr;
    }
    SDL_Texture *tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC, img.width, img.height);
    if (!tex) return nullptr;
    SDL_UpdateTexture(tex, nullptr, img.rgba.data(), img.width * 4);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
    textures_[page] = {tex, img.width, img.height, frameCounter_};
    evictTextures();
    return tex;
}

void Renderer::evictTextures() {
    while (textures_.size() > kMaxTextures) {
        auto victim = textures_.begin();
        for (auto it = textures_.begin(); it != textures_.end(); ++it) {
            if (it->second.lastUsed < victim->second.lastUsed) victim = it;
        }
        if (victim->second.tex) SDL_DestroyTexture(victim->second.tex);
        textures_.erase(victim);
    }
}

void Renderer::drawBackground() {
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    const SDL_FColor top = {0.055f, 0.065f, 0.085f, 1.0f};
    const SDL_FColor bottom = {0.016f, 0.020f, 0.028f, 1.0f};
    const float y0 = 0.0f, y1 = static_cast<float>(kLogicalH);
    SDL_Vertex a{{0.0f, y0}, top, {0.0f, 0.0f}};
    SDL_Vertex b{{static_cast<float>(kLogicalW), y0}, top, {1.0f, 0.0f}};
    SDL_Vertex c{{static_cast<float>(kLogicalW), y1}, bottom, {1.0f, 1.0f}};
    SDL_Vertex d{{0.0f, y1}, bottom, {0.0f, 1.0f}};
    v = {a, b, c, d};
    idx = {0, 1, 2, 0, 2, 3};
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_RenderGeometry(renderer_, nullptr, v.data(), static_cast<int>(v.size()),
                       idx.data(), static_cast<int>(idx.size()));
}

void Renderer::drawFloor(const m3::Mat4 &view, const m3::Mat4 &viewProj, float floorY) {
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    addWorldQuad(v, idx, view, viewProj,
                 {-6.0f, floorY, -6.0f}, {-6.0f, floorY, 2.0f},
                 {6.0f, floorY, 2.0f}, {6.0f, floorY, -6.0f},
                 {0.0f, 1.0f, 0.0f}, {0.105f, 0.110f, 0.125f, 1.0f});
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_RenderGeometry(renderer_, nullptr, v.data(), static_cast<int>(v.size()),
                       idx.data(), static_cast<int>(idx.size()));
}

void Renderer::drawShadow(const m3::Mat4 &view, const m3::Mat4 &viewProj, float floorY,
                          float halfW, float halfH) {
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    const float ex = halfW + 0.22f;
    const float ez = halfH + 0.20f;
    addWorldQuad(v, idx, view, viewProj,
                 {-ex, floorY + 0.0015f, -ez}, {-ex, floorY + 0.0015f, ez},
                 {ex, floorY + 0.0015f, ez}, {ex, floorY + 0.0015f, -ez},
                 {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.62f});
    SDL_RenderGeometry(renderer_, shadowTex_, v.data(), static_cast<int>(v.size()),
                       idx.data(), static_cast<int>(idx.size()));
}

void Renderer::drawCover(const m3::Mat4 &view, const m3::Mat4 &viewProj, float coverY,
                         float halfW, float halfH, float scale) {
    const float margin = 0.075f * scale + 0.010f;
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    addWorldQuad(v, idx, view, viewProj,
                 {-halfW - margin, coverY, -halfH - margin},
                 {-halfW - margin, coverY, halfH + margin},
                 {halfW + margin, coverY, halfH + margin},
                 {halfW + margin, coverY, -halfH - margin},
                 {0.0f, 1.0f, 0.0f}, {0.24f, 0.18f, 0.10f, 1.0f});
    const float inset = 0.011f * scale + 0.002f;
    addWorldQuad(v, idx, view, viewProj,
                 {-halfW - margin + inset, coverY + 0.0009f, -halfH - margin + inset},
                 {-halfW - margin + inset, coverY + 0.0009f, halfH + margin - inset},
                 {halfW + margin - inset, coverY + 0.0009f, halfH + margin - inset},
                 {halfW + margin - inset, coverY + 0.0009f, -halfH - margin + inset},
                 {0.0f, 1.0f, 0.0f}, {0.075f, 0.060f, 0.050f, 1.0f});
    addWorldQuad(v, idx, view, viewProj,
                 {-0.014f * scale, coverY + 0.0018f, -halfH - margin + inset},
                 {-0.014f * scale, coverY + 0.0018f, halfH + margin - inset},
                 {0.014f * scale, coverY + 0.0018f, halfH + margin - inset},
                 {0.014f * scale, coverY + 0.0018f, -halfH - margin + inset},
                 {0.0f, 1.0f, 0.0f}, {0.145f, 0.112f, 0.065f, 1.0f});
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_RenderGeometry(renderer_, nullptr, v.data(), static_cast<int>(v.size()),
                       idx.data(), static_cast<int>(idx.size()));
}

void Renderer::drawBlock(const m3::Mat4 &view, const m3::Mat4 &viewProj, float x0, float x1,
                         float z0, float z1, float yBottom, float yTop) {
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    // Top.
    addWorldQuad(v, idx, view, viewProj,
                 {x0, yTop, z0}, {x0, yTop, z1}, {x1, yTop, z1}, {x1, yTop, z0},
                 {0.0f, 1.0f, 0.0f}, {0.92f, 0.90f, 0.84f, 1.0f});
    // Front (+z).
    addWorldQuad(v, idx, view, viewProj,
                 {x0, yBottom, z1}, {x1, yBottom, z1}, {x1, yTop, z1}, {x0, yTop, z1},
                 {0.0f, 0.0f, 1.0f}, {0.92f, 0.90f, 0.84f, 1.0f});
    // Back (-z).
    addWorldQuad(v, idx, view, viewProj,
                 {x1, yBottom, z0}, {x0, yBottom, z0}, {x0, yTop, z0}, {x1, yTop, z0},
                 {0.0f, 0.0f, -1.0f}, {0.88f, 0.86f, 0.80f, 1.0f});
    // Outer side.
    if (x1 > 0.0f) {
        addWorldQuad(v, idx, view, viewProj,
                     {x1, yBottom, z1}, {x1, yBottom, z0}, {x1, yTop, z0}, {x1, yTop, z1},
                     {1.0f, 0.0f, 0.0f}, {0.92f, 0.90f, 0.84f, 1.0f});
    } else {
        addWorldQuad(v, idx, view, viewProj,
                     {x0, yBottom, z0}, {x0, yBottom, z1}, {x0, yTop, z1}, {x0, yTop, z0},
                     {-1.0f, 0.0f, 0.0f}, {0.92f, 0.90f, 0.84f, 1.0f});
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_RenderGeometry(renderer_, edgeTex_, v.data(), static_cast<int>(v.size()),
                       idx.data(), static_cast<int>(idx.size()));
}

void Renderer::drawPages(const Book &book, PageSource &source, const m3::Mat4 &view,
                         const m3::Mat4 &viewProj, float pageW, float pageH,
                         float bookScale) {
    const int n = book.pageCount();
    if (n <= 0) return;
    const int current = book.current();
    const auto &leaves = book.leaves();

    struct DrawItem {
        int index = 0;
        bool dynamic = false;
        float depth = 0.0f;
    };

    const m3::Mat4 bookBase = m3::mul(m3::scaling(bookScale),
                                      m3::rotationX(m3::kPi * 0.5f));

    auto makeModel = [&](const Leaf &l, float &bowOut) {
        const float turn = m3::clampf(l.turn, 0.0f, 1.0f);
        const int order = std::min(l.grabOrder, 6);
        const float grabLift = l.grabLift * (0.075f - 0.013f * order);
        const float grabTilt = l.grabLift * (l.grabRight ? -1.0f : 1.0f) *
                               (0.050f + 0.006f * static_cast<float>(order));
        const float angle = -m3::kPi * turn + grabTilt;
        const float bow = 0.035f * std::sin(m3::kPi * turn) * l.bowScale;
        bowOut = bow;
        return m3::mul(bookBase,
                       m3::mul(m3::translation(0.0f, 0.0f, grabLift),
                               m3::rotationY(angle)));
    };

    std::vector<DrawItem> items;
    std::vector<char> seen(static_cast<size_t>(n), 0);
    auto add = [&](int index) {
        if (index < 0 || index >= n || seen[index]) return;
        seen[index] = 1;
        const Leaf &l = leaves[index];
        float bow = 0.0f;
        const m3::Mat4 model = makeModel(l, bow);
        const m3::Vec3 center = m3::transformPoint(
            model, {pageW * 0.5f, 0.0f, 0.0f});
        const m3::Vec3 cv = m3::transformPoint(view, center);
        items.push_back({index, l.animating || l.grabbed, cv.z});
    };

    // Keep the next few right-hand pages warm so a quick series of turns
    // does not stall on PDF rasterization.
    for (int i = current; i < std::min(n, current + 3); ++i) {
        ensureTexture(i, source);
    }

    add(current - 1);
    add(current);
    if (book.rightHeld()) add(current + book.rightHeldCount());
    if (book.leftHeld()) add(current - book.leftHeldCount() - 1);
    for (int i = 0; i < n; ++i) {
        if (leaves[i].animating || leaves[i].grabbed) add(i);
    }

    std::sort(items.begin(), items.end(), [](const DrawItem &a, const DrawItem &b) {
        if (a.dynamic != b.dynamic) return !a.dynamic;
        return a.depth < b.depth;
    });

    const int NX = 24;
    const int NY = 4;
    for (const DrawItem &item : items) {
        const Leaf &l = leaves[item.index];
        const float turn = m3::clampf(l.turn, 0.0f, 1.0f);
        const bool back = turn >= 0.5f;
        SDL_Texture *pageTex = back ? nullptr : ensureTexture(item.index, source);
        SDL_Texture *tex = back ? paperTex_ : (pageTex ? pageTex : paperTex_);
        float bow = 0.0f;
        const m3::Mat4 model = makeModel(l, bow);

        std::vector<SDL_Vertex> verts;
        std::vector<int> indices;
        verts.reserve(static_cast<size_t>(NX + 1) * (NY + 1));
        indices.reserve(static_cast<size_t>(NX) * NY * 6);

        for (int j = 0; j <= NY; ++j) {
            const float v = static_cast<float>(j) / NY;
            for (int i = 0; i <= NX; ++i) {
                const float u = static_cast<float>(i) / NX;
                const float localZ = bow * std::sin(m3::kPi * u);
                const float dbow = bow * m3::kPi * std::cos(m3::kPi * u);
                const m3::Vec3 localN = m3::normalize(
                    {-dbow / std::max(pageW, 0.0001f), 0.0f, 1.0f});
                const m3::Vec3 localP = {u * pageW, (v - 0.5f) * pageH, localZ};
                const m3::Vec3 world = m3::transformPoint(model, localP);
                m3::Vec3 normal = m3::normalize(m3::transformDir(model, localN));
                if (back) normal = normal * -1.0f;
                const float diff = std::max(0.0f, m3::dot(normal, kLightDir));
                float shade = 0.60f + 0.40f * diff;
                if (back) shade = std::max(shade, 0.72f);
                const float glow = l.grabLift * 0.035f;
                SDL_FColor color = {
                    std::min(1.0f, shade + glow),
                    std::min(1.0f, shade * 0.98f + glow * 0.7f),
                    std::min(1.0f, shade * 0.93f),
                    1.0f};
                const Projected p = projectPoint(view, viewProj, world);
                SDL_Vertex sv;
                sv.position = {p.x, p.y};
                sv.color = color;
                sv.tex_coord = {u, 1.0f - v};
                verts.push_back(sv);
            }
        }
        for (int j = 0; j < NY; ++j) {
            for (int i = 0; i < NX; ++i) {
                const int a = j * (NX + 1) + i;
                const int b = a + 1;
                const int c = a + (NX + 1);
                const int d = c + 1;
                indices.insert(indices.end(), {a, b, c, b, d, c});
            }
        }
        SDL_RenderGeometry(renderer_, tex, verts.data(), static_cast<int>(verts.size()),
                           indices.data(), static_cast<int>(indices.size()));
    }
}

void Renderer::drawText(const std::string &text, float x, float y, int px, Color color,
                        int tracking, bool shadow) {
    if (shadow) {
        drawText(text, x + 1.5f, y + 1.5f, px, {0.0f, 0.0f, 0.0f, color.a * 0.55f},
                 tracking, false);
    }
    float cx = x;
    const float uScale = 1.0f / kAtlasW;
    const float vScale = 1.0f / kAtlasH;
    for (char ch : text) {
        if (ch == ' ') {
            cx += (5 + tracking) * px;
            continue;
        }
        const int g = glyphIndex(ch);
        const int col = g % kGlyphCols;
        const int row = g / kGlyphCols;
        const float u0 = (col * kGlyphCellW) * uScale;
        const float v0 = (row * kGlyphCellH) * vScale;
        const float u1 = (col * kGlyphCellW + 5) * uScale;
        const float v1 = (row * kGlyphCellH + 7) * vScale;
        const float x1 = cx + 5.0f * px;
        const float y1 = y + 7.0f * px;
        const SDL_FColor c = {color.r, color.g, color.b, color.a};
        const int base = static_cast<int>(textVerts_.size());
        SDL_Vertex a{{cx, y}, c, {u0, v0}};
        SDL_Vertex b{{x1, y}, c, {u1, v0}};
        SDL_Vertex cc{{x1, y1}, c, {u1, v1}};
        SDL_Vertex d{{cx, y1}, c, {u0, v1}};
        textVerts_.insert(textVerts_.end(), {a, b, cc, d});
        textIndices_.insert(textIndices_.end(), {base, base + 1, base + 2,
                                                 base, base + 2, base + 3});
        cx += (5 + tracking) * px;
    }
}

void Renderer::flushText() {
    if (textVerts_.empty()) return;
    SDL_RenderGeometry(renderer_, fontTex_, textVerts_.data(),
                       static_cast<int>(textVerts_.size()), textIndices_.data(),
                       static_cast<int>(textIndices_.size()));
    textVerts_.clear();
    textIndices_.clear();
}

void Renderer::drawUi(const Book &book, const std::string &title,
                      const std::string &toast) {
    const Color paper{0.94f, 0.92f, 0.86f, 1.0f};
    const Color muted{0.56f, 0.61f, 0.67f, 1.0f};
    const Color accent{1.00f, 0.72f, 0.30f, 1.0f};
    const Color held{0.55f, 1.00f, 0.62f, 1.0f};
    const Color danger{1.00f, 0.46f, 0.38f, 1.0f};

    const int titlePx = 3;
    const int bodyPx = 2;
    const int smallPx = 2;
    const float pad = 12.0f;
    const float lineGap = 6.0f;

    const int n = book.pageCount();
    const int current = book.current();
    const int shownPage = n > 0 ? std::min(current + 1, n) : 0;

    char pageLine[128];
    std::snprintf(pageLine, sizeof(pageLine),
                  "PAGE %03d / %03d     LEFT %03d   RIGHT %03d",
                  shownPage, n, current, std::max(0, n - current));
    char handLine[160];
    if (book.rightHeld()) {
        std::snprintf(handLine, sizeof(handLine),
                      "RIGHT HAND HOLDS %d PAGES  -  K RELEASE",
                      book.rightHeldCount());
    } else if (book.leftHeld()) {
        std::snprintf(handLine, sizeof(handLine),
                      "LEFT HAND HOLDS %d PAGES  -  S RELEASE",
                      book.leftHeldCount());
    } else {
        std::snprintf(handLine, sizeof(handLine), "HANDS RELEASED  -  K / S GRAB");
    }

    const bool hasToast = !toast.empty();
    float contentH = 7.0f * titlePx + lineGap + 7.0f * smallPx + lineGap +
                     7.0f * bodyPx + lineGap + 7.0f * bodyPx;
    if (hasToast) contentH += lineGap + 7.0f * smallPx;
    contentH += lineGap + 10.0f + 4.0f;

    const float topX = 18.0f;
    const float topY = 16.0f;
    const float topW = 640.0f;
    const float topH = pad * 2.0f + contentH;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 3, 4, 6, 150);
    SDL_FRect topRect{topX, topY, topW, topH};
    SDL_RenderFillRect(renderer_, &topRect);
    SDL_SetRenderDrawColor(renderer_, 70, 85, 100, 140);
    SDL_RenderRect(renderer_, &topRect);

    float tx = topX + pad;
    float ty = topY + pad;
    drawText("PDF VIEWER / BOOK MODE", tx, ty, titlePx, accent, 1, true);
    ty += 7.0f * titlePx + lineGap;

    bool asciiTitle = true;
    for (unsigned char c : title) {
        if (c >= 128) { asciiTitle = false; break; }
    }
    std::string titleLine = asciiTitle
        ? title
        : ("PDF DOCUMENT / " + std::to_string(n) + " PAGES");
    const float titleMax = topW - pad * 2.0f;
    while (!titleLine.empty() && textWidth(titleLine, smallPx) > titleMax) {
        titleLine.pop_back();
    }
    if (titleLine.size() != title.size() && titleLine.size() >= 3) {
        titleLine.resize(titleLine.size() - 3);
        titleLine += "...";
    }
    drawText(titleLine, tx, ty, smallPx, muted);
    ty += 7.0f * smallPx + lineGap;

    drawText(pageLine, tx, ty, bodyPx, paper, 1, true);
    ty += 7.0f * bodyPx + lineGap;

    drawText(handLine, tx, ty, bodyPx,
             book.rightHeld() || book.leftHeld() ? held : muted);
    ty += 7.0f * bodyPx + lineGap;

    if (hasToast) {
        std::string t = toast;
        while (!t.empty() && textWidth(t, smallPx) > titleMax) t.pop_back();
        drawText(t, tx, ty, smallPx, danger);
        ty += 7.0f * smallPx + lineGap;
    }

    const float trackX = tx;
    const float trackY = topY + topH - pad - 4.0f;
    const float trackW = topW - pad * 2.0f;
    SDL_FRect track{trackX, trackY, trackW, 3.0f};
    SDL_SetRenderDrawColor(renderer_, 90, 104, 118, 82);
    SDL_RenderFillRect(renderer_, &track);
    const float frac = n > 0 ? static_cast<float>(current) / n : 0.0f;
    SDL_FRect fill{trackX, trackY, trackW * frac, 3.0f};
    SDL_SetRenderDrawColor(renderer_, 255, 184, 77, 217);
    SDL_RenderFillRect(renderer_, &fill);
    SDL_FRect marker{trackX + trackW * frac - 2.0f, trackY - 2.0f, 4.0f, 7.0f};
    SDL_SetRenderDrawColor(renderer_, 255, 184, 77, 255);
    SDL_RenderFillRect(renderer_, &marker);

    // Controls.
    const float bw = 480.0f;
    const float lineH = 19.0f;
    const float bh = pad * 2.0f + lineH * 5.0f;
    const float bx = 18.0f;
    const float by = static_cast<float>(kLogicalH) - bh - 18.0f;
    SDL_FRect bottomRect{bx, by, bw, bh};
    SDL_SetRenderDrawColor(renderer_, 3, 4, 6, 150);
    SDL_RenderFillRect(renderer_, &bottomRect);
    SDL_SetRenderDrawColor(renderer_, 70, 85, 100, 140);
    SDL_RenderRect(renderer_, &bottomRect);

    const float colW = bw * 0.5f;
    const Color leftKey = book.leftHeld() ? held : accent;
    const Color rightKey = book.rightHeld() ? held : accent;

    auto line = [&](float x, float y, const char *key, const std::string &text,
                    Color keyColor) {
        drawText(key, x, y, bodyPx, keyColor);
        drawText(text, x + textWidth(key, bodyPx) + 5.0f, y, bodyPx, paper);
    };

    float ly = by + pad;
    line(bx + pad, ly, "S", "LEFT GRAB", leftKey);
    line(bx + pad + colW, ly, "K", "RIGHT GRAB", rightKey);
    ly += lineH;
    char gripL[48], gripR[48];
    std::snprintf(gripL, sizeof(gripL), "LEFT GRIP %d", book.leftGrip());
    std::snprintf(gripR, sizeof(gripR), "RIGHT GRIP %d", book.rightGrip());
    line(bx + pad, ly, "A", gripL, leftKey);
    line(bx + pad + colW, ly, "L", gripR, rightKey);
    ly += lineH;
    line(bx + pad, ly, "J", "FLICK RIGHT", accent);
    line(bx + pad + colW, ly, "D", "FLICK LEFT", accent);
    ly += lineH;
    line(bx + pad, ly, "M", "NEXT PAGE", accent);
    line(bx + pad + colW, ly, "C", "PREV PAGE", accent);
    ly += lineH;
    line(bx + pad, ly, "Q", "QUIT", accent);
    drawText("L / A CYCLE GRIP", bx + pad + colW, ly, smallPx, muted);

    flushText();
}

void Renderer::render(const Book &book, PageSource &source, const std::string &title,
                      const std::string &toast) {
    ++frameCounter_;
    SDL_SetRenderDrawColor(renderer_, 9, 12, 17, 255);
    SDL_RenderClear(renderer_);

    drawBackground();

    const int n = book.pageCount();
    const float aspect = n > 0 ? std::max(0.25f, source.aspect(0)) : 0.707f;
    const float pageH = 1.5f;
    const float pageW = pageH * aspect;
    const float bookScale = std::min(
        1.0f, std::min(2.55f / (2.0f * pageW), 1.62f / pageH));
    const float extent = std::max(2.0f * pageW * bookScale, pageH * bookScale);
    const float dist = 3.15f * (extent / 2.10f);

    const m3::Vec3 eyeDir = m3::normalize({0.10f, 1.55f, 1.05f});
    const m3::Vec3 eye = eyeDir * dist;
    const m3::Mat4 view = m3::lookAt(eye, {0.0f, -0.02f, 0.0f}, {0.0f, 1.0f, 0.0f});
    const m3::Mat4 proj = m3::perspective(
        42.0f * m3::kPi / 180.0f,
        static_cast<float>(kLogicalW) / kLogicalH, 0.03f, 60.0f);
    const m3::Mat4 viewProj = m3::mul(proj, view);

    const int stackLeaves = std::min(n, 14);
    const float stackDepth = static_cast<float>(stackLeaves) * 0.0035f * bookScale;
    const float coverY = -stackDepth - 0.010f;
    const float floorY = coverY - 0.012f;
    const float halfW = pageW * bookScale;
    const float halfH = pageH * bookScale * 0.5f;

    drawFloor(view, viewProj, floorY);
    drawShadow(view, viewProj, floorY, halfW, halfH);
    drawCover(view, viewProj, coverY, halfW, halfH, bookScale);

    const float yTop = -0.0015f;
    const float yBottom = coverY + 0.002f;
    if (book.current() > 0) {
        drawBlock(view, viewProj, -halfW, 0.0f, -halfH, halfH, yBottom, yTop);
    }
    if (book.current() < n) {
        drawBlock(view, viewProj, 0.0f, halfW, -halfH, halfH, yBottom, yTop);
    }

    drawPages(book, source, view, viewProj, pageW, pageH, bookScale);
    drawUi(book, title, toast);
}

void Renderer::present() {
    if (renderer_) SDL_RenderPresent(renderer_);
}
