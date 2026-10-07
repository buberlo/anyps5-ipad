#include <assert.h>
#include <stdio.h>
#include "aps5_audio_period.h"

static unsigned sdl_quantum(int64_t period, unsigned rate)
{
    /* SDL2 WASAPI's actual calculation, including its float rounding. */
    float millis = period / 10000.0f;
    return (unsigned)ceilf(millis * rate / 1000.0f);
}

static unsigned missing_frames(unsigned quantum, unsigned hardware, unsigned callbacks)
{
    /* SDL queues a block only while padding <= one block. Even with an
     * instantly answering producer, at most two blocks precede each callback. */
    unsigned padding = 0, missing = 0;
    for (unsigned i = 0; i < callbacks; ++i) {
        while (padding <= quantum) padding += quantum;
        unsigned played = padding < hardware ? padding : hardware;
        missing += hardware - played;
        padding -= played;
    }
    return missing;
}

int main(void)
{
    assert(aps5_audio_period(0) == APS5_AUDIO_FALLBACK_PERIOD);
    assert(aps5_audio_period(-1) == APS5_AUDIO_FALLBACK_PERIOD);
    assert(aps5_audio_period(NAN) == APS5_AUDIO_FALLBACK_PERIOD);
    assert(aps5_audio_period(INFINITY) == APS5_AUDIO_FALLBACK_PERIOD);
    assert(aps5_audio_period(0.0001) == APS5_AUDIO_FALLBACK_PERIOD);
    assert(aps5_audio_period(1) == APS5_AUDIO_FALLBACK_PERIOD);
    const unsigned rates[] = {44100, 48000, 96000};
    const unsigned frames[] = {128, 256, 512, 1024, 2048};
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned f = 0; f < 5; ++f) {
            int64_t period = aps5_audio_period((double)frames[f] / rates[r]);
            unsigned quantum = sdl_quantum(period, rates[r]);
            assert(quantum == frames[f]);
            assert(missing_frames(quantum, frames[f], 10000) == 0);
        }
    assert(sdl_quantum(APS5_AUDIO_FALLBACK_PERIOD, 48000) == 480);
    assert(missing_frames(480, 1024, 469) == 64 * 469);
    assert(missing_frames(sdl_quantum(aps5_audio_period(1024.0 / 48000), 48000),
                          1024, 469) == 0);
    puts("Audio period regression: 625.3 ms/10 s starvation reproduced; hardware periods pass.");
}
