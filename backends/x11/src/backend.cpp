#include <backend/backend.hpp>
#include <x11_backend.hpp>

std::unique_ptr<Backend> create_backend(Core& core, Runtime& runtime) {
    return std::make_unique<X11Backend>(core, runtime);
}