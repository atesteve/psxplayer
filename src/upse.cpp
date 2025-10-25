#include "upse.h"
#include "adpcm.h"

#include "libupse/upse-spu-internal.h"
#include <fmt/format.h>

#include <QAbstractEventDispatcher>

#include <algorithm>
#include <cassert>
#include <ranges>

using namespace std::literals;
using namespace std::chrono;

namespace {

upse_iofuncs_t stdio_funcs{
    .open_impl = (void* (*)(const char* path, const char* mode))fopen,
    .read_impl = (size_t (*)(void* ptr, size_t size, size_t nmemb, void* file))fread,
    .seek_impl = (int (*)(void* file, long offset, int whence))fseek,
    .close_impl = (int (*)(void* file))fclose,
    .tell_impl = (long (*)(void* file))ftell,
};

constexpr auto SNAPSHOT_INTERVAL = 30s;

uint32_t read_psx_mem(upse_module_instance_t* ins, uint32_t addr)
{
    char const* const ptr = ins->upse_ps1_memory_LUT[addr >> 16];
    if (!ptr) {
        return 0;
    }
    return *reinterpret_cast<uint32_t const*>(ptr + (addr & 0xffffu));
}

} // namespace

UpseModule::UpseModule(QObject* parent)
    : QThread{parent}
    , _channel_state{32}
{
    this->moveToThread(this);
    _slow_timer.moveToThread(this);
    _fast_timer.moveToThread(this);

    _audio = Audio::open(44100);

    if (!_audio) {
        return;
    }

    QObject::connect(&_slow_timer, &QTimer::timeout, this, &UpseModule::slow_timer_fired);
    _slow_timer.setSingleShot(false);
    QObject::connect(&_fast_timer, &QTimer::timeout, this, &UpseModule::fast_timer_fired);
    _fast_timer.setSingleShot(false);

    for (auto& channel : _control.input.channel) {
        channel.vol_multiplier = 1.f;
    }
}

void UpseModule::jal_hook(void* self, upse_module_instance_t* ins)
{
    static_cast<UpseModule*>(self)->jal_hook(ins);
}

void UpseModule::jal_hook(upse_module_instance_t* ins)
{
    if (!_channel_map.empty()) {
        return;
    }

    auto const [base_ptr_offset, channel_ptr_pitch] = [&] -> std::pair<size_t, size_t> {
        switch (ins->cpustate.pc) {
        case 0x80015fe0: // FF8
            return {0xf4, 0x110};
        case 0x800585e0: // FF9
            return {0x118, 0x134};
        case 0x8004d084: // Chrono Cross
            return {0x108, 0x124};
        default:
            return {};
        }
    }();

    // Not an interesting function address.
    if (base_ptr_offset == 0) {
        return;
    }

    // a0 constains the interesting pointer.
    auto const base_ptr = ins->cpustate.GPR.n.a0;
    // First channel at offset `base_ptr_offset`
    auto const channel_base_ptr = base_ptr + base_ptr_offset;

    // Each channel every `channel_ptr_pitch` bytes.
    for (int i = 0; i < 32; ++i) {
        auto const channel_ptr = channel_base_ptr + i * channel_ptr_pitch;
        _channel_map[channel_ptr] = {i, read_psx_mem(ins, channel_ptr)};
    }
}

void UpseModule::sw_hook(void* self,
                         upse_module_instance_t* ins,
                         mem_access_size_t size,
                         uint32_t addr,
                         uint32_t data)
{
    static_cast<UpseModule*>(self)->sw_hook(ins, size, addr, data);
}

void UpseModule::sw_hook(upse_module_instance_t* ins,
                         mem_access_size_t size,
                         uint32_t addr,
                         uint32_t data)
{
    (void)ins;
    (void)size;

    auto const it = _channel_map.find(addr);
    if (it == _channel_map.cend()) {
        return;
    }
    it->second.second = data;
    update_mapped_channels();
}

void UpseModule::run()
{
    QThread::currentThread()->setObjectName("UpseModule");
    emit state_changed(_state);
    _slow_timer.start(500);

    int16_t* buf;
    size_t n;

    while (!_shutdown) {
        if (_state == State::Seeking) {
            _control.input.speed_multiplier = 10;
        }

        if (_mod && (_state == State::Playing || _state == State::Seeking)) {
            n = upse_eventloop_render(_mod.get(), &buf);

            auto const current_seek = milliseconds{upse_eventloop_tell_seek(_mod.get())};
            if (_snapshots.back().first + SNAPSHOT_INTERVAL < current_seek) {
                take_snapshot();
            }

            if (n == 0) {
                set_state(State::Stopped);
                _paused = true;
                _control.input.speed_multiplier = _speed;
                slow_timer_fired();
            }
        } else {
            n = 0;
        }

        if (n > 0 && buf) {
            if (_state != State::Seeking) {
                _audio->write({buf, n * 2});
            }
            if (_state == State::Seeking) {
                _control.input.speed_multiplier = _speed;
                set_state(_paused ? State::Paused : State::Playing);
                slow_timer_fired();
            }
        }

        if (_state == State::Playing) {
            handle_channel_fire();
            find_sample_frequency();
        }

        // QEventLoop::AllEvents returns immediately if there are no events to dispatch.
        // QEventLoop::WaitForMoreEvents blocks until there is at least one event, then dispatches
        // it and returns.
        eventDispatcher()->processEvents((_state == State::Seeking || _state == State::Playing)
                                             ? QEventLoop::AllEvents
                                             : QEventLoop::WaitForMoreEvents);
    }
}

void UpseModule::find_sample_frequency()
{
    auto const* spu_state = reinterpret_cast<upse_spu_state_t const*>(_mod->instance.spu);
    auto const offset_to_ram =
        *reinterpret_cast<uint32_t const*>((char const*)spu_state->pCore + sizeof(uint32_t));
    auto const* ram = reinterpret_cast<uint8_t const*>(spu_state->pCore) + offset_to_ram;

    for (auto const& [ch, channel] : std::ranges::enumerate_view{_control.output.channel}) {
        if (!channel.sample_addr) {
            continue;
        }

        if (_sample_freq.contains(channel.sample_addr)) {
            continue;
        }

        auto bounds = get_sample_bounds({ram, 0x80000}, channel.sample_addr, channel.loop_addr);

        if (bounds.end_addr == 0) {
            _sample_freq.emplace(channel.sample_addr, 0.0);
            continue;
        }

        auto const v = {bounds.start_addr, bounds.loop_addr, bounds.end_addr};
        auto const min_addr = *std::ranges::min_element(v);

        auto const sample_size = bounds.max_addr - min_addr;
        std::vector<uint8_t> sample_mem;
        sample_mem.resize(sample_size);
        std::memcpy(sample_mem.data(), ram + min_addr, sample_size);

        bounds.start_addr -= min_addr;
        bounds.loop_addr -= min_addr;
        bounds.end_addr -= min_addr;
        bounds.max_addr -= min_addr;

        _sample_freq.emplace(channel.sample_addr,
                             std::async(std::launch::async,
                                        [sample_mem = std::move(sample_mem),
                                         addr = channel.sample_addr - min_addr,
                                         loop_addr = channel.loop_addr - min_addr,
                                         bounds,
                                         this] {
                                            return find_sample_freq(sample_mem,
                                                                    addr,
                                                                    loop_addr,
                                                                    bounds,
                                                                    _sample_freq_mutex);
                                        }));
    }
}

void UpseModule::handle_channel_fire()
{
    for (auto const& [ch, channel] : std::ranges::enumerate_view{_control.output.channel}) {
        if (!channel.fired) {
            continue;
        }

        channel.fired = false;

        if (_channel_map.empty()) {
            emit channel_fired(ch);
        } else {
            auto const it = std::ranges::find_if(
                _channel_map, [&](auto const& entry) { return entry.second.second == ch; });
            if (it != _channel_map.cend()) {
                emit channel_fired(it->second.first);
            }
        }
    }
}

void UpseModule::take_snapshot()
{
    auto const current_seek = milliseconds{upse_eventloop_tell_seek(_mod.get())};
    auto& emplaced = _snapshots.emplace_back();
    emplaced.first = current_seek;
    upse_module_take_snapshot(_mod.get(), &emplaced.second);
}

void UpseModule::seek(int pos)
{
    if (!_mod) {
        return;
    }

    int const current_seek = upse_eventloop_tell_seek(_mod.get());

    if (pos == current_seek) {
        return;
    }

    auto it = std::ranges::upper_bound(_snapshots,
                                       milliseconds{pos},
                                       std::less<>{},
                                       [](auto const& element) { return element.first; });
    assert(it != _snapshots.begin());
    if (pos < current_seek || prev(it)->first.count() > current_seek) {
        upse_module_restore_snapshot(_mod.get(), &prev(it)->second);

        if (prev(it)->first.count() == pos) {
            return;
        }
    }

    upse_eventloop_seek(_mod.get(), pos);
    set_state(State::Seeking);
}

void UpseModule::load_file(QString const& file_name)
{
    _mod.reset(upse_module_open(file_name.toStdString().c_str(), &stdio_funcs, &_control));

    _snapshots.clear();
    _snapshots.shrink_to_fit();
    _channel_map.clear();
    _sample_freq.clear();
    _control.output = {};

    if (!_mod) {
        set_state(State::Unloaded);
        emit supported_channels(0);
        return;
    }

    _snapshots.reserve(
        (_mod->metadata->length / duration_cast<milliseconds>(SNAPSHOT_INTERVAL).count()) + 1);
    take_snapshot();

    _paused = false;

    std::string_view game_name{_mod->metadata->game};
    if (game_name == "Final Fantasy 9") {
        emit supported_channels(32);
    } else if (game_name == "Final Fantasy 8") {
        emit supported_channels(32);
    } else if (game_name == "Final Fantasy 7") {
        emit supported_channels(16);
    } else if (game_name == "Chrono Cross") {
        emit supported_channels(32);
    } else {
        emit supported_channels(24);
    }

    set_state(State::Playing);
    emit total_time_changed(milliseconds{_mod->metadata->length});
    emit seek_changed(0ms);
}

void UpseModule::toggle_pause()
{
    if (!_mod) {
        return;
    }

    switch (_state) {
    case State::Seeking:
    case State::Playing:
        _paused = true;
        set_state(State::Paused);
        break;

    case State::Stopped:
        seek(0);
        [[fallthrough]];

    case State::Paused:
        _paused = false;
        set_state(State::Playing);
        break;

    case State::Unloaded:
        // Do nothing
        break;
    }
}

void UpseModule::stop()
{
    if (!_mod) {
        return;
    }

    seek(0);
    set_state(State::Stopped);
    _paused = true;
}

void UpseModule::set_speed(float speed)
{
    _speed = speed;
    if (_state != State::Seeking) {
        _control.input.speed_multiplier = _speed;
    }
}

void UpseModule::update_mapped_channels()
{
    for (auto const& mapped_channel : _channel_map) {
        auto const [log_channel, hw_channel] = mapped_channel.second;
        if (hw_channel >= 24) {
            continue;
        }
        _control.input.channel[hw_channel].mute = _channel_state[log_channel].muted;
        _control.input.channel[hw_channel].vol_multiplier = _channel_state[log_channel].vol;
    }
}

void UpseModule::mute_channel(int ch, bool muted)
{
    _channel_state[ch].muted = muted;

    if (_channel_map.empty()) {
        _control.input.channel[ch].mute = muted;
    } else {
        update_mapped_channels();
    }
}

void UpseModule::set_channel_vol(int ch, float vol)
{
    _channel_state[ch].vol = vol;

    if (_channel_map.empty()) {
        _control.input.channel[ch].vol_multiplier = vol;
    } else {
        update_mapped_channels();
    }
}

void UpseModule::shutdown()
{
    _slow_timer.stop();
    _fast_timer.stop();
    _shutdown = true;
}

void UpseModule::set_state(State new_state)
{
    if (new_state == State::Playing) {
        _fast_timer.start(33);
    } else {
        if (_state == State::Playing) {
            _audio->flush();
        }
        _stopped_cycles = 0;
    }

    auto const old_state = _state;
    _state = new_state;
    if (old_state != new_state) {
        state_changed(new_state);
    }
}

void UpseModule::slow_timer_fired()
{
    if (_state != State::Playing && _stopped_cycles > 2) {
        _fast_timer.stop();
    }
    _stopped_cycles++;

    if (!_mod || _state == State::Seeking) {
        return;
    }
    emit seek_changed(milliseconds{upse_eventloop_tell_seek(_mod.get())});
}

void UpseModule::fast_timer_fired()
{
    auto const compute_rms = [](auto&& buf) {
        double rms = 0;
        for (auto s : buf) {
            rms += s * s;
        }
        return std::sqrt(rms / std::size(buf));
    };

    if (_state == State::Playing) {
        emit sound_level_changed(compute_rms(_control.output.window_l) / 32768,
                                 compute_rms(_control.output.window_r) / 32768);
    } else {
        emit sound_level_changed(0, 0);
    }

    auto const emit_channel_signals = [&](int ch, out_channel_t const& channel) {
        if (_state == State::Playing) {
            emit channel_sound_level_changed(
                ch, compute_rms(channel.l) / 32768, compute_rms(channel.r) / 32768);

            auto const it = _sample_freq.find(channel.sample_addr);
            if (it == _sample_freq.end()) {
                return;
            }
            auto const sample_freq = it->second.get();
            if (!sample_freq) {
                emit channel_frequency_changed(ch, 0);
                return;
            }
            if (std::isnan(*sample_freq) || *sample_freq == 0) {
                emit channel_frequency_changed(ch, 0);
                return;
            }
            emit channel_frequency_changed(ch, (*sample_freq * channel.pitch) / 0x1000);
        } else {
            emit channel_sound_level_changed(ch, 0, 0);
        }
    };

    if (_channel_map.empty()) {
        for (auto const& [ch, channel] : std::ranges::enumerate_view{_control.output.channel}) {
            emit_channel_signals(ch, channel);
        }
    } else {
        for (auto const& mapped_channel : _channel_map) {
            auto const [log_channel, hw_channel] = mapped_channel.second;
            if (hw_channel >= 24) {
                continue;
            }
            emit_channel_signals(log_channel, _control.output.channel[hw_channel]);
        }
    }
}

UpseModule::Freq::Freq(double value)
    : _value{value}
{}

UpseModule::Freq::Freq(std::future<double> future)
    : _future(std::move(future))
{}

std::optional<double> UpseModule::Freq::get()
{
    if (_value) {
        return _value;
    }
    auto const future_state = _future.wait_for(0s);
    if (future_state != std::future_status::ready) {
        return std::nullopt;
    }
    _value = _future.get();
    return _value;
}
