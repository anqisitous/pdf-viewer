#pragma once

#include "book.hpp"
#include "math3d.hpp"
#include "page_source.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <unordered_map>
#include <vector>

struct Color {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

class Renderer {
public:
    bool init(SDL_Window *window);
    void shutdown();

    void render(const Book &book, PageSource &source, const std::string &title,
                const std::string &toast);
    void present();

private:
    struct TextureEntry {
        SDL_Texture *tex = nullptr;
        int width = 0;
        int height = 0;
        unsigned long long lastUsed = 0;
    };

    SDL_Texture *ensureTexture(int page, PageSource &source);
    void evictTextures();
    SDL_Texture *makeTexture(int w, int h, const std::vector<unsigned char> &rgba,
                             SDL_BlendMode blend, SDL_ScaleMode scale);

    void drawBackground();
    void drawFloor(const m3::Mat4 &view, const m3::Mat4 &viewProj, float floorY);
    void drawShadow(const m3::Mat4 &view, const m3::Mat4 &viewProj, float floorY,
                    float halfW, float halfH);
    void drawCover(const m3::Mat4 &view, const m3::Mat4 &viewProj, float coverY,
                   float halfW, float halfH, float scale);
    void drawBlock(const m3::Mat4 &view, const m3::Mat4 &viewProj, float x0, float x1,
                   float z0, float z1, float yBottom, float yTop);
    void drawPages(const Book &book, PageSource &source, const m3::Mat4 &view,
                   const m3::Mat4 &viewProj, float pageW, float pageH, float bookScale);
    void drawUi(const Book &book, const std::string &title, const std::string &toast);

    void drawText(const std::string &text, float x, float y, int px, Color color,
                  int tracking = 1, bool shadow = false);
    void flushText();

    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *paperTex_ = nullptr;
    SDL_Texture *edgeTex_ = nullptr;
    SDL_Texture *shadowTex_ = nullptr;
    SDL_Texture *fontTex_ = nullptr;

    std::unordered_map<int, TextureEntry> textures_;
    unsigned long long frameCounter_ = 0;

    std::vector<SDL_Vertex> textVerts_;
    std::vector<int> textIndices_;
};
