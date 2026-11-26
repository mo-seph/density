// Engine and filter tests. Configs are built by hand (the JSON loader is tested elsewhere).
#include <unity.h>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "fake_hal.h"
#include "../../lib/core/src/engine.h"
#include "../../lib/core/src/filter.h"

using namespace density;

// ---------------------------------------------------------------- helpers

static uint32_t rngState = 12345;
static int noise(int amp) {   // deterministic uniform -amp..amp
    rngState = rngState * 1664525u + 1013904223u;
    return int((rngState >> 8) % uint32_t(2 * amp + 1)) - amp;
}
static uint16_t clampRaw(int r) { return uint16_t(r < 0 ? 0 : (r > 4095 ? 4095 : r)); }

// Raw reading for a position with default calibration (40..4055).
static uint16_t rawAt(float pos) { return clampRaw(int(std::lround(40 + pos * (4055 - 40)))); }

struct Rig {
    FakeHal hal;
    Config cfg;
    std::unique_ptr<Engine> eng;
    int noiseAmp = 0;                 // ADC noise added on every tick
    uint16_t base[kMaxAnalog] = {};   // noiseless value per control

    Rig(uint8_t banks = 2) {
        cfg.numBanks = banks;
        for (uint8_t a = 0; a < kMaxAnalog; ++a)
            for (uint8_t b = 0; b < kMaxBanks; ++b) {
                cfg.analog[a][b].addr = {MidiType::CC, 1, uint8_t(a + 32 * b)};
                cfg.analog[a][b].pickup = PickupMode::Pickup;
            }
        for (auto& v : base) v = rawAt(0.5f);
    }
    // Creates the engine and runs it through its boot window (Engine::kBootUs).
    void start() {
        applyRaw();
        hal.poll();
        eng.reset(new Engine(hal, cfg));
        eng->update();
        while (!eng->booted()) tick();
    }
    void applyRaw() {
        for (uint8_t a = 0; a < kMaxAnalog; ++a)
            hal.setAnalog(a, noiseAmp ? clampRaw(base[a] + noise(noiseAmp)) : base[a]);
    }
    void tick(uint32_t us = 1000) {
        hal.advanceUs(us);
        applyRaw();
        hal.poll();
        eng->update();
    }
    void run(uint32_t ms) { for (uint32_t i = 0; i < ms; ++i) tick(); }
    // Linear move of control a to pos over ms, then hold for holdMs.
    void moveTo(AnalogId a, float pos, uint32_t ms = 100, uint32_t holdMs = 20) {
        const int from = base[a], to = rawAt(pos);
        for (uint32_t i = 1; i <= ms; ++i) {
            base[a] = uint16_t(from + (to - from) * int(i) / int(ms));
            tick();
        }
        run(holdMs);
    }
    // Leave controls alone until every filter is still.
    void settle() { run(400); }
    std::vector<MidiMsg>& sent() { return hal.sent; }
};

static std::unique_ptr<Rig> rig;

void setUp() { rngState = 12345; }
void tearDown() { rig.reset(); }


// ---------------------------------------------------------------- filter

void test_filter_noise_still_never_moves() {
    Filter f;
    f.configure(FilterParams{}, AnalogCal{}, false);
    uint32_t t = 0;
    f.update(2000, t);
    int movingTicks = 0;
    for (int i = 0; i < 20000; ++i) {   // 20 s at 1 kHz
        t += 1000;
        int r = 2000 + noise(4);
        if (i % 997 == 0) r += 400;              // single-sample spike
        if (i % 4001 == 0) r -= 600;             // and another
        if (i % 7919 == 0) r += 300;             // a two-sample spike
        if (i % 7919 == 1) r += 300;
        f.update(clampRaw(r), t);
        movingTicks += f.moving();
    }
    TEST_ASSERT_EQUAL(0, movingTicks);
}

void test_filter_timing_jitter_still() {
    // Irregular call intervals (0.3..3 ms) must not matter.
    Filter f;
    f.configure(FilterParams{}, AnalogCal{}, false);
    uint32_t t = 0;
    f.update(1000, t);
    for (int i = 0; i < 10000; ++i) {
        t += 300 + uint32_t(noise(1350) + 1350);
        f.update(clampRaw(1000 + noise(4)), t);
        TEST_ASSERT_FALSE(f.moving());
    }
}

void test_filter_step_response_fast() {
    Filter f;
    f.configure(FilterParams{}, AnalogCal{}, false);
    uint32_t t = 0;
    f.update(rawAt(0.25f), t);
    int movedAt = -1, within1pct = -1;
    for (int i = 1; i <= 100; ++i) {
        t += 1000;
        f.update(rawAt(0.75f), t);
        if (movedAt < 0 && f.moving()) movedAt = i;
        if (within1pct < 0 && std::fabs(f.position() - 0.75f) < 0.01f) within1pct = i;
    }
    TEST_ASSERT_TRUE(movedAt > 0 && movedAt <= 5);        // touch detected within 5 ms
    TEST_ASSERT_TRUE(within1pct > 0 && within1pct <= 15); // within 1% in 15 ms
}

void test_filter_exact_end_stops_and_settle() {
    Filter f;
    f.configure(FilterParams{}, AnalogCal{}, false);
    uint32_t t = 0;
    f.update(rawAt(0.5f), t);
    // sweep to the top over 200 ms, beyond rawMax, with noise
    for (int i = 1; i <= 200; ++i) {
        t += 1000;
        f.update(clampRaw(rawAt(0.5f) + (4095 - rawAt(0.5f)) * i / 200 + noise(3)), t);
    }
    for (int i = 0; i < 20; ++i) { t += 1000; f.update(clampRaw(4090 + noise(3)), t); }
    TEST_ASSERT_EQUAL_FLOAT(1.0f, f.position());
    TEST_ASSERT_TRUE(f.position() == 1.0f);
    // and down to the bottom (below rawMin)
    for (int i = 1; i <= 200; ++i) {
        t += 1000;
        f.update(clampRaw(4095 - 4095 * i / 200 + noise(3)), t);
    }
    for (int i = 0; i < 20; ++i) { t += 1000; f.update(clampRaw(5 + noise(3)), t); }
    TEST_ASSERT_TRUE(f.position() == 0.0f);
    TEST_ASSERT_TRUE(f.moving());
    for (int i = 0; i < 400; ++i) { t += 1000; f.update(clampRaw(5 + noise(3)), t); }
    TEST_ASSERT_FALSE(f.moving());
    TEST_ASSERT_TRUE(f.position() == 0.0f);
}

void test_filter_push_onto_end_stop_from_rest() {
    // Resting a hair below the top, pushed onto the stop: must reach exactly 1.0.
    Filter f;
    f.configure(FilterParams{}, AnalogCal{}, false);
    uint32_t t = 0;
    f.reset(4040, t);   // 0.9963, inside the dead band of the stop
    for (int i = 0; i < 30; ++i) { t += 1000; f.update(4060, t); }
    TEST_ASSERT_TRUE(f.position() == 1.0f);
}

void test_filter_rate_independent() {
    // Same step response at 500 Hz and 4 kHz (within a few ms).
    auto timeTo = [](uint32_t stepUs) {
        Filter f;
        f.configure(FilterParams{}, AnalogCal{}, false);
        uint32_t t = 0;
        f.update(rawAt(0.2f), t);
        while (t < 200000) {
            t += stepUs;
            f.update(rawAt(0.8f), t);
            if (std::fabs(f.position() - 0.8f) < 0.01f) return t;
        }
        return t;
    };
    uint32_t slow = timeTo(2000), fast = timeTo(250);
    TEST_ASSERT_TRUE(slow < 25000);
    TEST_ASSERT_TRUE(fast < 25000);
}

void test_filter_invert_and_calibration() {
    Filter f;
    f.configure(FilterParams{}, AnalogCal{0, 4000}, true);
    f.update(1000, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.75f, f.position());
    f.setCalibration(AnalogCal{1000, 3000});
    TEST_ASSERT_TRUE(f.position() == 1.0f);   // inverted, at rawMin
    TEST_ASSERT_FALSE(f.moving());
}

// ---------------------------------------------------------------- analog engine

void test_boot_sends_nothing_noisy_for_seconds() {
    rig.reset(new Rig());
    rig->noiseAmp = 4;
    for (uint8_t a = 0; a < 19; ++a) rig->base[a] = rawAt(a / 18.0f);
    rig->start();
    rig->run(10000);
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());
    // boot: stored = physical, picked up
    AnalogState s = rig->eng->analog(9);
    TEST_ASSERT_TRUE(s.pickedUp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, s.stored);
    TEST_ASSERT_FALSE(s.moving);
}

void test_sweep_sends_each_value_once_to_exact_ends() {
    rig.reset(new Rig());
    rig->noiseAmp = 3;
    rig->base[0] = 0;
    rig->start();
    rig->moveTo(0, 1.01f, 1000);   // past the top stop
    auto& v = rig->sent();
    TEST_ASSERT_TRUE(v.size() >= 120);
    for (size_t i = 1; i < v.size(); ++i) TEST_ASSERT_TRUE(v[i].value > v[i - 1].value);
    TEST_ASSERT_EQUAL(127, v.back().value);
    TEST_ASSERT_EQUAL(0, v.back().number);
    TEST_ASSERT_TRUE(v.back().type == MidiType::CC);
    rig->hal.clearSent();
    rig->base[0] = 4095;
    rig->moveTo(0, -0.02f, 1000);
    TEST_ASSERT_EQUAL(0, v.back().value);
    rig->hal.clearSent();
    rig->run(5000);
    TEST_ASSERT_EQUAL(0, (int)v.size());
}

void test_seven_bit_boundary_no_chatter() {
    rig.reset(new Rig());
    rig->noiseAmp = 0;
    rig->start();
    // move to exactly half way between 7-bit values 70 and 71, then sit there with noise
    rig->moveTo(0, 70.5f / 127.0f, 100, 0);
    rig->hal.clearSent();
    rig->noiseAmp = 6;
    for (int k = 0; k < 10; ++k) {
        rig->run(250);   // still inside the settle time at first, then settled
    }
    TEST_ASSERT_TRUE(rig->sent().size() <= 1);
}

void test_slow_move_inside_settle_window_tracks() {
    // A very slow move still gets through: drift accumulates against the frozen position.
    rig.reset(new Rig());
    rig->start();
    rig->moveTo(0, 0.6f, 5000, 0);
    TEST_ASSERT_TRUE(rig->sent().size() > 5);
    TEST_ASSERT_UINT8_WITHIN(1, 76, rig->sent().back().value);
}

void test_pickup_from_above() {
    rig.reset(new Rig());
    rig->start();                          // stored 0.5 everywhere
    rig->eng->setBank(1);
    rig->moveTo(0, 0.8f);                  // bank 1 now 0.8
    rig->settle();
    rig->eng->setBank(0);                  // bank 0 stored 0.5
    AnalogState s = rig->eng->analog(0);
    TEST_ASSERT_FALSE(s.pickedUp);
    TEST_ASSERT_EQUAL(-1, s.direction);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, s.stored);
    rig->hal.clearSent();
    rig->moveTo(0, 0.56f);
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());
    TEST_ASSERT_FALSE(rig->eng->analog(0).pickedUp);
    rig->moveTo(0, 0.3f);
    TEST_ASSERT_TRUE(rig->eng->analog(0).pickedUp);
    auto& v = rig->sent();
    TEST_ASSERT_TRUE(v.size() > 10);
    TEST_ASSERT_TRUE(v.front().value <= 68 && v.front().value >= 59);   // picks up within tolerance of stored (64)
    TEST_ASSERT_UINT8_WITHIN(1, 38, v.back().value);
    TEST_ASSERT_EQUAL(0, v.back().number);   // bank 0 address
}

void test_pickup_from_below() {
    rig.reset(new Rig());
    rig->start();
    rig->eng->setBank(1);
    rig->moveTo(0, 0.2f);
    rig->settle();
    rig->eng->setBank(0);
    AnalogState s = rig->eng->analog(0);
    TEST_ASSERT_FALSE(s.pickedUp);
    TEST_ASSERT_EQUAL(1, s.direction);
    rig->hal.clearSent();
    rig->moveTo(0, 0.44f);
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());
    rig->moveTo(0, 0.7f);
    auto& v = rig->sent();
    TEST_ASSERT_TRUE(v.size() > 10);
    TEST_ASSERT_TRUE(v.front().value >= 59 && v.front().value <= 68);   // within tolerance of 64
    TEST_ASSERT_UINT8_WITHIN(1, 89, v.back().value);
}

void test_pickup_by_jumping_over_stored_value() {
    // A fast move that crosses the stored value between two samples still picks up.
    rig.reset(new Rig());
    rig->start();
    rig->eng->setBank(1);
    rig->moveTo(0, 0.1f);
    rig->settle();
    rig->eng->setBank(0);
    rig->hal.clearSent();
    rig->moveTo(0, 0.9f, 3);   // 3 ms flick
    TEST_ASSERT_TRUE(rig->eng->analog(0).pickedUp);
    TEST_ASSERT_UINT8_WITHIN(1, 114, rig->sent().back().value);
}

void test_pickup_within_tolerance_is_immediate() {
    rig.reset(new Rig());
    rig->start();
    rig->eng->setBank(1);
    rig->moveTo(0, 0.52f);     // within the 0.03 tolerance of bank 0's 0.5
    rig->settle();
    rig->eng->setBank(0);
    TEST_ASSERT_TRUE(rig->eng->analog(0).pickedUp);
    TEST_ASSERT_EQUAL(0, rig->eng->analog(0).direction);
}

void test_host_cc_sets_stored_and_requires_pickup() {
    rig.reset(new Rig());
    rig->start();
    rig->hal.injectCC(1, 0, 100);   // knob 0, bank 0
    rig->run(5);
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());   // no echo
    AnalogState s = rig->eng->analog(0);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100 / 127.0f, s.stored);
    TEST_ASSERT_FALSE(s.pickedUp);
    TEST_ASSERT_EQUAL(1, s.direction);
    TEST_ASSERT_EQUAL(100, rig->eng->value7(0));
    rig->moveTo(0, 0.7f);
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());
    rig->moveTo(0, 0.95f);
    auto& v = rig->sent();
    TEST_ASSERT_TRUE(v.size() > 3);
    TEST_ASSERT_TRUE(v.front().value >= 95 && v.front().value <= 105);
    TEST_ASSERT_UINT8_WITHIN(1, 121, v.back().value);
}

void test_host_cc_near_position_stays_picked_up() {
    rig.reset(new Rig());
    rig->start();
    rig->hal.injectCC(1, 0, 65);   // 0.512, physical 0.5
    rig->run(2);
    TEST_ASSERT_TRUE(rig->eng->analog(0).pickedUp);
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());
}

void test_host_cc_other_bank_applies_on_switch() {
    rig.reset(new Rig());
    rig->start();
    rig->hal.injectCC(1, 32, 10);   // knob 0, bank 1
    rig->run(2);
    TEST_ASSERT_TRUE(rig->eng->analog(0).pickedUp);   // bank 0 untouched
    rig->eng->setBank(1);
    AnalogState s = rig->eng->analog(0);
    TEST_ASSERT_FALSE(s.pickedUp);
    TEST_ASSERT_EQUAL(-1, s.direction);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10 / 127.0f, s.stored);
}

void test_host_note_sets_stored_for_note_binding() {
    rig.reset(new Rig());
    rig->cfg.analog[1][0].addr = {MidiType::Note, 3, 60};
    rig->start();
    rig->hal.injectNote(3, 60, 127);
    rig->hal.injectCC(3, 60, 0);     // wrong type: ignored
    rig->hal.injectNote(4, 60, 0);   // wrong channel: ignored
    rig->run(1);
    TEST_ASSERT_TRUE(rig->eng->analog(1).stored == 1.0f);
    rig->moveTo(1, 1.02f);
    TEST_ASSERT_TRUE(rig->sent().size() >= 1);
    TEST_ASSERT_TRUE(rig->sent().back().type == MidiType::Note);
    TEST_ASSERT_EQUAL(3, rig->sent().back().channel);
    TEST_ASSERT_EQUAL(127, rig->sent().back().value);
}

void test_host_cc_while_moving_hand_wins() {
    rig.reset(new Rig());
    rig->start();
    rig->moveTo(0, 0.6f, 100, 0);   // still moving
    rig->hal.injectCC(1, 0, 5);
    rig->moveTo(0, 0.7f, 50, 0);
    TEST_ASSERT_TRUE(rig->eng->analog(0).pickedUp);
    TEST_ASSERT_UINT8_WITHIN(1, 89, rig->sent().back().value);
    TEST_ASSERT_EQUAL(rig->sent().back().value, rig->eng->value7(0));   // 7-bit shadow agrees
}

void test_jump_mode() {
    rig.reset(new Rig());
    for (uint8_t b = 0; b < kMaxBanks; ++b) rig->cfg.analog[0][b].pickup = PickupMode::Jump;
    rig->start();
    rig->eng->setBank(1);
    rig->moveTo(0, 0.8f);
    rig->settle();
    rig->hal.clearSent();
    rig->eng->setBank(0);
    rig->run(500);
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());   // bank change alone sends nothing
    AnalogState s = rig->eng->analog(0);
    TEST_ASSERT_TRUE(s.pickedUp);
    TEST_ASSERT_EQUAL(0, s.direction);
    // nudge just beyond the dead band: sends the physical value at once
    rig->moveTo(0, 0.81f, 5, 5);
    TEST_ASSERT_TRUE(rig->sent().size() >= 1);
    TEST_ASSERT_UINT8_WITHIN(1, 102, rig->sent().front().value);
    TEST_ASSERT_EQUAL(0, rig->sent().front().number);
}

void test_jump_mode_after_host_cc() {
    rig.reset(new Rig());
    rig->cfg.analog[0][0].pickup = PickupMode::Jump;
    rig->start();
    rig->hal.injectCC(1, 0, 120);
    rig->run(10);
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());
    TEST_ASSERT_TRUE(rig->eng->analog(0).pickedUp);
    rig->moveTo(0, 0.48f, 10, 5);
    TEST_ASSERT_TRUE(rig->sent().size() >= 1);
    TEST_ASSERT_UINT8_WITHIN(2, 62, rig->sent().front().value);
}

void test_shared_vs_banked_stored_values() {
    rig.reset(new Rig(3));
    for (uint8_t b = 0; b < kMaxBanks; ++b) {
        rig->cfg.analog[1][b].shared = true;
        rig->cfg.analog[1][b].addr = {MidiType::CC, 2, 7};
    }
    rig->start();
    rig->moveTo(0, 0.8f);
    rig->moveTo(1, 0.8f);
    rig->settle();
    rig->eng->setBank(2);
    TEST_ASSERT_FALSE(rig->eng->analog(0).pickedUp);   // banked: bank 2 still 0.5
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, rig->eng->analog(0).stored);
    TEST_ASSERT_TRUE(rig->eng->analog(1).pickedUp);    // shared: follows
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.8f, rig->eng->analog(1).stored);
    rig->hal.clearSent();
    rig->moveTo(1, 0.3f);
    TEST_ASSERT_EQUAL(7, rig->sent().back().number);
    TEST_ASSERT_EQUAL(2, rig->sent().back().channel);
    rig->settle();
    rig->eng->setBank(0);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.3f, rig->eng->analog(1).stored);
    // host CC on a shared address reaches every bank
    rig->hal.injectCC(2, 7, 127);
    rig->run(1);
    rig->eng->setBank(1);
    TEST_ASSERT_TRUE(rig->eng->analog(1).stored == 1.0f);
}

void test_bank_switch_restores_values() {
    rig.reset(new Rig(2));
    for (uint8_t b = 0; b < kMaxBanks; ++b) rig->cfg.analog[0][b].pickup = PickupMode::Jump;
    rig->start();
    rig->moveTo(0, 0.2f);
    rig->settle();
    rig->eng->setBank(1);
    rig->moveTo(0, 0.9f);
    rig->settle();
    TEST_ASSERT_EQUAL(32, rig->sent().back().number);
    rig->eng->setBank(0);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.2f, rig->eng->analog(0).stored);
    TEST_ASSERT_UINT8_WITHIN(1, 25, rig->eng->value7(0));
    rig->eng->setBank(1);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.9f, rig->eng->analog(0).stored);
    TEST_ASSERT_UINT8_WITHIN(1, 114, rig->eng->value7(0));
    rig->eng->setBank(5);   // out of range: ignored
    TEST_ASSERT_EQUAL(1, rig->eng->bank());
}

void test_addr_none_sends_nothing_but_reports() {
    rig.reset(new Rig());
    rig->cfg.analog[2][0].addr = MidiAddress{};
    rig->start();
    rig->moveTo(2, 0.9f);
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());
    AnalogState s = rig->eng->analog(2);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.9f, s.position);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.9f, s.stored);
    TEST_ASSERT_TRUE(s.moving);
    TEST_ASSERT_EQUAL(rawAt(0.9f), s.raw);
}

void test_invert_binding() {
    rig.reset(new Rig());
    rig->cfg.analog[0][0].invert = true;
    rig->base[0] = rawAt(0.2f);
    rig->start();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.8f, rig->eng->analog(0).position);
    rig->moveTo(0, -0.01f);
    TEST_ASSERT_EQUAL(127, rig->sent().back().value);
}

void test_suspend() {
    rig.reset(new Rig());
    const AnalogId pedal = 17;
    rig->start();
    rig->eng->suspend(pedal, true);
    TEST_ASSERT_TRUE(rig->eng->suspended(pedal));
    rig->moveTo(pedal, 1.0f);
    rig->moveTo(pedal, 0.0f);
    rig->moveTo(pedal, 0.9f);
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());
    AnalogState s = rig->eng->analog(pedal);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.9f, s.position);   // still read and filtered
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, s.stored);     // stored untouched
    rig->eng->suspend(pedal, false);
    TEST_ASSERT_FALSE(rig->eng->analog(pedal).pickedUp);
    rig->moveTo(pedal, 0.7f);
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());
    rig->moveTo(pedal, 0.4f);
    TEST_ASSERT_TRUE(rig->sent().size() > 0);
    TEST_ASSERT_EQUAL(17, rig->sent().back().number);
}

void test_calibration_change() {
    rig.reset(new Rig());
    const AnalogId pedal = 18;
    rig->base[pedal] = 2000;
    rig->start();
    const float before = rig->eng->analog(pedal).position;
    rig->eng->setCalibration(pedal, AnalogCal{100, 2000});
    AnalogState s = rig->eng->analog(pedal);
    TEST_ASSERT_TRUE(s.position == 1.0f);          // at once
    TEST_ASSERT_FALSE(s.moving);                   // not a touch
    TEST_ASSERT_FALSE(s.pickedUp);                 // stored is still the old position
    TEST_ASSERT_FLOAT_WITHIN(0.001f, before, s.stored);
    TEST_ASSERT_EQUAL(100, rig->eng->calibration(pedal).rawMin);
    TEST_ASSERT_EQUAL(2000, rig->eng->calibration(pedal).rawMax);
    rig->run(2000);
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());
    // full new travel reaches exact ends
    rig->moveTo(pedal, 0.0f);   // raw 40 < 100
    TEST_ASSERT_EQUAL(0, rig->sent().back().value);
    rig->base[pedal] = 1900;
    rig->moveTo(pedal, 0.5f, 100);   // raw 2047 > 2000
    TEST_ASSERT_EQUAL(127, rig->sent().back().value);
}

void test_update_rate_variation_still_silent() {
    rig.reset(new Rig());
    rig->noiseAmp = 4;
    rig->start();
    for (int i = 0; i < 20000; ++i) rig->tick(200 + uint32_t(noise(900) + 900));
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());
}


// ---------------------------------------------------------------- boot window

void test_boot_first_sample_spike_sends_nothing() {
    // One bad first conversion must not become the stored value (and so a "touch").
    rig.reset(new Rig());
    rig->base[0] = clampRaw(rawAt(0.5f) + 100);
    rig->applyRaw();
    rig->eng.reset(new Engine(rig->hal, rig->cfg));
    rig->eng->update();
    rig->base[0] = rawAt(0.5f);   // real, steady value from now on
    rig->run(1000);
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());
    TEST_ASSERT_TRUE(rig->eng->analog(0).pickedUp);
    TEST_ASSERT_FLOAT_WITHIN(0.002f, 0.5f, rig->eng->analog(0).stored);
}

void test_boot_window_silent_then_honours_calibration_and_held_pads() {
    rig.reset(new Rig());
    const AnalogId p1 = 17, p2 = 18;
    rig->cfg.pads[3][0] = PadBinding{PadAction::Momentary, 0, {MidiType::Note, 1, 40}, 100, 0, false};
    rig->base[p1] = 1050;   // 0.5 under {100, 2000}
    rig->base[p2] = 3000;   // 0.75 under {0, 4000}
    rig->hal.setPad(3, true);          // held at power-up
    rig->hal.injectCC(1, 1, 100);      // host value for knob 1 arrives at once
    rig->applyRaw();
    rig->eng.reset(new Engine(rig->hal, rig->cfg));
    rig->eng->setCalibration(p1, AnalogCal{100, 2000});   // before the first update (/cal.json)
    rig->eng->update();
    for (int i = 0; i < 29; ++i) {
        if (i == 10) rig->eng->setCalibration(p2, AnalogCal{0, 4000});   // during the window
        rig->tick();
    }
    TEST_ASSERT_FALSE(rig->eng->booted());
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());      // nothing sent in the window
    TEST_ASSERT_EQUAL(1, (int)rig->hal.incoming.size()); // and no MIDI read
    rig->tick();                                          // 30 ms: window ends
    TEST_ASSERT_TRUE(rig->eng->booted());
    TEST_ASSERT_EQUAL(1, (int)rig->sent().size());       // the held pad fires once
    TEST_ASSERT_TRUE(rig->sent()[0] == (MidiMsg{MidiType::Note, 1, 40, 100}));
    TEST_ASSERT_FLOAT_WITHIN(0.002f, 0.5f, rig->eng->analog(p1).stored);
    TEST_ASSERT_FLOAT_WITHIN(0.002f, 0.75f, rig->eng->analog(p2).stored);
    TEST_ASSERT_TRUE(rig->eng->analog(p1).pickedUp);
    TEST_ASSERT_TRUE(rig->eng->analog(p2).pickedUp);
    // The host value was applied after the boot values, so it stands.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100 / 127.0f, rig->eng->analog(1).stored);
    TEST_ASSERT_FALSE(rig->eng->analog(1).pickedUp);
    rig->run(1000);
    TEST_ASSERT_EQUAL(1, (int)rig->sent().size());
}

void test_micros_wrap_still_then_move() {
    rig.reset(new Rig());
    rig->hal.setTimeUs(0xFFFFFFFFull - 300000);   // 300 ms before micros() wraps
    rig->noiseAmp = 10;
    rig->start();
    rig->run(1000);
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());
    rig->moveTo(0, 0.75f, 200);
    rig->settle();
    TEST_ASSERT_FALSE(rig->eng->analog(0).moving);
    TEST_ASSERT_TRUE(rig->sent().size() > 10);
}

// ---------------------------------------------------------------- pickup at the end stops

void test_pickup_when_stored_end_is_beyond_pot_travel() {
    // Host sets 127; this pot's top end reads 3900 (calibrated 40..4055), short by ~4%.
    rig.reset(new Rig());
    rig->start();
    rig->hal.injectCC(1, 0, 127);
    rig->run(5);
    TEST_ASSERT_FALSE(rig->eng->analog(0).pickedUp);
    rig->base[0] = 3900 - 200;
    rig->moveTo(0, (3900 - 40) / 4015.0f, 200, 0);
    TEST_ASSERT_TRUE_MESSAGE(rig->eng->analog(0).pickedUp, "knob at its physical maximum never picks up 127");
    TEST_ASSERT_TRUE(rig->sent().size() > 0);
    // Resting there: a new host 127 is picked up at once. Same at the bottom with 0.
    rig->settle();
    rig->hal.injectCC(1, 0, 127);
    rig->run(2);
    TEST_ASSERT_TRUE(rig->eng->analog(0).pickedUp);
    rig->moveTo(0, 0.015f);
    rig->settle();
    rig->hal.injectCC(1, 0, 0);
    rig->run(2);
    TEST_ASSERT_TRUE(rig->eng->analog(0).pickedUp);
    // Away from the ends the ordinary tolerance applies.
    rig->hal.injectCC(1, 0, 20);   // 0.157, knob at 0.015
    rig->run(2);
    TEST_ASSERT_FALSE(rig->eng->analog(0).pickedUp);
}

void test_pickup_tolerance_below_minimum_is_clamped() {
    rig.reset(new Rig());
    rig->cfg.pickupTolerance = 0;
    rig->start();
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, Engine::kMinPickupTolerance, rig->eng->pickupTolerance());
    rig->hal.injectCC(1, 0, 127);
    rig->run(5);
    rig->moveTo(0, (4040 - 40) / 4015.0f, 200);   // 15 LSB short of rawMax
    rig->settle();
    TEST_ASSERT_TRUE(rig->eng->analog(0).pickedUp);
}

// ---------------------------------------------------------------- host value vs the hand

void test_host_cc_after_release_within_settle_is_accepted() {
    // The knob was let go 100 ms ago (still "moving" for the filter's settle time) and sent
    // nothing since: a host change then is not ours and must stand.
    rig.reset(new Rig());
    rig->start();
    rig->moveTo(0, 0.24f, 200, 0);
    rig->run(100);
    TEST_ASSERT_TRUE(rig->eng->analog(0).moving);
    rig->hal.injectCC(1, 0, 100);
    rig->run(5);
    AnalogState s = rig->eng->analog(0);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100 / 127.0f, s.stored);
    TEST_ASSERT_FALSE(s.pickedUp);
    TEST_ASSERT_EQUAL(100, rig->eng->value7(0));
    rig->settle();
    rig->hal.clearSent();
    rig->moveTo(0, 0.27f, 50);   // small nudge: must not jump the host's value
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());
}

void test_host_echo_while_moving_hand_wins_other_value_accepted() {
    rig.reset(new Rig());
    rig->start();
    rig->moveTo(0, 0.6f, 100, 0);
    rig->run(80);   // still "moving", last send 80 ms ago
    TEST_ASSERT_TRUE(rig->eng->analog(0).moving);
    const uint8_t ours = rig->sent().back().value;
    // Within 2 steps of what we sent: our own echo; the hand keeps the value.
    rig->hal.injectCC(1, 0, uint8_t(ours + 2));
    rig->run(2);
    TEST_ASSERT_TRUE(rig->eng->analog(0).pickedUp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.6f, rig->eng->analog(0).stored);
    TEST_ASSERT_EQUAL(ours, rig->eng->value7(0));
    // Far from it: a real host change, taken, and the knob must pick it up again.
    rig->hal.injectCC(1, 0, 5);
    rig->run(2);
    TEST_ASSERT_FALSE(rig->eng->analog(0).pickedUp);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5 / 127.0f, rig->eng->analog(0).stored);
    TEST_ASSERT_EQUAL(5, rig->eng->value7(0));
}

void test_midi_in_at_most_64_per_update() {
    rig.reset(new Rig());
    rig->start();
    for (int i = 0; i < 200; ++i) rig->hal.injectCC(1, 0, uint8_t(i & 127));
    rig->tick();
    TEST_ASSERT_EQUAL(200 - Engine::kMaxMidiPerUpdate, (int)rig->hal.incoming.size());
    rig->tick();
    TEST_ASSERT_EQUAL(200 - 2 * Engine::kMaxMidiPerUpdate, (int)rig->hal.incoming.size());
    rig->run(5);
    TEST_ASSERT_EQUAL(0, (int)rig->hal.incoming.size());
    TEST_ASSERT_EQUAL(199 & 127, rig->eng->value7(0));   // all of them were applied, in order
}

// ---------------------------------------------------------------- end of a suspend

void test_suspend_end_sends_nothing_until_moved_jump() {
    // Cancelling a pedal calibration while the pedal is still "moving" for its filter.
    rig.reset(new Rig());
    const AnalogId pedal = 17;
    for (uint8_t b = 0; b < kMaxBanks; ++b) rig->cfg.analog[pedal][b].pickup = PickupMode::Jump;
    rig->start();
    rig->eng->suspend(pedal, true);
    rig->moveTo(pedal, 0.9f, 100, 20);   // the hand has stopped; the filter still says moving
    TEST_ASSERT_TRUE(rig->eng->analog(pedal).moving);
    rig->eng->suspend(pedal, false);
    rig->run(500);
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());
    TEST_ASSERT_TRUE(rig->eng->analog(pedal).pickedUp);
    rig->moveTo(pedal, 0.95f);   // a new movement: sends its own value
    TEST_ASSERT_TRUE(rig->sent().size() > 0);
    TEST_ASSERT_UINT8_WITHIN(1, 121, rig->sent().back().value);
}

void test_suspend_end_sends_nothing_until_moved_pickup() {
    rig.reset(new Rig());
    const AnalogId pedal = 17;
    rig->start();                       // pickup mode, stored 0.5
    rig->eng->suspend(pedal, true);
    rig->moveTo(pedal, 0.9f, 50, 0);
    rig->moveTo(pedal, 0.51f, 50, 10);  // back near the stored value, still "moving"
    rig->eng->suspend(pedal, false);
    TEST_ASSERT_TRUE(rig->eng->analog(pedal).pickedUp);   // within tolerance
    rig->run(500);
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());
    rig->moveTo(pedal, 0.6f);
    TEST_ASSERT_TRUE(rig->sent().size() > 0);
}

// ---------------------------------------------------------------- pads

static void press(PadId p) { rig->hal.setPad(p, true); rig->tick(); }
static void release(PadId p) { rig->hal.setPad(p, false); rig->tick(); }

void test_pad_momentary() {
    rig.reset(new Rig());
    PadBinding& b = rig->cfg.pads[3][0];
    b.action = PadAction::Momentary;
    b.addr = {MidiType::Note, 10, 36};
    b.onValue = 100;
    b.offValue = 0;
    rig->start();
    press(3);
    rig->run(50);
    TEST_ASSERT_EQUAL(1, (int)rig->sent().size());
    TEST_ASSERT_TRUE(rig->sent()[0] == (MidiMsg{MidiType::Note, 10, 36, 100}));
    TEST_ASSERT_TRUE(rig->eng->pad(3).pressed);
    TEST_ASSERT_TRUE(rig->eng->pad(3).on);
    release(3);
    TEST_ASSERT_EQUAL(2, (int)rig->sent().size());
    TEST_ASSERT_TRUE(rig->sent()[1] == (MidiMsg{MidiType::Note, 10, 36, 0}));
    TEST_ASSERT_FALSE(rig->eng->pad(3).on);
}

void test_pad_momentary_release_after_bank_change_uses_pressed_addr() {
    rig.reset(new Rig());
    rig->cfg.pads[3][0] = PadBinding{PadAction::Momentary, 0, {MidiType::CC, 1, 64}, 127, 0, false};
    rig->cfg.pads[3][1] = PadBinding{PadAction::Momentary, 0, {MidiType::CC, 1, 65}, 127, 0, false};
    rig->start();
    press(3);
    rig->eng->setBank(1);
    release(3);
    TEST_ASSERT_EQUAL(2, (int)rig->sent().size());
    TEST_ASSERT_TRUE(rig->sent()[1] == (MidiMsg{MidiType::CC, 1, 64, 0}));
}

void test_pad_toggle_per_bank_and_shared() {
    rig.reset(new Rig());
    for (uint8_t bk = 0; bk < 2; ++bk) {
        rig->cfg.pads[0][bk] = PadBinding{PadAction::Toggle, 0, {MidiType::CC, 1, uint8_t(80 + bk)}, 127, 0, false};
        rig->cfg.pads[1][bk] = PadBinding{PadAction::Toggle, 0, {MidiType::CC, 1, 90}, 100, 10, true};
    }
    rig->start();
    press(0); release(0);
    press(1); release(1);
    TEST_ASSERT_EQUAL(2, (int)rig->sent().size());
    TEST_ASSERT_TRUE(rig->sent()[0] == (MidiMsg{MidiType::CC, 1, 80, 127}));
    TEST_ASSERT_TRUE(rig->sent()[1] == (MidiMsg{MidiType::CC, 1, 90, 100}));
    TEST_ASSERT_TRUE(rig->eng->pad(0).on);
    TEST_ASSERT_TRUE(rig->eng->pad(1).on);
    TEST_ASSERT_FALSE(rig->eng->pad(0).pressed);
    rig->eng->setBank(1);
    TEST_ASSERT_FALSE(rig->eng->pad(0).on);   // per bank
    TEST_ASSERT_TRUE(rig->eng->pad(1).on);    // shared
    press(0); release(0);
    TEST_ASSERT_TRUE(rig->sent().back() == (MidiMsg{MidiType::CC, 1, 81, 127}));
    press(1); release(1);
    TEST_ASSERT_TRUE(rig->sent().back() == (MidiMsg{MidiType::CC, 1, 90, 10}));
    TEST_ASSERT_FALSE(rig->eng->pad(1).on);
    rig->eng->setBank(0);
    TEST_ASSERT_TRUE(rig->eng->pad(0).on);
    TEST_ASSERT_FALSE(rig->eng->pad(1).on);
    press(0); release(0);
    TEST_ASSERT_TRUE(rig->sent().back() == (MidiMsg{MidiType::CC, 1, 80, 0}));
}

void test_pad_bank_select_next_prev() {
    rig.reset(new Rig(3));
    for (uint8_t bk = 0; bk < kMaxBanks; ++bk) {
        rig->cfg.pads[16][bk] = PadBinding{PadAction::BankSelect, 2, {}, 127, 0, false};
        rig->cfg.pads[17][bk] = PadBinding{PadAction::BankNext, 0, {}, 127, 0, false};
        rig->cfg.pads[18][bk] = PadBinding{PadAction::BankPrev, 0, {}, 127, 0, false};
        rig->cfg.pads[19][bk] = PadBinding{PadAction::BankSelect, 5, {}, 127, 0, false};   // >= numBanks
    }
    rig->start();
    TEST_ASSERT_EQUAL(0, rig->eng->bank());
    TEST_ASSERT_FALSE(rig->eng->pad(16).on);
    press(16); release(16);
    TEST_ASSERT_EQUAL(2, rig->eng->bank());
    TEST_ASSERT_TRUE(rig->eng->pad(16).on);
    press(17);
    TEST_ASSERT_EQUAL(0, rig->eng->bank());   // wraps
    TEST_ASSERT_TRUE(rig->eng->pad(17).on);   // held
    rig->run(100);
    TEST_ASSERT_EQUAL(0, rig->eng->bank());   // one step per press
    release(17);
    TEST_ASSERT_FALSE(rig->eng->pad(16).on);
    press(18); release(18);
    TEST_ASSERT_EQUAL(2, rig->eng->bank());   // wraps back
    press(18); release(18);
    TEST_ASSERT_EQUAL(1, rig->eng->bank());
    press(19); release(19);
    TEST_ASSERT_EQUAL(1, rig->eng->bank());   // ignored
    TEST_ASSERT_FALSE(rig->eng->pad(19).on);
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());
}

void test_bank_pads_with_send_send_on_press() {
    rig.reset(new Rig(3));
    for (uint8_t bk = 0; bk < kMaxBanks; ++bk) {
        rig->cfg.pads[16][bk] = PadBinding{PadAction::BankSelect, 2, {MidiType::CC, 1, uint8_t(100 + bk)}, 99, 0, false};
        rig->cfg.pads[17][bk] = PadBinding{PadAction::BankNext, 0, {MidiType::CC, 1, 110}, 127, 0, false};
        rig->cfg.pads[18][bk] = PadBinding{PadAction::BankPrev, 0, {MidiType::Note, 2, 60}, 127, 0, false};
        rig->cfg.pads[19][bk] = PadBinding{PadAction::BankSelect, 5, {MidiType::CC, 1, 120}, 127, 0, false};
    }
    rig->start();
    press(16);
    TEST_ASSERT_EQUAL(2, rig->eng->bank());
    TEST_ASSERT_EQUAL(1, (int)rig->sent().size());
    TEST_ASSERT_TRUE(rig->sent()[0] == (MidiMsg{MidiType::CC, 1, 100, 99}));   // bank 1's address
    release(16);
    TEST_ASSERT_EQUAL(1, (int)rig->sent().size());                              // nothing on release
    press(17); release(17);
    TEST_ASSERT_EQUAL(0, rig->eng->bank());
    TEST_ASSERT_TRUE(rig->sent().back() == (MidiMsg{MidiType::CC, 1, 110, 127}));
    press(18); release(18);
    TEST_ASSERT_EQUAL(2, rig->eng->bank());
    TEST_ASSERT_TRUE(rig->sent().back() == (MidiMsg{MidiType::Note, 2, 60, 127}));
    TEST_ASSERT_EQUAL(3, (int)rig->sent().size());
    press(19); release(19);   // bank 6 does not exist: ignored, nothing sent
    TEST_ASSERT_EQUAL(2, rig->eng->bank());
    TEST_ASSERT_EQUAL(3, (int)rig->sent().size());
}

void test_pad_none_and_single_bank_next() {
    rig.reset(new Rig(1));
    rig->cfg.pads[5][0].action = PadAction::BankNext;
    rig->start();
    press(4); release(4);   // None
    press(5); release(5);
    TEST_ASSERT_EQUAL(0, rig->eng->bank());
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());
}

void test_pad_midi_in() {
    rig.reset(new Rig());
    rig->cfg.pads[0][0] = PadBinding{PadAction::Toggle, 0, {MidiType::Note, 1, 40}, 127, 0, false};
    rig->cfg.pads[1][0] = PadBinding{PadAction::Momentary, 0, {MidiType::CC, 5, 20}, 127, 0, false};
    rig->cfg.pads[1][1] = PadBinding{PadAction::Momentary, 0, {MidiType::CC, 5, 21}, 127, 0, false};
    rig->start();
    rig->hal.injectNote(1, 40, 127);
    rig->hal.injectCC(5, 20, 64);
    rig->hal.injectCC(5, 21, 127);   // bank 1's address
    rig->run(1);
    TEST_ASSERT_EQUAL(0, (int)rig->sent().size());   // no echo
    TEST_ASSERT_TRUE(rig->eng->pad(0).on);
    TEST_ASSERT_TRUE(rig->eng->pad(0).midiValue == 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 64 / 127.0f, rig->eng->pad(1).midiValue);
    TEST_ASSERT_FALSE(rig->eng->pad(1).on);   // momentary on = held
    // pressing the toggle now turns it off
    press(0);
    TEST_ASSERT_TRUE(rig->sent().back() == (MidiMsg{MidiType::Note, 1, 40, 0}));
    TEST_ASSERT_FALSE(rig->eng->pad(0).on);
    rig->hal.injectNote(1, 40, 90);
    rig->run(1);
    TEST_ASSERT_TRUE(rig->eng->pad(0).on);
    rig->hal.injectNote(1, 40, 0);   // note off
    rig->run(1);
    TEST_ASSERT_FALSE(rig->eng->pad(0).on);
    rig->eng->setBank(1);
    TEST_ASSERT_TRUE(rig->eng->pad(1).midiValue == 1.0f);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_filter_noise_still_never_moves);
    RUN_TEST(test_filter_timing_jitter_still);
    RUN_TEST(test_filter_step_response_fast);
    RUN_TEST(test_filter_exact_end_stops_and_settle);
    RUN_TEST(test_filter_push_onto_end_stop_from_rest);
    RUN_TEST(test_filter_rate_independent);
    RUN_TEST(test_filter_invert_and_calibration);
    RUN_TEST(test_boot_sends_nothing_noisy_for_seconds);
    RUN_TEST(test_sweep_sends_each_value_once_to_exact_ends);
    RUN_TEST(test_seven_bit_boundary_no_chatter);
    RUN_TEST(test_slow_move_inside_settle_window_tracks);
    RUN_TEST(test_pickup_from_above);
    RUN_TEST(test_pickup_from_below);
    RUN_TEST(test_pickup_by_jumping_over_stored_value);
    RUN_TEST(test_pickup_within_tolerance_is_immediate);
    RUN_TEST(test_host_cc_sets_stored_and_requires_pickup);
    RUN_TEST(test_host_cc_near_position_stays_picked_up);
    RUN_TEST(test_host_cc_other_bank_applies_on_switch);
    RUN_TEST(test_host_note_sets_stored_for_note_binding);
    RUN_TEST(test_host_cc_while_moving_hand_wins);
    RUN_TEST(test_jump_mode);
    RUN_TEST(test_jump_mode_after_host_cc);
    RUN_TEST(test_shared_vs_banked_stored_values);
    RUN_TEST(test_bank_switch_restores_values);
    RUN_TEST(test_addr_none_sends_nothing_but_reports);
    RUN_TEST(test_invert_binding);
    RUN_TEST(test_suspend);
    RUN_TEST(test_calibration_change);
    RUN_TEST(test_update_rate_variation_still_silent);
    RUN_TEST(test_boot_first_sample_spike_sends_nothing);
    RUN_TEST(test_boot_window_silent_then_honours_calibration_and_held_pads);
    RUN_TEST(test_micros_wrap_still_then_move);
    RUN_TEST(test_pickup_when_stored_end_is_beyond_pot_travel);
    RUN_TEST(test_pickup_tolerance_below_minimum_is_clamped);
    RUN_TEST(test_host_cc_after_release_within_settle_is_accepted);
    RUN_TEST(test_host_echo_while_moving_hand_wins_other_value_accepted);
    RUN_TEST(test_midi_in_at_most_64_per_update);
    RUN_TEST(test_suspend_end_sends_nothing_until_moved_jump);
    RUN_TEST(test_suspend_end_sends_nothing_until_moved_pickup);
    RUN_TEST(test_pad_momentary);
    RUN_TEST(test_pad_momentary_release_after_bank_change_uses_pressed_addr);
    RUN_TEST(test_pad_toggle_per_bank_and_shared);
    RUN_TEST(test_pad_bank_select_next_prev);
    RUN_TEST(test_bank_pads_with_send_send_on_press);
    RUN_TEST(test_pad_none_and_single_bank_next);
    RUN_TEST(test_pad_midi_in);
    return UNITY_END();
}
