#include "page_source.hpp"

#include <CoreGraphics/CoreGraphics.h>
#include <Foundation/Foundation.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace {

struct PdfPageInfo {
    double width = 0.0;
    double height = 0.0;
};

PdfPageInfo pageInfo(CGPDFPageRef page) {
    CGRect box = CGPDFPageGetBoxRect(page, kCGPDFMediaBox);
    int rot = CGPDFPageGetRotationAngle(page) % 360;
    if (rot < 0) rot += 360;
    const bool swapped = (rot == 90 || rot == 270);
    PdfPageInfo info;
    info.width = swapped ? box.size.height : box.size.width;
    info.height = swapped ? box.size.width : box.size.height;
    if (info.width <= 0.0) info.width = 1.0;
    if (info.height <= 0.0) info.height = 1.0;
    return info;
}

class PdfSource final : public PageSource {
public:
    explicit PdfSource(CGPDFDocumentRef doc, std::string title)
        : doc_(doc), title_(std::move(title)) {
        count_ = static_cast<int>(CGPDFDocumentGetNumberOfPages(doc_));
    }

    ~PdfSource() override {
        if (doc_) CGPDFDocumentRelease(doc_);
    }

    int pageCount() const override { return count_; }

    float aspect(int page) const override {
        CGPDFPageRef p = pageRef(page);
        if (!p) return 0.707f;
        const PdfPageInfo info = pageInfo(p);
        return static_cast<float>(info.width / info.height);
    }

    bool render(int page, int maxDim, PageImage &out) override {
        CGPDFPageRef p = pageRef(page);
        if (!p) return false;
        const PdfPageInfo info = pageInfo(p);
        const double scale = static_cast<double>(std::max(64, maxDim)) / info.height;
        const int w = std::max(1, static_cast<int>(std::lround(info.width * scale)));
        const int h = std::max(1, static_cast<int>(std::lround(info.height * scale)));

        out.width = w;
        out.height = h;
        out.rgba.assign(static_cast<size_t>(w) * h * 4, 255);

        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(out.rgba.data(), w, h, 8,
                                                 static_cast<size_t>(w) * 4, cs,
                                                 kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        CGColorSpaceRelease(cs);
        if (!ctx) return false;

        CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0);
        CGContextFillRect(ctx, CGRectMake(0, 0, w, h));
        CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
        CGContextSetAllowsAntialiasing(ctx, true);
        CGContextSetShouldAntialias(ctx, true);

        // CGPDFPageGetDrawingTransform handles the page's /Rotate and media box.
        // The bitmap context stays in its default orientation; the raw buffer is
        // top-down and is uploaded directly as an SDL texture.
        CGAffineTransform transform = CGPDFPageGetDrawingTransform(
            p, kCGPDFMediaBox, CGRectMake(0, 0, w, h), 0, true);
        CGContextConcatCTM(ctx, transform);
        CGContextDrawPDFPage(ctx, p);

        CGContextRelease(ctx);
        return true;
    }

    std::string title() const override { return title_; }

private:
    CGPDFPageRef pageRef(int page) const {
        if (page < 0 || page >= count_) return nullptr;
        return CGPDFDocumentGetPage(doc_, static_cast<size_t>(page + 1));
    }

    CGPDFDocumentRef doc_ = nullptr;
    int count_ = 0;
    std::string title_;
};

} // namespace

std::unique_ptr<PageSource> createPdfSource(const std::string &path) {
    @autoreleasepool {
        NSString *p = [NSString stringWithUTF8String:path.c_str()];
        if (!p) return nullptr;
        NSURL *url = [NSURL fileURLWithPath:p];
        CGPDFDocumentRef doc = CGPDFDocumentCreateWithURL((__bridge CFURLRef)url);
        if (!doc) return nullptr;
        if (CGPDFDocumentGetNumberOfPages(doc) == 0) {
            CGPDFDocumentRelease(doc);
            return nullptr;
        }
        std::string title = std::filesystem::path(path).filename().string();
        if (title.empty()) title = path;
        return std::make_unique<PdfSource>(doc, title);
    }
}
