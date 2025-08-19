#include "audio.h"

pa_simple_unique_ptr open_sound_device(unsigned rate)
{
    pa_sample_spec ss{
        .format = PA_SAMPLE_S16NE,
        .rate = rate,
        .channels = 2,
    };

    auto* s = pa_simple_new(nullptr,     // Use the default server.
                            "psxplayer", // Our application's name.
                            PA_STREAM_PLAYBACK,
                            nullptr,         // Use the default device.
                            "PSX/PS2 music", // Description of our stream.
                            &ss,             // Our sample format.
                            nullptr,         // Use default channel map
                            nullptr,         // Use default buffering attributes.
                            nullptr          // Ignore error code.
    );

    return pa_simple_unique_ptr{s};
}
