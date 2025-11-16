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

} // namespace

UpseModule::UpseModule(QObject* parent)
    : QThread{parent}
{
    this->moveToThread(this);
    _slow_timer.moveToThread(this);
    _fast_timer.moveToThread(this);

    _audio.emplace(this, this);

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
    if (_channel_mapper->jal_hook(ins)) {
        update_mapped_channels();
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
    if (_channel_mapper->sw_hook(ins, size, addr, data)) {
        update_mapped_channels();
    }
}

void UpseModule::run()
{
    QThread::currentThread()->setObjectName("UpseModule");
    emit state_changed(_state);
    _slow_timer.start(500);

    int16_t* buf;
    int n;

    while (!_shutdown) {
        if (_state == State::Seeking) {
            _control.input.speed_multiplier = 2.5;
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

            if (n == -1) {
                _snapshots.clear();
                take_snapshot();
            }
        } else {
            n = 0;
        }

        if (n > 0 && buf) {
            if (_state != State::Seeking) {
                _audio->write({buf, (unsigned)n * 2});
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

        auto const key_pair = std::pair{channel.sample_addr, channel.loop_addr};

        if (_sample_freq.contains(key_pair)) {
            continue;
        }

        auto bounds = get_sample_bounds({ram, 0x80000}, channel.sample_addr, channel.loop_addr);

        if (bounds.end_addr == 0) {
            _sample_freq.emplace(key_pair, 0.0);
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

        _sample_freq.emplace(
            key_pair,
            std::async(std::launch::async,
                       [sample_mem = std::move(sample_mem),
                        addr = channel.sample_addr - min_addr,
                        loop_addr = channel.loop_addr - min_addr,
                        bounds,
                        name = fmt::format("channel {} - {:#x}, {:#x}",
                                           _channel_mapper->physical_to_logical(ch),
                                           channel.sample_addr,
                                           channel.loop_addr),
                        this] {
                           return find_sample_freq(
                               sample_mem, addr, loop_addr, bounds, _sample_freq_mutex, name);
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
        emit channel_fired(_channel_mapper->physical_to_logical(ch));
    }
}

void UpseModule::take_snapshot()
{
    auto const current_seek = milliseconds{upse_eventloop_tell_seek(_mod.get())};
    auto& emplaced = _snapshots.emplace_back();
    emplaced.first = current_seek;
    emplaced.second.second = _channel_mapper->take_snapshot();
    upse_module_take_snapshot(_mod.get(), &emplaced.second.first);
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
        upse_module_restore_snapshot(_mod.get(), &prev(it)->second.first);
        _channel_mapper->restore_snapshot(prev(it)->second.second);

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
    _sample_freq.clear();
    _control.output = {};

    if (!_mod) {
        set_state(State::Unloaded);
        emit supported_channels(0);
        return;
    }

    _channel_mapper = ChannelMapper::build(&_mod->instance, _mod->metadata->game);
    _channel_state.clear();
    _channel_state.resize(_channel_mapper->supported_channels());

    _snapshots.reserve(
        (_mod->metadata->length / duration_cast<milliseconds>(SNAPSHOT_INTERVAL).count()) + 1);
    take_snapshot();

    _paused = false;

    emit supported_channels(_channel_mapper->supported_channels());

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
    for (auto const& [log_channel, channel_state] : std::views::enumerate(_channel_state)) {
        auto const hw_channel = _channel_mapper->logical_to_physical(log_channel);
        if (hw_channel == -1) {
            continue;
        }
        _control.input.channel[hw_channel].mute = channel_state.muted;
        _control.input.channel[hw_channel].vol_multiplier = channel_state.vol;
    }
}

void UpseModule::mute_channel(int ch, bool muted)
{
    _channel_state[ch].muted = muted;
    update_mapped_channels();
}

void UpseModule::set_channel_vol(int ch, float vol)
{
    _channel_state[ch].vol = vol;
    update_mapped_channels();
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
        _audio->start();
        _fast_timer.start(33);
    } else {
        if (_state == State::Playing) {
            _audio->stop();
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

    for (auto ch = 0u; ch < _channel_state.size(); ch++) {
        auto const hw_channel = _channel_mapper->logical_to_physical(ch);
        if (hw_channel == -1) {
            emit channel_sound_level_changed(ch, 0, 0);
            emit channel_frequency_changed(ch, 0);
            continue;
        }

        auto const& channel_info = _control.output.channel[hw_channel];

        if (_state == State::Playing) {
            emit channel_sound_level_changed(
                ch, compute_rms(channel_info.l) / 32768, compute_rms(channel_info.r) / 32768);

            auto const it = _sample_freq.find({channel_info.sample_addr, channel_info.loop_addr});
            if (it == _sample_freq.end()) {
                continue;
            }
            auto const sample_freq = it->second.get();
            if (!sample_freq) {
                emit channel_frequency_changed(ch, 0);
                continue;
            }
            if (std::isnan(*sample_freq) || *sample_freq == 0) {
                emit channel_frequency_changed(ch, 0);
                continue;
            }
            emit channel_frequency_changed(ch, (*sample_freq * channel_info.pitch) / 0x1000);
        } else {
            emit channel_sound_level_changed(ch, 0, 0);
            emit channel_frequency_changed(ch, 0);
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
