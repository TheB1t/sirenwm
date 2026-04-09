#include "audio_module.hpp"

#include <lua_host.hpp>
#include <log.hpp>
#include <module_registry.hpp>

#include <alsa/asoundlib.h>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// ALSA mixer helpers
// ---------------------------------------------------------------------------

struct VolumeInfo {
    bool available = false;
    int  percent   = 0;
    bool muted     = false;
};

static VolumeInfo read_mixer(const char* card, const char* elem_name, bool capture) {
    VolumeInfo   v;

    snd_mixer_t* mixer = nullptr;
    if (snd_mixer_open(&mixer, 0) < 0)
        return v;
    if (snd_mixer_attach(mixer, card) < 0 ||
        snd_mixer_selem_register(mixer, nullptr, nullptr) < 0 ||
        snd_mixer_load(mixer) < 0) {
        snd_mixer_close(mixer);
        return v;
    }

    snd_mixer_selem_id_t* sid;
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, elem_name);

    snd_mixer_elem_t* elem = snd_mixer_find_selem(mixer, sid);
    if (!elem) {
        snd_mixer_close(mixer);
        return v;
    }

    long min = 0, max = 0, vol = 0;

    if (capture) {
        if (!snd_mixer_selem_has_capture_volume(elem)) {
            snd_mixer_close(mixer);
            return v;
        }
        snd_mixer_selem_get_capture_volume_range(elem, &min, &max);
        snd_mixer_selem_get_capture_volume(elem, SND_MIXER_SCHN_FRONT_LEFT, &vol);

        int sw = 0;
        if (snd_mixer_selem_has_capture_switch(elem)) {
            snd_mixer_selem_get_capture_switch(elem, SND_MIXER_SCHN_FRONT_LEFT, &sw);
            v.muted = (sw == 0);
        }
    } else {
        snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
        snd_mixer_selem_get_playback_volume(elem, SND_MIXER_SCHN_FRONT_LEFT, &vol);

        int sw = 0;
        if (snd_mixer_selem_has_playback_switch(elem)) {
            snd_mixer_selem_get_playback_switch(elem, SND_MIXER_SCHN_FRONT_LEFT, &sw);
            v.muted = (sw == 0);
        }
    }

    v.available = true;
    if (max > min)
        v.percent = (int)((double)(vol - min) / (max - min) * 100.0 + 0.5);

    snd_mixer_close(mixer);
    return v;
}

// ---------------------------------------------------------------------------
// Configurable card and element names
// ---------------------------------------------------------------------------

static std::string g_card        = "default";
static std::string g_output_elem = "Master";
static std::string g_input_elem  = "Capture";

// ---------------------------------------------------------------------------
// Lua API: audio.volume(), audio.output(), audio.input()
// ---------------------------------------------------------------------------

static void push_volume_table(LuaContext& lua, const VolumeInfo& v) {
    lua.new_table();
    lua.push_bool(v.available);  lua.set_field(-2, "available");
    lua.push_integer(v.percent); lua.set_field(-2, "percent");
    lua.push_bool(v.muted);     lua.set_field(-2, "muted");
}

static int lua_audio_volume(LuaContext& lua) {
    VolumeInfo out = read_mixer(g_card.c_str(), g_output_elem.c_str(), false);
    VolumeInfo in  = read_mixer(g_card.c_str(), g_input_elem.c_str(),  true);
    lua.new_table();
    push_volume_table(lua, out); lua.set_field(-2, "output");
    push_volume_table(lua, in);  lua.set_field(-2, "input");
    return 1;
}

static int lua_audio_output(LuaContext& lua) {
    VolumeInfo v = read_mixer(g_card.c_str(), g_output_elem.c_str(), false);
    push_volume_table(lua, v);
    return 1;
}

static int lua_audio_input(LuaContext& lua) {
    VolumeInfo v = read_mixer(g_card.c_str(), g_input_elem.c_str(), true);
    push_volume_table(lua, v);
    return 1;
}

// ---------------------------------------------------------------------------
// Module lifecycle
// ---------------------------------------------------------------------------

static AudioModule* g_instance = nullptr;

void AudioModule::on_init() {}

void AudioModule::on_lua_init() {
    g_instance = this;

    auto& lua = this->lua();
    auto  ctx = lua.context();

    // Proxy table with __newindex for audio.settings = {...}
    ctx.new_table();   // proxy
    ctx.new_table();   // metatable

    // Register functions on the proxy directly
    static const LuaFunctionReg fns[] = {
        { "volume", lua_audio_volume },
        { "output", lua_audio_output },
        { "input",  lua_audio_input  },
    };
    // Store functions in a raw table inside the proxy so they survive metatable
    for (const auto& r : fns) {
        lua.push_callback(r.func);
        ctx.set_field(-3, r.name);  // set on proxy (below metatable)
    }

    lua.push_callback([](LuaContext& lctx, void* /*ud*/) -> int {
            // __newindex(proxy, key, value)
            std::string key = lctx.is_string(2) ? lctx.to_string(2) : "";
            if (key == "settings" && lctx.is_table(3)) {
                lctx.get_field(3, "card");
                if (lctx.is_string(-1)) g_card = lctx.to_string(-1);
                lctx.pop(1);

                lctx.get_field(3, "output");
                if (lctx.is_string(-1)) g_output_elem = lctx.to_string(-1);
                lctx.pop(1);

                lctx.get_field(3, "input");
                if (lctx.is_string(-1)) g_input_elem = lctx.to_string(-1);
                lctx.pop(1);

                LOG_INFO("audio: card=%s output=%s input=%s",
                g_card.c_str(), g_output_elem.c_str(), g_input_elem.c_str());
            }
            return 0;
        }, nullptr);
    ctx.set_field(-2, "__newindex");
    ctx.set_metatable(-2);

    lua.set_module_table("audio");
}

void AudioModule::on_start() {
    VolumeInfo test = read_mixer(g_card.c_str(), g_output_elem.c_str(), false);
    if (test.available)
        LOG_INFO("audio: ALSA mixer OK (%s/%s: %d%%)",
            g_card.c_str(), g_output_elem.c_str(), test.percent);
    else
        LOG_WARN("audio: element '%s' not found on card '%s'",
            g_output_elem.c_str(), g_card.c_str());
}

void AudioModule::on_stop(bool) {
    g_instance = nullptr;
}

SIRENWM_REGISTER_MODULE("audio", AudioModule)
