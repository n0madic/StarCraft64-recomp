#pragma once
#include "native.hpp"
#include "scripts.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Window;

// Main-thread presentation: the original image with nearest sampling in the
// shared Fit viewport, then the overlay. No other thread touches SDL video.
namespace sc64::shell {
class Font {
public:
    ~Font();
    // Bakes printable ASCII from a TrueType file; throws on failure.
    void load(SDL_Renderer* renderer, const std::string& path);
    void release();
    // Draws centred text; `size` is the em height in pixels. `wrap` > 0 breaks
    // lines at spaces to fit that pixel width.
    void draw(SDL_Renderer* renderer, const std::string& text, float cx, float cy, float size,
              const std::array<double, 4>& color, float wrap = 0) const;
private:
    struct Glyph { float x0, y0, x1, y1, xoff, yoff, advance; };
    SDL_Texture* texture_ = nullptr;
    std::vector<Glyph> glyphs_;
    float bake_size_ = 0, ascent_ = 0, line_ = 0, atlas_ = 0;
};

class Video {
public:
    ~Video();
    // `icon` is a PNG for the start screen; without it (or if it cannot be read) the screen has no icon.
    void create(SDL_Window* window, const std::string& driver, const std::string& font, const std::string& icon = {});
    bool has_icon() const { return icon_ != nullptr; }
    void destroy();
    // Recreates device objects after SDL_EVENT_RENDER_DEVICE_RESET.
    void reset();
    void upload(const Native::Upload& image);
    // One frame: black clear, Fit image, overlay, start screen and status text (all skipped while probing).
    void draw(bool image_visible, unsigned width, unsigned height, const std::vector<OverlayNode>* overlay,
              const std::vector<OverlayNode>* start_screen, const OverlayNode* status);
    Readback readback() const;
    void present();
    std::string name() const;
    std::pair<double, double> size() const;
private:
    void ensure_texture(unsigned width, unsigned height);
    void box(const OverlayNode& node, double sx, double sy, int height) const;
    void draw_nodes(const std::vector<OverlayNode>& nodes, double sx, double sy, int height);
    void load_icon();
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    unsigned texture_width_ = 0, texture_height_ = 0;
    std::string font_path_, icon_path_;
    SDL_Texture* icon_ = nullptr;
    Font font_;
    std::optional<Native::Upload> last_;
};
}
