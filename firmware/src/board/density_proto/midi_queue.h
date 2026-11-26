// Outgoing MIDI queue between Hal::midiSend and the USB TX FIFO (16 packets). Portable: no
// Arduino or SDK headers, so it can be checked natively. Fixed size, no heap.
//
// Policy when the queue is full:
//   - a control change replaces the OLDEST queued CC with the same channel and number (that
//     one is superseded by the new value); if there is none, the new CC is dropped.
//   - a note off (Note with value 0) is never lost to a CC: if no superseded CC exists, the
//     oldest CC of any kind is dropped to make room. Only a queue full of notes drops it.
//   - a note on is dropped.
// Messages keep their order. dropped() counts messages lost (the new one, or a CC pushed out
// for a note off); superseded() counts CCs replaced by a newer value for the same control.
#pragma once
#include <cstdint>
#include "density/types.h"

namespace density {
namespace proto {

template <uint16_t N>
class MidiQueue {
    static_assert(N > 0, "queue needs room");

public:
    bool empty() const { return count_ == 0; }
    uint16_t size() const { return count_; }
    static constexpr uint16_t capacity() { return N; }
    const MidiMsg& front() const { return buf_[head_]; }
    void pop() {
        if (!count_) return;
        head_ = uint16_t((head_ + 1) % N);
        --count_;
    }
    void clear() { head_ = count_ = 0; }

    // Returns true if m was queued (possibly after removing an older message).
    bool push(const MidiMsg& m) {
        if (count_ == N) {
            if (m.type == MidiType::CC) {
                const int i = findOldest([&](const MidiMsg& q) {
                    return q.type == MidiType::CC && q.channel == m.channel && q.number == m.number;
                });
                if (i < 0) {
                    ++dropped_;
                    return false;
                }
                removeAt(uint16_t(i));
                ++superseded_;
            } else if (m.type == MidiType::Note && m.value == 0) {
                const int i = findOldest([](const MidiMsg& q) { return q.type == MidiType::CC; });
                if (i < 0) {
                    ++dropped_;
                    return false;
                }
                removeAt(uint16_t(i));
                ++dropped_;   // that CC is lost, not superseded
            } else {
                ++dropped_;
                return false;
            }
        }
        buf_[(head_ + count_) % N] = m;
        ++count_;
        return true;
    }

    uint32_t dropped() const { return dropped_; }
    uint32_t superseded() const { return superseded_; }
    void countDropped(uint32_t n = 1) { dropped_ += n; }   // losses outside the queue

private:
    template <typename Pred>
    int findOldest(Pred pred) const {
        for (uint16_t k = 0; k < count_; ++k)
            if (pred(buf_[(head_ + k) % N])) return k;
        return -1;
    }
    // k = position from the front; later messages move up one place, order kept.
    void removeAt(uint16_t k) {
        for (uint16_t j = k; j + 1 < count_; ++j) buf_[(head_ + j) % N] = buf_[(head_ + j + 1) % N];
        --count_;
    }

    MidiMsg buf_[N] = {};
    uint16_t head_ = 0;
    uint16_t count_ = 0;
    uint32_t dropped_ = 0;
    uint32_t superseded_ = 0;
};

}  // namespace proto
}  // namespace density
