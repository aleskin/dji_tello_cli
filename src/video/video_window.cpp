#include "video/video_window.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace video
{
namespace
{

// 5x7 bitmap font for the OSD, upper case only (SDL2_ttf is not a dependency): one row per byte,
// bit 4 = left.
struct Glyph
{
    char c;
    std::array<std::uint8_t, 7> rows;
};

constexpr std::array<Glyph, 40> kFont{{
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
    {'%', {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
}};

constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
constexpr int kAdvance = kGlyphWidth + 1;

const Glyph* glyph(char c)
{
    const auto* it = std::ranges::find_if(kFont,
                                          [c](const Glyph& g)
                                          {
                                              return g.c == c;
                                          });
    return it == kFont.end() ? nullptr : &*it;
}

void draw_text(SDL_Renderer* renderer, std::string_view text, int x, int y, int scale)
{
    std::vector<SDL_Rect> pixels;
    for (const char c : text)
    {
        if (const Glyph* g = glyph(c))
        {
            for (int row = 0; row < kGlyphHeight; ++row)
            {
                for (int col = 0; col < kGlyphWidth; ++col)
                {
                    if (((static_cast<unsigned>(g->rows.at(static_cast<std::size_t>(row))) >>
                          static_cast<unsigned>(kGlyphWidth - 1 - col)) &
                         1U) != 0)
                    {
                        pixels.push_back({x + col * scale, y + row * scale, scale, scale});
                    }
                }
            }
        }
        x += kAdvance * scale;
    }
    SDL_RenderFillRects(renderer, pixels.data(), static_cast<int>(pixels.size()));
}

std::string key_name(SDL_Keycode key)
{
    std::string name = SDL_GetKeyName(key);
    std::ranges::transform(name, name.begin(),
                           [](unsigned char c)
                           {
                               return static_cast<char>(std::tolower(c));
                           });
    return name;
}

int text_width(std::string_view text, int scale)
{
    return static_cast<int>(text.size()) * kAdvance * scale;
}

// The picture scaled into the window with its aspect kept (black bars on the sides).
SDL_Rect fit(int pic_w, int pic_h, int out_w, int out_h)
{
    if (pic_w <= 0 || pic_h <= 0)
    {
        return {0, 0, out_w, out_h};
    }
    int w = out_w;
    int h = static_cast<int>(static_cast<std::int64_t>(out_w) * pic_h / pic_w);
    if (h > out_h)
    {
        h = out_h;
        w = static_cast<int>(static_cast<std::int64_t>(out_h) * pic_w / pic_h);
    }
    return {(out_w - w) / 2, (out_h - h) / 2, w, h};
}

struct SdlVideo
{
    bool ok;
    SdlVideo()
        : ok(SDL_InitSubSystem(SDL_INIT_VIDEO) == 0)
    {
    }
    SdlVideo(const SdlVideo&) = delete;
    SdlVideo& operator=(const SdlVideo&) = delete;
    SdlVideo(SdlVideo&&) = delete;
    SdlVideo& operator=(SdlVideo&&) = delete;
    ~SdlVideo()
    {
        if (ok)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
        SDL_Quit();
    }
};

struct WindowDeleter
{
    void operator()(SDL_Window* w) const
    {
        SDL_DestroyWindow(w);
    }
};
struct RendererDeleter
{
    void operator()(SDL_Renderer* r) const
    {
        SDL_DestroyRenderer(r);
    }
};
struct TextureDeleter
{
    void operator()(SDL_Texture* t) const
    {
        SDL_DestroyTexture(t);
    }
};

constexpr int kInitialWidth = 640;
constexpr int kInitialHeight = 480;
constexpr auto kFramePause = std::chrono::milliseconds(15);

} // namespace

VideoWindow::VideoWindow(ViewerSources sources, std::string title)
    : sources_(std::move(sources))
    , title_(std::move(title))
    , thread_(
          [this](const std::stop_token& stop)
          {
              run(stop);
          })
{
}

VideoWindow::~VideoWindow()
{
    thread_.request_stop();
    if (thread_.joinable())
    {
        thread_.join();
    }
}

bool VideoWindow::is_open() const
{
    return open_.load();
}

std::string VideoWindow::error() const
{
    const std::scoped_lock lock(mutex_);
    return error_;
}

std::set<std::string> VideoWindow::pressed_keys() const
{
    const std::scoped_lock lock(mutex_);
    return pressed_;
}

std::vector<std::string> VideoWindow::take_key_presses()
{
    const std::scoped_lock lock(mutex_);
    return std::exchange(presses_, {});
}

void VideoWindow::run(const std::stop_token& stop)
{
    auto fail = [this](const std::string& what)
    {
        const std::scoped_lock lock(mutex_);
        error_ = what + ": " + SDL_GetError();
        open_.store(false);
    };
    // Ctrl+C belongs to the CLI (lands the drone); SDL must not turn it into a quit event.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    const SdlVideo sdl;
    if (!sdl.ok)
    {
        fail("cannot start SDL video");
        return;
    }
    const std::unique_ptr<SDL_Window, WindowDeleter> window(
        SDL_CreateWindow(title_.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                         kInitialWidth, kInitialHeight, SDL_WINDOW_RESIZABLE));
    if (!window)
    {
        fail("cannot open the window");
        return;
    }
    std::unique_ptr<SDL_Renderer, RendererDeleter> renderer(
        SDL_CreateRenderer(window.get(), -1, 0));
    if (!renderer)
    {
        renderer.reset(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_SOFTWARE));
    }
    if (!renderer)
    {
        fail("cannot create a renderer");
        return;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    std::unique_ptr<SDL_Texture, TextureDeleter> texture;
    int tex_w = 0;
    int tex_h = 0;
    std::int64_t number = 0;
    while (!stop.stop_requested())
    {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0)
        {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE))
            {
                const std::scoped_lock lock(mutex_);
                pressed_.clear();
                open_.store(false);
                return;
            }
            const std::scoped_lock lock(mutex_);
            if (event.type == SDL_KEYDOWN)
            {
                const std::string name = key_name(event.key.keysym.sym);
                pressed_.insert(name);
                if (event.key.repeat == 0)
                {
                    presses_.push_back(name);
                }
            }
            else if (event.type == SDL_KEYUP)
            {
                pressed_.erase(key_name(event.key.keysym.sym));
            }
            else if (event.type == SDL_WINDOWEVENT &&
                     event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            {
                pressed_.clear(); // key-up events go to the other window now
            }
        }

        if (auto picture = sources_.picture ? sources_.picture(number) : std::nullopt)
        {
            if (!texture || picture->width != tex_w || picture->height != tex_h)
            {
                texture.reset(SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_IYUV,
                                                SDL_TEXTUREACCESS_STREAMING, picture->width,
                                                picture->height));
                tex_w = picture->width;
                tex_h = picture->height;
            }
            if (texture)
            {
                const int chroma = (picture->width + 1) / 2;
                SDL_UpdateYUVTexture(texture.get(), nullptr, picture->y.data(), picture->width,
                                     picture->u.data(), chroma, picture->v.data(), chroma);
                number = picture->number;
                ++shown_;
            }
        }

        int out_w = 0;
        int out_h = 0;
        SDL_GetRendererOutputSize(renderer.get(), &out_w, &out_h);
        const int scale = std::max(1, out_h / 240);
        SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255);
        SDL_RenderClear(renderer.get());
        SDL_SetRenderDrawColor(renderer.get(), 255, 255, 255, 255);
        if (texture && number > 0)
        {
            const SDL_Rect dest = fit(tex_w, tex_h, out_w, out_h);
            SDL_RenderCopy(renderer.get(), texture.get(), nullptr, &dest);
        }
        else
        {
            constexpr std::string_view waiting = "NO VIDEO";
            draw_text(renderer.get(), waiting, (out_w - text_width(waiting, scale * 2)) / 2,
                      (out_h - kGlyphHeight * scale * 2) / 2, scale * 2);
        }

        const std::string line = osd_text(sources_.osd ? sources_.osd() : Osd{});
        const int pad = 3 * scale;
        const SDL_Rect bar{0, 0, out_w, kGlyphHeight * scale + 2 * pad};
        SDL_SetRenderDrawBlendMode(renderer.get(), SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 140);
        SDL_RenderFillRect(renderer.get(), &bar);
        SDL_SetRenderDrawColor(renderer.get(), 255, 255, 255, 255);
        draw_text(renderer.get(), line, pad, pad, scale);
        SDL_RenderPresent(renderer.get());

        std::this_thread::sleep_for(kFramePause);
    }
}

} // namespace video
