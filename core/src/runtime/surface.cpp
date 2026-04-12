#include <surface.hpp>

#include <backend/render_port.hpp>
#include <runtime.hpp>

Surface::Surface(Runtime& runtime, std::unique_ptr<backend::RenderWindow> window)
    : runtime_(runtime), window_(std::move(window)) {}

Surface::~Surface() {
    runtime_.unregister_surface(this);
}

WindowId Surface::id() const        { return window_->id(); }
int Surface::monitor_index() const { return window_->monitor_index(); }
int Surface::x() const              { return window_->x(); }
int Surface::y() const              { return window_->y(); }
int Surface::width() const          { return window_->width(); }
int Surface::height() const         { return window_->height(); }

cairo_t* Surface::cairo()  { return window_->cairo(); }
void     Surface::present() { window_->present(); }

void Surface::raise()                  { window_->raise(); }
void Surface::lower()                  { window_->lower(); }
void Surface::set_visible(bool visible) { window_->set_visible(visible); }
