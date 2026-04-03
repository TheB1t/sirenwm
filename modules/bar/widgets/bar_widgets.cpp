#include "bar_widgets.hpp"

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cstdio>

namespace bar::widgets {

namespace {

void parse_hex_color(const std::string& hex, double& r, double& g, double& b) {
    unsigned int rv = 0;
    unsigned int gv = 0;
    unsigned int bv = 0;
    if (hex.size() == 7 && hex[0] == '#')
        std::sscanf(hex.c_str() + 1, "%02x%02x%02x", &rv, &gv, &bv);
    r = rv / 255.0;
    g = gv / 255.0;
    b = bv / 255.0;
}

std::string xft_to_pango(const std::string& s) {
    auto colon = s.find(':');
    if (colon == std::string::npos)
        return s;

    std::string name = s.substr(0, colon);
    std::string rest = s.substr(colon + 1);
    std::string size;

    size_t      pos = 0;
    while (pos < rest.size()) {
        size_t      next = rest.find(':', pos);
        std::string kv   = rest.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        auto        eq   = kv.find('=');
        if (eq != std::string::npos) {
            std::string key = kv.substr(0, eq);
            std::string val = kv.substr(eq + 1);
            if (key == "size" || key == "pixelsize")
                size = val;
        }
        if (next == std::string::npos)
            break;
        pos = next + 1;
    }

    if (!size.empty())
        return name + " " + size;
    return name;
}

} // namespace

PaintContext::PaintContext(backend::RenderWindow& window, const std::string& font_desc)
    : window_(window) {
    cr_ = window_.cairo();
    if (!cr_)
        return;

    layout_ = pango_cairo_create_layout(cr_);
    if (!layout_)
        return;

    std::string pango_font = xft_to_pango(font_desc);
    font_desc_ = pango_font_description_from_string(pango_font.c_str());
    if (!font_desc_)
        return;
    pango_layout_set_font_description(layout_, font_desc_);
}

PaintContext::~PaintContext() {
    if (layout_)
        g_object_unref(layout_);
    if (font_desc_)
        pango_font_description_free(font_desc_);
}

int PaintContext::width() const {
    return window_.width();
}

int PaintContext::height() const {
    return window_.height();
}

void PaintContext::clear(const std::string& bg) {
    if (!cr_)
        return;
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    parse_hex_color(bg, r, g, b);
    cairo_set_source_rgb(cr_, r, g, b);
    cairo_rectangle(cr_, 0, 0, width(), height());
    cairo_fill(cr_);
}

int PaintContext::text_width(const std::string& text) {
    if (!layout_)
        return 0;
    pango_layout_set_text(layout_, text.c_str(), (int)text.size());
    int tw = 0;
    int th = 0;
    pango_layout_get_pixel_size(layout_, &tw, &th);
    (void)th;
    return tw;
}

int PaintContext::draw_text(int x,
    const std::string& text,
    const std::string& fg,
    const std::string& bg,
    int pad) {
    if (!cr_ || !layout_)
        return 0;

    pango_layout_set_text(layout_, text.c_str(), (int)text.size());
    int    tw      = 0;
    int    th      = 0;
    pango_layout_get_pixel_size(layout_, &tw, &th);
    int    total_w = tw + pad * 2;

    double br      = 0.0;
    double bgc     = 0.0;
    double bb      = 0.0;
    parse_hex_color(bg, br, bgc, bb);
    cairo_set_source_rgb(cr_, br, bgc, bb);
    cairo_rectangle(cr_, x, 0, total_w, height());
    cairo_fill(cr_);

    int    ty  = (height() - th) / 2;
    double fr  = 0.0;
    double fgc = 0.0;
    double fb  = 0.0;
    parse_hex_color(fg, fr, fgc, fb);
    cairo_set_source_rgb(cr_, fr, fgc, fb);
    cairo_move_to(cr_, x + pad, ty);
    pango_cairo_show_layout(cr_, layout_);
    return total_w;
}

void PaintContext::draw_rect(int x, int y, int w, int h, const std::string& color) {
    if (!cr_)
        return;
    double r = 0.0, g = 0.0, b = 0.0;
    parse_hex_color(color, r, g, b);
    cairo_set_source_rgb(cr_, r, g, b);
    cairo_rectangle(cr_, x, y, w, h);
    cairo_fill(cr_);
}

void PaintContext::present() {
    window_.present();
}

std::vector<TagHit> TagsWidget::draw(PaintContext& paint,
    const BarState& state,
    const BarConfig& cfg,
    int& cursor_x) const {
    std::vector<TagHit> hits;
    for (const auto& tag : state.tags) {
        const std::string& fg = tag.focused ? cfg.colors.focused_fg : cfg.colors.normal_fg;
        const std::string& bg = tag.focused ? cfg.colors.focused_bg : cfg.colors.bar_bg;
        int                tw = paint.draw_text(cursor_x, tag.name, fg, bg);
        if (tag.has_windows) {
            constexpr int sq = 4;
            paint.draw_rect(cursor_x + 2, 2, sq, sq, fg);
        }
        hits.push_back({ cursor_x, cursor_x + tw, tag.id });
        cursor_x += tw;
    }
    return hits;
}

void TitleWidget::draw_center(PaintContext& paint,
    const BarState& state,
    const BarConfig& cfg,
    int left_edge,
    int right_edge) const {
    if (state.title.empty())
        return;
    int tw        = paint.text_width(state.title) + 16;
    int available = right_edge - left_edge;
    int cx        = left_edge + (available - tw) / 2;
    cx = std::max(left_edge, std::min(cx, right_edge - tw));
    if (cx + tw <= right_edge)
        paint.draw_text(cx, state.title, cfg.colors.normal_fg, cfg.colors.bar_bg);
}

int TrayWidget::reserved_width(const backend::TrayHost* tray) const {
    return tray ? tray->width() : 0;
}

void TrayWidget::reposition(backend::TrayHost* tray, const backend::RenderWindow& bar) const {
    if (!tray)
        return;
    tray->reposition(bar.id(), bar.x() + bar.width(), bar.y());
}

} // namespace bar::widgets