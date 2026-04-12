#pragma once

#include <backend/tray_host.hpp>
#include <bar/bar_state.hpp>
#include <bar_config.hpp>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <surface.hpp>

#include <string>
#include <vector>

namespace bar::widgets {

struct TagHit {
    int x0    = 0;
    int x1    = 0;
    int ws_id = -1;
};

class PaintContext {
    public:
        PaintContext(Surface& surface, const std::string& font_desc);
        ~PaintContext();

        int  width() const;
        int  height() const;

        void clear(const std::string& bg);
        int  text_width(const std::string& text);
        int  draw_text(int x,
            const std::string& text,
            const std::string& fg,
            const std::string& bg,
            int pad = 8);
        void draw_rect(int x, int y, int w, int h, const std::string& color);
        void present();

    private:
        Surface& surface_;
        PangoLayout* layout_             = nullptr;
        PangoFontDescription* font_desc_ = nullptr;
        cairo_t* cr_                     = nullptr;
};

class TagsWidget {
    public:
        std::vector<TagHit> draw(PaintContext& paint,
            const BarState& state,
            const BarConfig& cfg,
            int& cursor_x) const;
};

class TitleWidget {
    public:
        void draw_center(PaintContext& paint,
            const BarState& state,
            const BarConfig& cfg,
            int left_edge,
            int right_edge) const;
};

class TrayWidget {
    public:
        int  reserved_width(const backend::TrayHost* tray) const;
        void reposition(backend::TrayHost* tray, const Surface& bar) const;
};

} // namespace bar::widgets
