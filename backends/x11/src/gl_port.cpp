#include <x11_ports.hpp>

#include <xconn.hpp>
#include <backend/gl_port.hpp>
#include <log.hpp>

#include <EGL/egl.h>
#include <GL/gl.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// X11GLWindow — EGL-backed implementation of backend::GLWindow.
// ---------------------------------------------------------------------------
class X11GLWindow final : public backend::GLWindow {
    Display*   dpy_       = nullptr;
    Window     win_       = 0;
    EGLDisplay egl_dpy_   = EGL_NO_DISPLAY;
    EGLContext egl_ctx_   = EGL_NO_CONTEXT;
    EGLSurface egl_surf_  = EGL_NO_SURFACE;
    Colormap   cmap_      = 0;
    Atom       wm_delete_ = 0;
    bool       visible_   = false;
    int        width_     = 0;
    int        height_    = 0;
    bool       closed_    = false;

    public:
        X11GLWindow(const backend::GLWindowCreateInfo& info)
            : width_(std::max(1, info.size.x()))
              , height_(std::max(1, info.size.y()))
        {
            try {
                // Suppress Mesa background threads (glthread, llvmpipe workers).
                setenv("mesa_glthread", "false", 0);
                setenv("LP_NUM_THREADS", "0", 0);

                dpy_ = XOpenDisplay(nullptr);
                if (!dpy_)
                    throw std::runtime_error("X11GLWindow: failed to open X display");

                egl_dpy_ = eglGetDisplay(dpy_);
                if (egl_dpy_ == EGL_NO_DISPLAY)
                    throw std::runtime_error("X11GLWindow: eglGetDisplay failed");

                EGLint major, minor;
                if (!eglInitialize(egl_dpy_, &major, &minor))
                    throw std::runtime_error("X11GLWindow: eglInitialize failed");

                // clang-format off
                static const EGLint config_attribs[] = {
                    EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
                    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                    EGL_RED_SIZE,        8,
                    EGL_GREEN_SIZE,      8,
                    EGL_BLUE_SIZE,       8,
                    EGL_ALPHA_SIZE,      8,
                    EGL_DEPTH_SIZE,      24,
                    EGL_NONE
                };
                // clang-format on

                EGLConfig egl_config;
                EGLint    num_configs;
                if (!eglChooseConfig(egl_dpy_, config_attribs, &egl_config, 1, &num_configs) ||
                    num_configs == 0)
                    throw std::runtime_error("X11GLWindow: no suitable EGL config");

                EGLint visual_id;
                eglGetConfigAttrib(egl_dpy_, egl_config, EGL_NATIVE_VISUAL_ID, &visual_id);

                int          screen = DefaultScreen(dpy_);

                XVisualInfo  vi_template {};
                vi_template.visualid = visual_id;
                int          n_vi = 0;
                XVisualInfo* vi   = XGetVisualInfo(dpy_, VisualIDMask, &vi_template, &n_vi);
                if (!vi || n_vi == 0)
                    throw std::runtime_error("X11GLWindow: cannot find X visual for EGL config");

                cmap_ = XCreateColormap(dpy_, RootWindow(dpy_, screen), vi->visual, AllocNone);

                XSetWindowAttributes swa {};
                swa.colormap   = cmap_;
                swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask
                    | ButtonPressMask | ButtonReleaseMask
                    | PointerMotionMask | StructureNotifyMask;
                swa.override_redirect = info.override_redirect ? True : False;

                unsigned long xmask = CWColormap | CWEventMask | CWOverrideRedirect;

                win_ = XCreateWindow(dpy_, RootWindow(dpy_, screen),
                        0, 0, width_, height_, 0,
                        vi->depth, InputOutput, vi->visual,
                        xmask, &swa);
                XFree(vi);

                if (!win_)
                    throw std::runtime_error("X11GLWindow: failed to create X window");

                XStoreName(dpy_, win_, "SirenWM Debug");
                XClassHint hint = {};
                hint.res_name  = const_cast<char*>("sirenwm_debug");
                hint.res_class = const_cast<char*>("SirenwmDebug");
                XSetClassHint(dpy_, win_, &hint);

                wm_delete_ = XInternAtom(dpy_, "WM_DELETE_WINDOW", False);
                XSetWMProtocols(dpy_, win_, &wm_delete_, 1);

                Atom net_wm_window_type = XInternAtom(dpy_, "_NET_WM_WINDOW_TYPE", False);
                Atom type_utility       = XInternAtom(dpy_, "_NET_WM_WINDOW_TYPE_UTILITY", False);
                XChangeProperty(dpy_, win_, net_wm_window_type, XA_ATOM, 32,
                    PropModeReplace, (unsigned char*)&type_utility, 1);

                if (info.keep_above && !info.override_redirect) {
                    Atom net_wm_state       = XInternAtom(dpy_, "_NET_WM_STATE", False);
                    Atom net_wm_state_above = XInternAtom(dpy_, "_NET_WM_STATE_ABOVE", False);
                    XChangeProperty(dpy_, win_, net_wm_state, XA_ATOM, 32,
                        PropModeReplace, (unsigned char*)&net_wm_state_above, 1);
                }

                egl_surf_ = eglCreateWindowSurface(egl_dpy_, egl_config, win_, nullptr);
                if (egl_surf_ == EGL_NO_SURFACE)
                    throw std::runtime_error("X11GLWindow: eglCreateWindowSurface failed");

                eglBindAPI(EGL_OPENGL_API);

                static const EGLint ctx_attribs[] = { EGL_NONE };
                egl_ctx_ = eglCreateContext(egl_dpy_, egl_config, EGL_NO_CONTEXT, ctx_attribs);
                if (egl_ctx_ == EGL_NO_CONTEXT)
                    throw std::runtime_error("X11GLWindow: eglCreateContext failed");

                eglMakeCurrent(egl_dpy_, egl_surf_, egl_surf_, egl_ctx_);
                eglSwapInterval(egl_dpy_, 0);
                eglMakeCurrent(egl_dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            } catch (...) {
                cleanup();
                throw;
            }
        }

        ~X11GLWindow() override { cleanup(); }

    private:
        void cleanup() noexcept {
            if (egl_dpy_ != EGL_NO_DISPLAY) {
                eglMakeCurrent(egl_dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                if (egl_ctx_ != EGL_NO_CONTEXT)
                    eglDestroyContext(egl_dpy_, egl_ctx_);
                if (egl_surf_ != EGL_NO_SURFACE)
                    eglDestroySurface(egl_dpy_, egl_surf_);
                eglTerminate(egl_dpy_);
                egl_dpy_ = EGL_NO_DISPLAY;
            }
            if (win_ && dpy_) {
                XDestroyWindow(dpy_, win_); win_ = 0;
            }
            if (cmap_ && dpy_) {
                XFreeColormap(dpy_, cmap_); cmap_ = 0;
            }
            if (dpy_) {
                XSync(dpy_, False);
                XCloseDisplay(dpy_);
                dpy_ = nullptr;
            }
        }

    public:

        X11GLWindow(const X11GLWindow&)            = delete;
        X11GLWindow& operator=(const X11GLWindow&) = delete;

        int fd() const override {
            return ConnectionNumber(dpy_);
        }

        std::vector<backend::GLInputEvent> poll_events() override {
            std::vector<backend::GLInputEvent> out;

            while (XPending(dpy_)) {
                XEvent xev {};
                XNextEvent(dpy_, &xev);

                backend::GLInputEvent ev {};

                switch (xev.type) {
                    case MotionNotify:
                        ev.type = backend::GLInputEvent::MouseMove;
                        ev.pos  = { xev.xmotion.x, xev.xmotion.y };
                        out.push_back(ev);
                        break;

                    case ButtonPress:
                    case ButtonRelease: {
                        bool pressed = (xev.type == ButtonPress);
                        if (xev.xbutton.button == 4 || xev.xbutton.button == 5) {
                            if (pressed) {
                                ev.type   = backend::GLInputEvent::Scroll;
                                ev.pos    = { xev.xbutton.x, xev.xbutton.y };
                                ev.scroll = (xev.xbutton.button == 4) ? 1.0f : -1.0f;
                                out.push_back(ev);
                            }
                        } else {
                            ev.type    = backend::GLInputEvent::MouseButton;
                            ev.pos     = { xev.xbutton.x, xev.xbutton.y };
                            ev.pressed = pressed;
                            switch (xev.xbutton.button) {
                                case 1: ev.button  = 0; break;
                                case 2: ev.button  = 2; break;
                                case 3: ev.button  = 1; break;
                                default: ev.button = xev.xbutton.button - 1; break;
                            }
                            out.push_back(ev);
                        }
                        break;
                    }

                    case KeyPress:
                    case KeyRelease: {
                        ev.type    = backend::GLInputEvent::Key;
                        ev.pressed = (xev.type == KeyPress);
                        ev.key     = XLookupKeysym(&xev.xkey, 0);
                        out.push_back(ev);

                        if (ev.pressed) {
                            char   buf[8] = {};
                            KeySym ks;
                            int    len = XLookupString(&xev.xkey, buf, sizeof(buf), &ks, nullptr);
                            if (len > 0 && (unsigned char)buf[0] >= 32) {
                                backend::GLInputEvent cev {};
                                cev.type = backend::GLInputEvent::Char;
                                cev.ch   = (unsigned char)buf[0];
                                out.push_back(cev);
                            }
                        }
                        break;
                    }

                    case ConfigureNotify:
                        if (xev.xconfigure.width != width_ || xev.xconfigure.height != height_) {
                            width_    = xev.xconfigure.width;
                            height_   = xev.xconfigure.height;
                            ev.type   = backend::GLInputEvent::Resize;
                            ev.resize = { width_, height_ };
                            out.push_back(ev);
                        }
                        break;

                    case ClientMessage:
                        if (static_cast<Atom>(xev.xclient.data.l[0]) == wm_delete_) {
                            ev.type = backend::GLInputEvent::Close;
                            out.push_back(ev);
                            closed_ = true;
                        }
                        break;

                    default:
                        break;
                }
            }

            return out;
        }

        void make_current() override {
            eglMakeCurrent(egl_dpy_, egl_surf_, egl_surf_, egl_ctx_);
        }

        void swap_buffers() override {
            eglSwapBuffers(egl_dpy_, egl_surf_);
        }

        void show() override {
            if (!visible_) {
                XMapWindow(dpy_, win_);
                XFlush(dpy_);
                visible_ = true;
                closed_  = false;
            }
        }

        void hide() override {
            if (visible_) {
                XUnmapWindow(dpy_, win_);
                XFlush(dpy_);
                visible_ = false;
            }
        }

        bool visible() const override { return visible_ && !closed_; }
        int  width()  const override { return width_; }
        int  height() const override { return height_; }
};

// ---------------------------------------------------------------------------
// X11GLPort — factory for X11GLWindow instances.
// ---------------------------------------------------------------------------
class X11GLPort final : public backend::GLPort {
    public:
        std::unique_ptr<backend::GLWindow>
        create_window(const backend::GLWindowCreateInfo& info) override {
            return std::make_unique<X11GLWindow>(info);
        }
};

} // namespace

namespace backend::x11 {

std::unique_ptr<backend::GLPort> create_gl_port() {
    return std::make_unique<X11GLPort>();
}

} // namespace backend::x11
