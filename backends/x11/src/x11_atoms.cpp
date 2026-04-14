#include <x11_atoms.hpp>
#include <xconn.hpp>

const WindowTypeAtoms& window_type_atoms(XConnection& xconn) {
    static const WindowTypeAtoms atoms = [&xconn]() {
            auto m = xconn.intern_atoms({
            "_NET_WM_WINDOW_TYPE",
            "_NET_WM_WINDOW_TYPE_DIALOG",
            "_NET_WM_WINDOW_TYPE_UTILITY",
            "_NET_WM_WINDOW_TYPE_SPLASH",
            "_NET_WM_WINDOW_TYPE_MODAL",
        });
            WindowTypeAtoms out;
            out.net_wm_window_type = m["_NET_WM_WINDOW_TYPE"];
            out.dialog             = m["_NET_WM_WINDOW_TYPE_DIALOG"];
            out.utility            = m["_NET_WM_WINDOW_TYPE_UTILITY"];
            out.splash             = m["_NET_WM_WINDOW_TYPE_SPLASH"];
            out.modal              = m["_NET_WM_WINDOW_TYPE_MODAL"];
            return out;
        }();
    return atoms;
}
