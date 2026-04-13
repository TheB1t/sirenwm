#!/usr/bin/env python3

import argparse
import xml.etree.ElementTree as ET
from pathlib import Path
from dataclasses import dataclass


@dataclass
class Arg:
    name: str
    arg_type: str
    interface: str | None


@dataclass
class Message:
    name: str
    args: list[Arg]


@dataclass
class Interface:
    name: str
    version: int
    requests: list[Message]
    events: list[Message]


def snake_to_camel(name: str) -> str:
    return "".join(part.capitalize() for part in name.split("_") if part)


def safe_name(name: str) -> str:
    reserved = {
        "class", "template", "default", "operator", "new", "delete", "this",
        "namespace", "typename", "public", "private", "protected",
    }
    if not name or name in reserved:
        return f"{name}_"
    return name


def parse_interfaces(xml_path: str) -> list[Interface]:
    root = ET.parse(xml_path).getroot()
    out: list[Interface] = []
    for iface in root.findall("interface"):
        name = iface.attrib.get("name")
        if not name:
            continue
        version = int(iface.attrib.get("version", "1"))

        requests: list[Message] = []
        for req in iface.findall("request"):
            rname = req.attrib.get("name")
            if not rname:
                continue
            args: list[Arg] = []
            for arg in req.findall("arg"):
                aname = arg.attrib.get("name", "arg")
                atype = arg.attrib.get("type", "")
                aiface = arg.attrib.get("interface")
                args.append(Arg(aname, atype, aiface))
            requests.append(Message(rname, args))

        events: list[Message] = []
        for ev in iface.findall("event"):
            ename = ev.attrib.get("name")
            if not ename:
                continue
            args: list[Arg] = []
            for arg in ev.findall("arg"):
                aname = arg.attrib.get("name", "arg")
                atype = arg.attrib.get("type", "")
                aiface = arg.attrib.get("interface")
                args.append(Arg(aname, atype, aiface))
            events.append(Message(ename, args))

        out.append(Interface(name, version, requests, events))
    return out


def cpp_type(arg: Arg) -> str:
    t = arg.arg_type
    if t == "int":
        return "int32_t"
    if t == "uint":
        return "uint32_t"
    if t == "fixed":
        return "wl_fixed_t"
    if t == "string":
        return "const char*"
    if t == "array":
        return "wl_array*"
    if t == "fd":
        return "int32_t"
    if t in {"object", "new_id"}:
        if arg.interface:
            return f"{arg.interface}*"
        return "wl_proxy*"
    return "void*"


def message_params(msg: Message, include_new_id: bool = False) -> list[Arg]:
    if include_new_id:
        return list(msg.args)
    return [a for a in msg.args if a.arg_type != "new_id"]


def message_return_type(msg: Message) -> str:
    for arg in msg.args:
        if arg.arg_type == "new_id":
            if arg.interface:
                return f"{arg.interface}*"
            return "wl_proxy*"
    return "void"


def render_decl_params(args: list[Arg], with_names: bool) -> str:
    if not args:
        return ""
    parts: list[str] = []
    for arg in args:
        t = cpp_type(arg)
        if with_names:
            parts.append(f"{t} {safe_name(arg.name)}")
        else:
            parts.append(t)
    return ", ".join(parts)


def render_call_args(args: list[Arg]) -> str:
    return ", ".join(safe_name(a.name) for a in args)


def render_client(base: str, namespace: str, interfaces: list[Interface]) -> str:
    lines = [
        "#pragma once",
        "",
        "extern \"C\" {",
        f"#include \"{base}-client-protocol.h\"",
        "}",
        "",
        "#include <cstdint>",
        "#include <wl/protocol.hpp>",
        "",
        f"namespace {namespace} {{",
        "",
    ]

    for iface in interfaces:
        cls = snake_to_camel(iface.name) + "Client"
        lines.extend(
            [
                f"class {cls} final : public wl::Protocol<{iface.name}> {{",
                "public:",
                f"    using wl::Protocol<{iface.name}>::Protocol;",
                f"    static constexpr const char* interface_name() noexcept {{ return \"{iface.name}\"; }}",
                f"    static constexpr std::uint32_t interface_version() noexcept {{ return {iface.version}u; }}",
                "};",
                "",
            ]
        )

    lines.extend([f"}} // namespace {namespace}", ""])
    return "\n".join(lines)


def render_server(base: str, namespace: str, interfaces: list[Interface]) -> str:
    lines = [
        "#pragma once",
        "",
        "extern \"C\" {",
        f"#include \"{base}-protocol.h\"",
        "}",
        "",
        "#include <cstdint>",
        "#include <wl/protocol.hpp>",
        "",
        f"namespace {namespace} {{",
        "",
    ]

    for iface in interfaces:
        cls = snake_to_camel(iface.name) + "Server"
        lines.extend(
            [
                f"class {cls} final : public wl::Protocol<wl_resource> {{",
                "public:",
                "    using wl::Protocol<wl_resource>::Protocol;",
                f"    static constexpr const char* interface_name() noexcept {{ return \"{iface.name}\"; }}",
                f"    static constexpr std::uint32_t interface_version() noexcept {{ return {iface.version}u; }}",
                f"    static constexpr const wl_interface* interface_desc() noexcept {{ return &{iface.name}_interface; }}",
                "};",
                "",
            ]
        )

    lines.extend([f"}} // namespace {namespace}", ""])
    return "\n".join(lines)


def render_client_api(base: str, interfaces: list[Interface]) -> str:
    lines = [
        "#pragma once",
        "",
        "extern \"C\" {",
        f"#include \"{base}-client-protocol.h\"",
        "}",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "#include <cstring>",
        "#include <utility>",
        "#include <wl/proxy.hpp>",
        "",
        "namespace wlproto {",
        "",
    ]

    for iface in interfaces:
        cls = snake_to_camel(iface.name) + "ClientApi"
        listener_t = f"{iface.name}_listener"
        lines.extend(
            [
                "template<typename Derived>",
                f"class {cls} {{",
                "public:",
                f"    void attach(wl::Proxy<{iface.name}>&& proxy) {{",
                "        proxy_ = std::move(proxy);",
                "        if (proxy_)",
                f"            {iface.name}_add_listener(proxy_.raw(), &listener(), static_cast<Derived*>(this));",
                "    }",
                "",
                "    explicit operator bool() const noexcept { return static_cast<bool>(proxy_); }",
                "",
            ]
        )

        for req in iface.requests:
            req_args = message_params(req)
            decl = render_decl_params(req_args, with_names=True)
            call = render_call_args(req_args)
            ret = message_return_type(req)
            if decl:
                sig = f"{ret} {req.name}({decl})"
            else:
                sig = f"{ret} {req.name}()"

            lines.append(f"    {sig} {{")
            lines.append("        if (!proxy_) {")
            if ret != "void":
                lines.append("            return nullptr;")
            else:
                lines.append("            return;")
            lines.append("        }")

            call_expr = f"{iface.name}_{req.name}(proxy_.raw()"
            if call:
                call_expr += f", {call}"
            call_expr += ")"

            if ret == "void":
                lines.append(f"        {call_expr};")
                lines.append("    }")
            else:
                lines.append(f"        return {call_expr};")
                lines.append("    }")
            lines.append("")

            # Convenience overload for single array payload.
            if len(req.args) == 1 and req.args[0].arg_type == "array":
                arr_name = safe_name(req.args[0].name)
                lines.extend(
                    [
                        f"    void {req.name}(const void* data, std::size_t size) {{",
                        "        if (!proxy_) return;",
                        "        wl_array arr;",
                        "        wl_array_init(&arr);",
                        "        if (size > 0) {",
                        "            void* p = wl_array_add(&arr, size);",
                        "            if (p) std::memcpy(p, data, size);",
                        "        }",
                        f"        {iface.name}_{req.name}(proxy_.raw(), &arr);",
                        "        wl_array_release(&arr);",
                        "    }",
                        "",
                    ]
                )

        lines.extend(
            [
                "    void reset() { proxy_.reset(); }",
                "",
                "    template<typename RegistryT>",
                "    bool try_bind(RegistryT& registry, uint32_t name, const char* iface_name, uint32_t version) {",
                f"        if (!RegistryT::match(iface_name, {iface.name}_interface))",
                "            return false;",
                "        const uint32_t bind_version =",
                f"            version < {iface.version}u ? version : {iface.version}u;",
                f"        attach(registry.template bind<{iface.name}>(name, {iface.name}_interface, bind_version));",
                "        return true;",
                "    }",
                "",
            ]
        )

        for ev in iface.events:
            ev_args = message_params(ev, include_new_id=True)
            decl = render_decl_params(ev_args, with_names=False)
            if decl:
                lines.append(f"    void on_{ev.name}({decl}) {{}}")
            else:
                lines.append(f"    void on_{ev.name}() {{}}")
        lines.extend(
            [
                "",
                "protected:",
                f"    wl::Proxy<{iface.name}> proxy_;",
                "",
                "private:",
                f"    static const {listener_t}& listener() {{",
                f"        static const {listener_t} l = {{",
            ]
        )

        for ev in iface.events:
            ev_args = message_params(ev, include_new_id=True)
            lambda_params: list[str] = [f"{iface.name}*"]
            for a in ev_args:
                lambda_params.append(f"{cpp_type(a)} {safe_name(a.name)}")
            lambda_sig = ", ".join(lambda_params)
            call_args = render_call_args(ev_args)
            if call_args:
                call_args = f"({call_args})"
            else:
                call_args = "()"
            lines.extend(
                [
                    f"            .{ev.name} = [](void* d, {lambda_sig}) {{",
                    f"                static_cast<Derived*>(d)->on_{ev.name}{call_args};",
                    "            },",
                ]
            )

        lines.extend(
            [
                "        };",
                "        return l;",
                "    }",
                "};",
                "",
            ]
        )

    lines.extend(["} // namespace wlproto", ""])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate thin C++ wrappers for Wayland protocol interfaces")
    parser.add_argument("--xml", required=True)
    parser.add_argument("--out-client", required=True)
    parser.add_argument("--out-server", required=True)
    parser.add_argument("--out-client-api", required=True)
    parser.add_argument("--namespace", default="wlproto")
    parser.add_argument("--base", required=True)
    args = parser.parse_args()

    interfaces = parse_interfaces(args.xml)

    out_client = Path(args.out_client)
    out_server = Path(args.out_server)
    out_client_api = Path(args.out_client_api)
    out_client.parent.mkdir(parents=True, exist_ok=True)
    out_server.parent.mkdir(parents=True, exist_ok=True)
    out_client_api.parent.mkdir(parents=True, exist_ok=True)

    out_client.write_text(render_client(args.base, args.namespace, interfaces), encoding="utf-8")
    out_server.write_text(render_server(args.base, args.namespace, interfaces), encoding="utf-8")
    out_client_api.write_text(render_client_api(args.base, interfaces), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
