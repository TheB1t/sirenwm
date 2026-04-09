#include "sysinfo_module.hpp"

#include <backend/backend.hpp>
#include <backend/keyboard_port.hpp>
#include <config.hpp>
#include <log.hpp>
#include <module_registry.hpp>

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <sys/statvfs.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Persistent file descriptors — opened once, re-read via lseek+read.
// ---------------------------------------------------------------------------

static int g_stat_fd    = -1;
static int g_meminfo_fd = -1;

static void open_fds() {
    if (g_stat_fd < 0)
        g_stat_fd = open("/proc/stat", O_RDONLY | O_CLOEXEC);
    if (g_meminfo_fd < 0)
        g_meminfo_fd = open("/proc/meminfo", O_RDONLY | O_CLOEXEC);
}

static void close_fds() {
    if (g_stat_fd >= 0) {
        close(g_stat_fd);    g_stat_fd = -1;
    }
    if (g_meminfo_fd >= 0) {
        close(g_meminfo_fd); g_meminfo_fd = -1;
    }
}

// Read fd from offset 0 into buf. Returns bytes read, or -1.
static ssize_t read_fd(int fd, char* buf, size_t size) {
    if (fd < 0) return -1;
    if (lseek(fd, 0, SEEK_SET) < 0) return -1;
    ssize_t n = read(fd, buf, size - 1);
    if (n > 0) buf[n] = '\0';
    return n;
}

// ---------------------------------------------------------------------------
// CPU — delta between two reads of /proc/stat
// ---------------------------------------------------------------------------

struct CpuSnapshot {
    long long idle  = 0;
    long long total = 0;
};

static CpuSnapshot g_cpu_prev;

static double cpu_read_percent() {
    char buf[256];
    if (read_fd(g_stat_fd, buf, sizeof(buf)) <= 0)
        return 0.0;

    long long u = 0, n = 0, s = 0, id = 0, iw = 0, ir = 0, si = 0, st = 0;
    if (sscanf(buf, "cpu %lld %lld %lld %lld %lld %lld %lld %lld",
        &u, &n, &s, &id, &iw, &ir, &si, &st) < 4)
        return 0.0;

    long long idle  = id + iw;
    long long total = u + n + s + id + iw + ir + si + st;
    long long dt    = total - g_cpu_prev.total;
    long long di    = idle  - g_cpu_prev.idle;
    g_cpu_prev = { idle, total };

    return (dt > 0) ? (double)(dt - di) / dt * 100.0 : 0.0;
}

// ---------------------------------------------------------------------------
// Memory — parse /proc/meminfo
// ---------------------------------------------------------------------------

struct MemInfo {
    double used_gb  = 0.0;
    double total_gb = 0.0;
    double percent  = 0.0;
};

static MemInfo mem_read() {
    char buf[4096];
    if (read_fd(g_meminfo_fd, buf, sizeof(buf)) <= 0)
        return {};

    long long mem_total = 0, mem_free = 0, buffers = 0, cached = 0;
    char*     p = buf;
    while (*p) {
        char* nl = strchr(p, '\n');
        if (nl) *nl = '\0';

        long long v = 0;
        if      (sscanf(p, "MemTotal: %lld",  &v) == 1) mem_total = v;
        else if (sscanf(p, "MemFree: %lld",   &v) == 1) mem_free = v;
        else if (sscanf(p, "Buffers: %lld",   &v) == 1) buffers = v;
        else if (sscanf(p, "Cached: %lld",    &v) == 1) cached = v;

        if (!nl) break;
        p = nl + 1;
    }

    constexpr double KB_TO_GB = 1048576.0;
    MemInfo          m;
    m.total_gb = mem_total / KB_TO_GB;
    m.used_gb  = (mem_total - mem_free - buffers - cached) / KB_TO_GB;
    m.percent  = (m.total_gb > 0) ? m.used_gb / m.total_gb * 100.0 : 0.0;
    return m;
}

// ---------------------------------------------------------------------------
// Network — default route interface IP via /proc/net/route + getifaddrs
// ---------------------------------------------------------------------------

static std::string net_default_ip() {
    FILE* f = fopen("/proc/net/route", "r");
    if (!f) return "";

    char         iface[32];
    unsigned int dest, gw, flags;
    char         default_iface[32] = {};

    char         line[256];
    fgets(line, sizeof(line), f); // skip header
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%31s %x %x %x", iface, &dest, &gw, &flags) == 4) {
            if (dest == 0 && (flags & 0x3) == 0x3) { // RTF_UP | RTF_GATEWAY
                strncpy(default_iface, iface, sizeof(default_iface) - 1);
                break;
            }
        }
    }
    fclose(f);

    if (default_iface[0] == '\0') return "";

    struct ifaddrs* ifa_list = nullptr;
    if (getifaddrs(&ifa_list) != 0) return "";

    std::string result;
    for (struct ifaddrs* ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(ifa->ifa_name, default_iface) != 0) continue;
        char  buf[INET_ADDRSTRLEN];
        auto* sin = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
        if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf)))
            result = buf;
        break;
    }
    freeifaddrs(ifa_list);
    return result;
}

// ---------------------------------------------------------------------------
// Disks — /proc/mounts + statvfs, skip pseudo-filesystems
// ---------------------------------------------------------------------------

static const char* const PSEUDO_FS[] = {
    "proc", "sysfs", "devtmpfs", "devpts", "tmpfs", "cgroup", "cgroup2",
    "pstore", "efivarfs", "bpf", "tracefs", "securityfs", "hugetlbfs",
    "mqueue", "debugfs", "configfs", "fusectl", "overlay", nullptr
};

static bool is_pseudo(const char* fstype) {
    for (int i = 0; PSEUDO_FS[i]; ++i)
        if (strcmp(fstype, PSEUDO_FS[i]) == 0) return true;
    return false;
}

struct DiskInfo {
    std::string device;
    std::string mountpoint;
    uint64_t    total   = 0;
    uint64_t    used    = 0;
    uint64_t    free_   = 0;
    double      percent = 0.0;
};

static std::vector<DiskInfo> disks_read() {
    FILE* f = fopen("/proc/mounts", "r");
    if (!f) return {};

    std::vector<DiskInfo> result;
    char                  dev[256], mnt[256], fstype[64], opts[256];
    int                   dump, pass;
    while (fscanf(f, "%255s %255s %63s %255s %d %d", dev, mnt, fstype, opts, &dump, &pass) == 6) {
        if (is_pseudo(fstype)) continue;
        if (strncmp(dev, "/dev/", 5) != 0) continue;

        struct statvfs st;
        if (statvfs(mnt, &st) != 0) continue;

        DiskInfo d;
        d.device     = dev;
        d.mountpoint = mnt;
        d.total      = (uint64_t)st.f_blocks * st.f_frsize;
        d.free_      = (uint64_t)st.f_bavail * st.f_frsize;
        d.used       = d.total - (uint64_t)st.f_bfree * st.f_frsize;
        d.percent    = (d.total > 0) ? (double)d.used / d.total * 100.0 : 0.0;
        result.push_back(std::move(d));
    }
    fclose(f);
    return result;
}

// ---------------------------------------------------------------------------
// Lua API: siren.sys.*
// ---------------------------------------------------------------------------

static int lua_sys_cpu(LuaContext& lua) {
    lua.push_number(cpu_read_percent());
    return 1;
}

static int lua_sys_mem(LuaContext& lua) {
    MemInfo m = mem_read();
    lua.new_table();
    // stack: table
    lua.push_number(m.used_gb);  lua.set_field(-2, "used");
    lua.push_number(m.total_gb); lua.set_field(-2, "total");
    lua.push_number(m.percent);  lua.set_field(-2, "percent");
    return 1;
}

static int lua_sys_uptime(LuaContext& lua) {
    char buf[64];
    int  fd = open("/proc/uptime", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        lua.push_number(0); return 1;
    }
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    double  up = 0;
    if (n > 0) {
        buf[n] = '\0'; sscanf(buf, "%lf", &up);
    }
    lua.push_number(up);
    return 1;
}

static int lua_sys_net_ip(LuaContext& lua) {
    std::string ip = net_default_ip();
    lua.push_string(ip.empty() ? "N/A" : ip.c_str());
    return 1;
}

static int lua_sys_disks(LuaContext& lua) {
    auto disks = disks_read();
    lua.new_table();
    int  idx = 1;
    for (const auto& d : disks) {
        lua.new_table();
        lua.push_string(d.device.c_str());      lua.set_field(-2, "device");
        lua.push_string(d.mountpoint.c_str());  lua.set_field(-2, "mountpoint");
        lua.push_number((double)d.total);        lua.set_field(-2, "total");
        lua.push_number((double)d.used);         lua.set_field(-2, "used");
        lua.push_number((double)d.free_);        lua.set_field(-2, "free");
        lua.push_number(d.percent);              lua.set_field(-2, "percent");
        lua.push_integer(idx++);
        lua.insert(-2);
        lua.raw_set(-3);
    }
    return 1;
}

static int lua_sys_loadavg(LuaContext& lua) {
    char   buf[64];
    int    fd = open("/proc/loadavg", O_RDONLY | O_CLOEXEC);
    double a1 = 0, a5 = 0, a15 = 0;
    if (fd >= 0) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = '\0'; sscanf(buf, "%lf %lf %lf", &a1, &a5, &a15);
        }
    }
    lua.new_table();
    lua.push_number(a1);  lua.set_field(-2, "1");
    lua.push_number(a5);  lua.set_field(-2, "5");
    lua.push_number(a15); lua.set_field(-2, "15");
    return 1;
}

// ---------------------------------------------------------------------------
// Battery — /sys/class/power_supply/BAT*
// ---------------------------------------------------------------------------

static int g_bat_cap_fd    = -1;
static int g_bat_status_fd = -1;

static void open_battery_fds() {
    if (g_bat_cap_fd >= 0) return; // already open
    for (int i = 0; i < 4; ++i) {
        std::string base = "/sys/class/power_supply/BAT" + std::to_string(i);
        std::string type_path = base + "/type";
        int fd = open(type_path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        char buf[32];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) continue;
        buf[n] = '\0';
        if (strncmp(buf, "Battery", 7) != 0) continue;
        g_bat_cap_fd    = open((base + "/capacity").c_str(), O_RDONLY | O_CLOEXEC);
        g_bat_status_fd = open((base + "/status").c_str(),   O_RDONLY | O_CLOEXEC);
        break;
    }
}

static void close_battery_fds() {
    if (g_bat_cap_fd >= 0)    { close(g_bat_cap_fd);    g_bat_cap_fd = -1; }
    if (g_bat_status_fd >= 0) { close(g_bat_status_fd); g_bat_status_fd = -1; }
}

static int lua_sys_battery(LuaContext& lua) {
    lua.new_table();

    char buf[64];
    bool present = (g_bat_cap_fd >= 0);
    lua.push_bool(present);  lua.set_field(-2, "present");

    int capacity = 0;
    if (present && read_fd(g_bat_cap_fd, buf, sizeof(buf)) > 0)
        capacity = std::atoi(buf);
    lua.push_integer(capacity); lua.set_field(-2, "capacity");

    std::string status = "Unknown";
    if (present && read_fd(g_bat_status_fd, buf, sizeof(buf)) > 0) {
        status = buf;
        // strip trailing newline
        while (!status.empty() && status.back() == '\n') status.pop_back();
    }
    lua.push_string(status.c_str()); lua.set_field(-2, "status");

    return 1;
}

// ---------------------------------------------------------------------------
// Brightness — /sys/class/backlight/*
// ---------------------------------------------------------------------------

static int g_bl_cur_fd = -1;
static int g_bl_max_fd = -1;

static void open_backlight_fds() {
    if (g_bl_cur_fd >= 0) return;
    // Try common names first, then fall back to scanning
    static const char* names[] = {
        "intel_backlight", "amdgpu_bl0", "amdgpu_bl1",
        "nvidia_0", "acpi_video0", "acpi_video1", nullptr
    };
    auto try_open = [](const std::string& base) -> bool {
        int max_fd = open((base + "/max_brightness").c_str(), O_RDONLY | O_CLOEXEC);
        if (max_fd < 0) return false;
        int cur_fd = open((base + "/brightness").c_str(), O_RDONLY | O_CLOEXEC);
        if (cur_fd < 0) { close(max_fd); return false; }
        g_bl_cur_fd = cur_fd;
        g_bl_max_fd = max_fd;
        return true;
    };
    for (int i = 0; names[i]; ++i) {
        if (try_open(std::string("/sys/class/backlight/") + names[i]))
            return;
    }
    // Fallback: scan directory
    FILE* f = popen("ls /sys/class/backlight/ 2>/dev/null", "r");
    if (f) {
        char name[128];
        while (fgets(name, sizeof(name), f)) {
            size_t len = strlen(name);
            if (len > 0 && name[len - 1] == '\n') name[len - 1] = '\0';
            if (name[0] && try_open(std::string("/sys/class/backlight/") + name))
                break;
        }
        pclose(f);
    }
}

static void close_backlight_fds() {
    if (g_bl_cur_fd >= 0) { close(g_bl_cur_fd); g_bl_cur_fd = -1; }
    if (g_bl_max_fd >= 0) { close(g_bl_max_fd); g_bl_max_fd = -1; }
}

static int lua_sys_brightness(LuaContext& lua) {
    lua.new_table();

    bool present = (g_bl_cur_fd >= 0 && g_bl_max_fd >= 0);
    lua.push_bool(present); lua.set_field(-2, "present");

    int percent = 0;
    if (present) {
        char buf[32];
        int cur = 0, max = 0;
        if (read_fd(g_bl_cur_fd, buf, sizeof(buf)) > 0) cur = std::atoi(buf);
        if (read_fd(g_bl_max_fd, buf, sizeof(buf)) > 0) max = std::atoi(buf);
        if (max > 0) percent = (int)((double)cur / max * 100.0 + 0.5);
    }
    lua.push_integer(percent); lua.set_field(-2, "percent");

    return 1;
}

// ---------------------------------------------------------------------------
// kbd_layout — needs backend access via static module pointer
// ---------------------------------------------------------------------------

static SysinfoModule* g_instance = nullptr;

backend::KeyboardPort* SysinfoModule::backend_keyboard_port() {
    return backend().keyboard_port();
}

static int lua_sys_kbd_layout(LuaContext& lua) {
    auto* kp = g_instance ? g_instance->backend_keyboard_port() : nullptr;
    if (!kp) {
        lua.push_nil();
        return 1;
    }
    lua.push_string(kp->current_layout());
    return 1;
}

// ---------------------------------------------------------------------------
// Module lifecycle
// ---------------------------------------------------------------------------

void SysinfoModule::on_init() {
    // Nothing — table is built in on_lua_init once backend is available.
}

void SysinfoModule::on_start() {
    open_fds();
    open_battery_fds();
    open_backlight_fds();
}

void SysinfoModule::on_lua_init() {
    g_instance = this;

    auto& lua = config().lua();
    auto  ctx = lua.context();

    ctx.new_table();

    static const LuaFunctionReg fns[] = {
        { "cpu",        lua_sys_cpu        },
        { "mem",        lua_sys_mem        },
        { "uptime",     lua_sys_uptime     },
        { "loadavg",    lua_sys_loadavg    },
        { "net_ip",     lua_sys_net_ip     },
        { "disks",      lua_sys_disks      },
        { "kbd_layout",  lua_sys_kbd_layout  },
        { "battery",     lua_sys_battery     },
        { "brightness",  lua_sys_brightness  },
    };
    for (const auto& r : fns) {
        lua.push_callback(r.func);
        ctx.set_field(-2, r.name);
    }

    lua.set_module_table("sysinfo");
}

void SysinfoModule::on_stop(bool) {
    close_fds();
    close_battery_fds();
    close_backlight_fds();
}

SIRENWM_REGISTER_MODULE("sysinfo", SysinfoModule)
