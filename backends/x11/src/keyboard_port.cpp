// Include Xlib before everything else — must match xconn.cpp convention.
// <cstdio> must precede XKBrules.h which uses FILE without including stdio.
#include <cstdio>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include <memory>

#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XKBrules.h>

#include <backend/keyboard_port.hpp>
#include <xconn.hpp>
#include <log.hpp>

namespace backend::x11 {

namespace {

// Parse comma-separated layouts string from XkbRF_GetNamesProp.
static std::vector<std::string> split_layouts(const char* s) {
    std::vector<std::string> out;
    if (!s || !*s)
        return out;
    std::istringstream ss(s);
    std::string        token;
    while (std::getline(ss, token, ','))
        out.push_back(token);
    return out;
}

struct XkbSnapshot {
    std::string              rules;
    std::string              model;
    std::vector<std::string> layouts;
    std::string              variant;
    std::string              options;
};

static std::optional<XkbSnapshot> read_snapshot(Display* dpy) {
    XkbRF_VarDefsRec vd  = {};
    char*            rules_file = nullptr;
    if (!XkbRF_GetNamesProp(dpy, &rules_file, &vd))
        return std::nullopt;

    XkbSnapshot snap;
    if (rules_file) { snap.rules   = rules_file; XFree(rules_file); }
    if (vd.model)   { snap.model   = vd.model;   XFree(vd.model);   }
    if (vd.options) { snap.options = vd.options;  XFree(vd.options); }
    if (vd.variant) { snap.variant = vd.variant;  XFree(vd.variant); }
    snap.layouts = split_layouts(vd.layout);
    if (vd.layout)  XFree(vd.layout);
    return snap;
}

static void apply_snapshot(Display* dpy, const XkbSnapshot& snap) {
    std::string layouts_str;
    for (size_t i = 0; i < snap.layouts.size(); ++i) {
        if (i) layouts_str += ',';
        layouts_str += snap.layouts[i];
    }

    XkbRF_VarDefsRec vd = {};
    vd.model   = snap.model.empty()    ? nullptr : const_cast<char*>(snap.model.c_str());
    vd.layout  = layouts_str.empty()   ? nullptr : const_cast<char*>(layouts_str.c_str());
    vd.variant = snap.variant.empty()  ? nullptr : const_cast<char*>(snap.variant.c_str());
    vd.options = snap.options.empty()  ? nullptr : const_cast<char*>(snap.options.c_str());

    // Resolve rules file path — look in standard locations.
    const char* rules_name = snap.rules.empty() ? "evdev" : snap.rules.c_str();
    std::string rules_path = std::string("/usr/share/X11/xkb/rules/") + rules_name;

    XkbRF_RulesRec* rules = XkbRF_Load(const_cast<char*>(rules_path.c_str()), (char*)"C", True, True);
    if (!rules) {
        // Try without locale suffix.
        rules = XkbRF_Load(const_cast<char*>(rules_path.c_str()), nullptr, True, True);
    }
    if (!rules) {
        LOG_WARN("KeyboardPort: failed to load XKB rules from '%s'", rules_path.c_str());
        return;
    }

    XkbComponentNamesRec comp = {};
    if (!XkbRF_GetComponents(rules, &vd, &comp)) {
        XkbRF_Free(rules, True);
        LOG_WARN("KeyboardPort: XkbRF_GetComponents failed");
        return;
    }

    XkbDescPtr desc = XkbGetKeyboardByName(dpy, XkbUseCoreKbd, &comp,
        XkbGBN_AllComponentsMask, XkbGBN_AllComponentsMask, True);

    // Free component name strings allocated by XkbRF_GetComponents.
    if (comp.keymap)   XFree(comp.keymap);
    if (comp.keycodes) XFree(comp.keycodes);
    if (comp.types)    XFree(comp.types);
    if (comp.compat)   XFree(comp.compat);
    if (comp.symbols)  XFree(comp.symbols);
    if (comp.geometry) XFree(comp.geometry);

    XkbRF_Free(rules, True);

    if (!desc) {
        LOG_WARN("KeyboardPort: XkbGetKeyboardByName failed");
        return;
    }

    XkbFreeKeyboard(desc, XkbAllComponentsMask, True);

    // Update the root window property so tools like setxkbmap -query report correctly.
    char rules_buf[256] = {};
    strncpy(rules_buf, rules_name, sizeof(rules_buf) - 1);
    XkbRF_SetNamesProp(dpy, rules_buf, &vd);

    XFlush(dpy);
}

} // namespace

class X11KeyboardPort final : public KeyboardPort {
    Display*                  dpy_;
    std::optional<XkbSnapshot> saved_;

    public:
        explicit X11KeyboardPort(Display* dpy) : dpy_(dpy) {}

        std::string current_layout() const override {
            if (!dpy_)
                return {};
            XkbStateRec state = {};
            if (XkbGetState(dpy_, XkbUseCoreKbd, &state) != Success)
                return {};
            auto snap = read_snapshot(dpy_);
            if (!snap || snap->layouts.empty())
                return {};
            int group = (int)state.group;
            if (group < 0 || group >= (int)snap->layouts.size())
                return snap->layouts[0];
            return snap->layouts[group];
        }

        std::vector<std::string> layout_names() const override {
            if (!dpy_)
                return {};
            auto snap = read_snapshot(dpy_);
            if (!snap)
                return {};
            return snap->layouts;
        }

        void apply(const std::vector<std::string>& layouts,
            const std::string& options) override {
            if (!dpy_ || layouts.empty())
                return;
            // Save current state before first apply.
            if (!saved_)
                saved_ = read_snapshot(dpy_);

            XkbSnapshot next;
            if (saved_) {
                next.rules   = saved_->rules;
                next.model   = saved_->model;
                next.variant = saved_->variant;
            } else {
                next.rules = "evdev";
                next.model = "pc105";
            }
            next.layouts = layouts;
            next.options = options;
            apply_snapshot(dpy_, next);
        }

        void restore() override {
            if (!dpy_ || !saved_)
                return;
            apply_snapshot(dpy_, *saved_);
            saved_.reset();
        }
};

std::unique_ptr<KeyboardPort> create_keyboard_port(XConnection& xconn) {
    return std::make_unique<X11KeyboardPort>(xconn.xlib_display());
}

} // namespace backend::x11
