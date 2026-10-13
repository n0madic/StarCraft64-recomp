#include "video.hpp"
#include "input/devices.hpp"
#include "log.hpp"
#include <SDL3/SDL.h>
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb/stb_truetype.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <stdexcept>

namespace sc64::shell {
namespace {
constexpr int first_char = 32, char_count = 95;
constexpr int atlas_size = 512;
constexpr float bake_pixels = 40;
// The former engine font is 14 design units high; text scales with the
// window like the 960x720 design space, but uniformly.
constexpr double font_design_size = 14;
SDL_FColor fcolor(const std::array<double, 4>& c) {
    return {float(c[0]), float(c[1]), float(c[2]), float(c[3])};
}
[[noreturn]] void fail(const char* what) { throw std::runtime_error(std::string(what) + ": " + SDL_GetError()); }
}

Font::~Font() { release(); }

void Font::release() {
    if (texture_) SDL_DestroyTexture(texture_);
    texture_ = nullptr;
}

void Font::load(SDL_Renderer* renderer, const std::string& path) {
    release();
    std::ifstream file(path, std::ios::binary);
    SDL_IOStream* stream = nullptr;
    std::vector<unsigned char> data;
    if (file) data.assign(std::istreambuf_iterator<char>(file), {});
    else if ((stream = SDL_IOFromFile(path.c_str(), "rb"))) {
        // Android assets are reachable only through SDL's asset stream.
        const auto size = SDL_GetIOSize(stream);
        if (size > 0) {
            data.resize(std::size_t(size));
            if (SDL_ReadIO(stream, data.data(), data.size()) != data.size()) data.clear();
        }
        SDL_CloseIO(stream);
    }
    if (data.empty()) throw std::runtime_error("Overlay font unavailable: " + path);
    std::vector<unsigned char> alpha(atlas_size * atlas_size);
    std::vector<stbtt_bakedchar> baked(char_count);
    if (stbtt_BakeFontBitmap(data.data(), 0, bake_pixels, alpha.data(), atlas_size, atlas_size, first_char,
                             char_count, baked.data()) <= 0)
        throw std::runtime_error("Overlay font atlas does not fit");
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0)))
        throw std::runtime_error("Invalid overlay font");
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
    const float scale = stbtt_ScaleForPixelHeight(&info, bake_pixels);
    ascent_ = float(ascent) * scale; line_ = float(ascent - descent + gap) * scale;
    bake_size_ = bake_pixels; atlas_ = float(atlas_size);
    std::vector<std::uint8_t> rgba(alpha.size() * 4);
    for (std::size_t i = 0; i < alpha.size(); ++i) {
        rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = alpha[i];
    }
    texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, atlas_size, atlas_size);
    if (!texture_) fail("Overlay font texture");
    SDL_UpdateTexture(texture_, nullptr, rgba.data(), atlas_size * 4);
    SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_LINEAR);
    glyphs_.clear();
    for (const auto& c : baked)
        glyphs_.push_back({float(c.x0), float(c.y0), float(c.x1), float(c.y1), c.xoff, c.yoff, c.xadvance});
}

void Font::draw(SDL_Renderer* renderer, const std::string& text, float cx, float cy, float size,
                const std::array<double, 4>& color, float wrap) const {
    if (!texture_ || text.empty() || size <= 0) return;
    const float k = size / bake_size_;
    const auto width = [&](std::string_view line) {
        float w = 0;
        for (const unsigned char ch : line) {
            if (ch >= first_char && ch < first_char + char_count) w += glyphs_[ch - first_char].advance * k;
        }
        return w;
    };
    // Explicit newlines first, then greedy word wrap within `wrap`.
    std::vector<std::string> lines;
    std::string paragraph;
    const auto flush = [&](const std::string& value) {
        if (wrap <= 0 || width(value) <= wrap) { lines.push_back(value); return; }
        std::string line, word;
        std::size_t start = 0;
        while (start <= value.size()) {
            const auto space = value.find(' ', start);
            word = value.substr(start, space == std::string::npos ? std::string::npos : space - start);
            const auto candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && width(candidate) > wrap) { lines.push_back(line); line = word; }
            else line = candidate;
            if (space == std::string::npos) break;
            start = space + 1;
        }
        lines.push_back(line);
    };
    for (const char ch : text) {
        if (ch == '\n') { flush(paragraph); paragraph.clear(); }
        else paragraph += ch;
    }
    flush(paragraph);
    const float line_height = line_ * k;
    float top = cy - line_height * float(lines.size()) / 2;
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    const auto fc = fcolor(color);
    for (const auto& line : lines) {
        float x = cx - width(line) / 2;
        const float baseline = top + ascent_ * k;
        for (const unsigned char ch : line) {
            if (ch < first_char || ch >= first_char + char_count) continue;
            const auto& g = glyphs_[ch - first_char];
            const float x0 = x + g.xoff * k, y0 = baseline + g.yoff * k;
            const float x1 = x0 + (g.x1 - g.x0) * k, y1 = y0 + (g.y1 - g.y0) * k;
            const int base = int(vertices.size());
            vertices.push_back({{x0, y0}, fc, {g.x0 / atlas_, g.y0 / atlas_}});
            vertices.push_back({{x1, y0}, fc, {g.x1 / atlas_, g.y0 / atlas_}});
            vertices.push_back({{x1, y1}, fc, {g.x1 / atlas_, g.y1 / atlas_}});
            vertices.push_back({{x0, y1}, fc, {g.x0 / atlas_, g.y1 / atlas_}});
            for (int i : {0, 1, 2, 0, 2, 3}) indices.push_back(base + i);
            x += g.advance * k;
        }
        top += line_height;
    }
    if (!vertices.empty())
        SDL_RenderGeometry(renderer, texture_, vertices.data(), int(vertices.size()), indices.data(), int(indices.size()));
}

Video::~Video() { destroy(); }

void Video::create(SDL_Window* window, const std::string& driver, const std::string& font, const std::string& icon) {
    window_ = window; font_path_ = font; icon_path_ = icon;
    SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window);
    if (!driver.empty()) SDL_SetStringProperty(properties, SDL_PROP_RENDERER_CREATE_NAME_STRING, driver.c_str());
    SDL_SetNumberProperty(properties, SDL_PROP_RENDERER_CREATE_PRESENT_VSYNC_NUMBER, 1);
    renderer_ = SDL_CreateRendererWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (!renderer_) fail("Renderer creation failed");
    log::info("SC64 presentation initialized through SDL renderer %s", name().c_str());
    // SDL tries a comma-separated list in order; say so when the first choice failed.
    const std::string requested = driver.substr(0, driver.find(','));
    if (!requested.empty() && SDL_strcasecmp(requested.c_str(), name().c_str()) != 0)
        log::warning("SC64 requested renderer %s unavailable; using %s", requested.c_str(), name().c_str());
    font_.load(renderer_, font_path_);
    load_icon();
}

// The icon is a nicety: a missing or unreadable file only leaves the start screen without it.
void Video::load_icon() {
    if (icon_) SDL_DestroyTexture(icon_);
    icon_ = nullptr;
    if (icon_path_.empty()) return;
    SDL_Surface* surface = SDL_LoadPNG(icon_path_.c_str());
    if (!surface) { log::debug("Start screen icon %s: %s", icon_path_.c_str(), SDL_GetError()); return; }
    icon_ = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_DestroySurface(surface);
    if (!icon_) { log::debug("Start screen icon texture: %s", SDL_GetError()); return; }
    SDL_SetTextureScaleMode(icon_, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(icon_, SDL_BLENDMODE_BLEND);
}

void Video::destroy() {
    font_.release();
    if (icon_) SDL_DestroyTexture(icon_);
    icon_ = nullptr;
    if (texture_) SDL_DestroyTexture(texture_);
    texture_ = nullptr; texture_width_ = texture_height_ = 0;
    if (renderer_) SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
}

void Video::reset() {
    if (texture_) SDL_DestroyTexture(texture_);
    texture_ = nullptr; texture_width_ = texture_height_ = 0;
    font_.load(renderer_, font_path_);
    load_icon();
    if (last_) upload(*last_);
}

void Video::ensure_texture(unsigned width, unsigned height) {
    if (texture_ && texture_width_ == width && texture_height_ == height) return;
    if (texture_) SDL_DestroyTexture(texture_);
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, int(width), int(height));
    if (!texture_) fail("Presentation texture");
    // Original raster, nearest sampling, opaque.
    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_NONE);
    texture_width_ = width; texture_height_ = height;
}

void Video::upload(const Native::Upload& image) {
    ensure_texture(image.width, image.height);
    if (!SDL_UpdateTexture(texture_, nullptr, image.rgba->data(), int(image.width * 4))) fail("Presentation upload");
    last_ = image;
}

std::pair<double, double> Video::size() const {
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
    return {double(w), double(h)};
}

std::string Video::name() const {
    const char* value = renderer_ ? SDL_GetRendererName(renderer_) : nullptr;
    return value ? value : "";
}

// Overlay boxes: design-space centre/size (960x720, bottom-left, stretched over
// the window), rotation in degrees counter-clockwise.
void Video::box(const OverlayNode& node, double sx, double sy, int height) const {
    const double cx = node.x * sx, cy = node.y * sy;
    const double hw = node.width * sx / 2, hh = node.height * sy / 2;
    const double angle = node.rotation * std::numbers::pi / 180;
    const double c = std::cos(angle), s = std::sin(angle);
    const std::array<std::pair<double, double>, 4> corners = {{{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}}};
    std::array<SDL_Vertex, 4> vertices{};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto [x, y] = corners[i];
        vertices[i].position = {float(cx + x * c - y * s), float(height - (cy + x * s + y * c))};
        vertices[i].color = fcolor(node.color);
    }
    const int indices[] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer_, nullptr, vertices.data(), 4, indices, 6);
}

void Video::draw_nodes(const std::vector<OverlayNode>& nodes, double sx, double sy, int height) {
    const float text_scale = float(std::min(sx, sy) * font_design_size);
    for (const auto& node : nodes) {
        if (!node.enabled) continue;
        switch (node.kind) {
        case OverlayNode::Kind::box: box(node, sx, sy, height); break;
        case OverlayNode::Kind::text:
            font_.draw(renderer_, node.text, float(node.x * sx), float(height - node.y * sy), text_scale * float(node.scale),
                       node.color, node.line_break ? float(node.width * sx) : 0);
            break;
        case OverlayNode::Kind::image: {
            if (!icon_) break;
            // The sizes of an image node already account for the stretch; the texture is square.
            const float w = float(node.width * sx), h = float(node.height * sy);
            const SDL_FRect target{float(node.x * sx) - w / 2, float(height - node.y * sy) - h / 2, w, h};
            SDL_RenderTexture(renderer_, icon_, nullptr, &target);
            break;
        }
        }
    }
}

void Video::draw(bool image_visible, unsigned width, unsigned height, const std::vector<OverlayNode>* overlay,
                 const std::vector<OverlayNode>* start_screen, const OverlayNode* status) {
    const auto [w, h] = size();
    SDL_SetRenderViewport(renderer_, nullptr);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    // Nothing for an empty window (e.g. minimised) or image.
    if (const auto rect = input::viewport::rectangle(w, h, width, height); rect && image_visible && texture_) {
        // The Fit rectangle has a bottom-left origin, like pointer inversion.
        const SDL_FRect target{float(rect->x), float(h - rect->y - rect->height), float(rect->width), float(rect->height)};
        SDL_RenderTexture(renderer_, texture_, nullptr, &target);
    }
    if (!overlay) return;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const double sx = w / design_width, sy = h / design_height;
    draw_nodes(*overlay, sx, sy, int(h));
    if (start_screen) draw_nodes(*start_screen, sx, sy, int(h));
    if (status && status->enabled && !status->text.empty()) draw_nodes({*status}, sx, sy, int(h));
}

Readback Video::readback() const {
    Readback result;
    SDL_Surface* surface = SDL_RenderReadPixels(renderer_, nullptr);
    if (!surface) fail("Window readback");
    SDL_Surface* converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_BGRA32);
    SDL_DestroySurface(surface);
    if (!converted) fail("Window readback conversion");
    result.width = unsigned(converted->w); result.height = unsigned(converted->h);
    result.bgra.resize(std::size_t(result.width) * result.height * 4);
    for (unsigned row = 0; row < result.height; ++row)
        std::copy_n(static_cast<const std::uint8_t*>(converted->pixels) + std::size_t(row) * converted->pitch,
                    std::size_t(result.width) * 4, result.bgra.data() + std::size_t(row) * result.width * 4);
    SDL_DestroySurface(converted);
    return result;
}

void Video::present() { SDL_RenderPresent(renderer_); }
}
