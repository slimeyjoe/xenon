// ymfm YM2203 behind the C opn_core interface.
//
// ymfm generates at input_clock / 24 with OPN_FIDELITY_MIN (166 400 Hz for the 3.9936 MHz
// 26K clock), repeating each FM sample 3x and running the SSG at its own rate. The FM engine's
// real rate is clock / 72 = 55 467 Hz, so three generated samples are averaged into one output
// sample: FM samples are identical within a group, the SSG gets a 3-tap box filter. The host
// resamples from 55 467 Hz.
#include "ymfm/ymfm_opn.h"
extern "C" {
#include "snd/opn_core.h"
}

namespace {

class intf : public ymfm::ymfm_interface {
public:
    // Timers are modelled by the sequencer; the chip's own timers are ignored.
    void ymfm_set_timer(uint32_t, int32_t) override {}
};

struct core {
    intf iface;
    ymfm::ym2203 chip;
    uint32_t rate;
    int32_t hp_x1;               // DC-blocking high-pass state (sound.md §6.1)
    int64_t hp_y1;               // 16.16
    core() : chip(iface), rate(0), hp_x1(0), hp_y1(0) {}
};

struct opn_core *ymfm_create(uint32_t clock_hz, uint32_t want_rate, uint32_t *rate)
{
    (void)want_rate;
    core *c = new core();
    c->chip.set_fidelity(ymfm::OPN_FIDELITY_MIN);
    c->rate = c->chip.sample_rate(clock_hz) / 3;       // 166 400 / 3 = 55 467
    c->chip.reset();
    *rate = c->rate;
    return reinterpret_cast<struct opn_core *>(c);
}

void ymfm_destroy(struct opn_core *oc)
{
    delete reinterpret_cast<core *>(oc);
}

void ymfm_reset(struct opn_core *oc)
{
    reinterpret_cast<core *>(oc)->chip.reset();
}

void ymfm_write(struct opn_core *oc, uint8_t reg, uint8_t val)
{
    core *c = reinterpret_cast<core *>(oc);
    c->chip.write_address(reg);
    c->chip.write_data(val);
}

void ymfm_render(struct opn_core *oc, int16_t *mono, int frames)
{
    core *c = reinterpret_cast<core *>(oc);
    ymfm::ym2203::output_data out[3];
    for (int i = 0; i < frames; i++) {
        c->chip.generate(out, 3);
        // FM output (index 0) plus the three SSG channels (1-3). ymfm's SSG channels are
        // unipolar; the 26K's output coupling removes the DC on hardware (sound.md §6.1),
        // modelled by a one-pole high-pass (~45 Hz) on the mix. Balance FM : SSG = 1 : 1/4
        // per channel (unknown on hardware; sound.md §7).
        int32_t fm = (out[0].data[0] + out[1].data[0] + out[2].data[0]) / 3;
        int32_t ssg = 0;
        for (int k = 0; k < 3; k++)
            ssg += out[k].data[1] + out[k].data[2] + out[k].data[3];
        ssg = ssg / 12;
        int32_t x = (fm + ssg) * 2;          // level: ~libvgm reference (sound.md §6.1)
        // y = x - x1 + pole * y1 (pole = 1 - 334/65536, ~45 Hz at 55 467 Hz, same as opn_lite),
        // y1 kept in 16.16 so the feedback does not stall at a DC
        int64_t y = ((int64_t)(x - c->hp_x1) << 16) + c->hp_y1 - ((c->hp_y1 * 334) >> 16);
        c->hp_x1 = x;
        c->hp_y1 = y;
        y >>= 16;
        if (y > 32767) y = 32767;
        if (y < -32768) y = -32768;
        mono[i] = (int16_t)y;
    }
}

} // namespace

extern "C" const struct opn_core_ops opn_ymfm_ops = {
    "ymfm", ymfm_create, ymfm_destroy, ymfm_reset, ymfm_write, ymfm_render, nullptr
};
