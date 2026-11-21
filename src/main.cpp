#include "book.hpp"
#include "page_source.hpp"
#include "renderer.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <memory>
#include <string>

namespace {

void handleKey(const SDL_KeyboardEvent &key, Book &book, bool &running) {
    if (key.repeat) return;
    switch (key.key) {
        case SDLK_S: book.toggleLeftGrab(); break;
        case SDLK_K: book.toggleRightGrab(); break;
        case SDLK_A: book.decreaseLeftGrip(); break;
        case SDLK_L: book.decreaseRightGrip(); break;
        case SDLK_J: book.flickRight(); break;
        case SDLK_D: book.flickLeft(); break;
        case SDLK_M: book.nextPage(); break;
        case SDLK_C: book.prevPage(); break;
        case SDLK_Q:
        case SDLK_ESCAPE: running = false; break;
        default: break;
    }
}

} // namespace

int main(int argc, char **argv) {
    std::string pdfPath;
    bool demoOnly = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--demo") { demoOnly = true; continue; }
        if (pdfPath.empty() && a.rfind("--", 0) != 0) pdfPath = a;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "PDF Viewer / Physical Book", 1280, 800,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 720, 480);

    Renderer renderer;
    if (!renderer.init(window)) {
        std::fprintf(stderr, "Renderer init failed\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::unique_ptr<PageSource> source;
    std::string toast;
    Uint64 toastUntil = 0;

    if (!demoOnly && !pdfPath.empty()) {
        source = createPdfSource(pdfPath);
        if (!source) {
            toast = "COULD NOT OPEN PDF - SHOWING DEMO BOOK";
            toastUntil = SDL_GetTicks() + 5500;
        }
    }
    if (!source) source = createDemoSource();

    Book book(source->pageCount());
    const std::string title = source->title();
    SDL_SetWindowTitle(window, ("PDF Viewer | " + title).c_str());

    bool running = true;
    Uint64 last = SDL_GetTicks();
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    handleKey(event.key, book, running);
                    break;
                default:
                    break;
            }
        }

        const Uint64 now = SDL_GetTicks();
        float dt = static_cast<float>(now - last) / 1000.0f;
        last = now;
        if (dt > 0.1f) dt = 0.1f;

        book.update(dt);
        const std::string activeToast =
            (now < toastUntil) ? toast : std::string();
        renderer.render(book, *source, title, activeToast);
        renderer.present();
    }

    renderer.shutdown();
    source.reset();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
