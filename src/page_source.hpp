#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct PageImage {
    int width = 0;
    int height = 0;
    // RGBA, row 0 is the top row.  The renderer flips rows when uploading.
    std::vector<uint8_t> rgba;
};

class PageSource {
public:
    virtual ~PageSource() = default;
    virtual int pageCount() const = 0;
    virtual float aspect(int page) const = 0;
    virtual bool render(int page, int maxDim, PageImage &out) = 0;
    virtual std::string title() const = 0;
};

// Returns nullptr when the file cannot be opened as a PDF (or on a platform
// without the macOS PDF bridge).
std::unique_ptr<PageSource> createPdfSource(const std::string &path);

std::unique_ptr<PageSource> createDemoSource();
