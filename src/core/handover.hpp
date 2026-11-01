#pragma once

/// @file
/// @ingroup core
/// The handover between the logic thread and the main thread (design doc 0002 §5): one object
/// with three channels behind one mutex, held only to swap pointers and vectors. Also the main
/// thread's side of the state channel (Display) and of the event channel (PlayDue()).

#include "core/assert.hpp"
#include "core/profile.hpp"
#include "core/thread_safety.hpp"
#include "core/tick_clock.hpp"
#include "core/types.hpp"

#include <array>
#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

namespace core {

/// The three channels between the threads. State (logic to main) is newest-wins: a snapshot may be
/// skipped, since each one is complete. Events (logic to main) and input (main to logic)
/// accumulate: none is lost or delivered twice, and each arrives in the order sent.
///
/// The state channel rotates four `State` buffers: one the logic writes, one finished and waiting,
/// and up to two the main thread holds (one taken but not yet due, one drawn). Buffers and
/// vectors keep their capacity, so the steady state allocates nothing.
template<typename State, typename Event, typename Input>
class Handover {
public:
    /// The number of `State` buffers.
    static constexpr std::size_t BUFFERS = 4;

    Handover() {
        for (std::size_t i = 1; i < BUFFERS; ++i) {
            m_free[m_freeCount++] = &m_states[i];
        }
    }

    Handover(const Handover&) = delete;
    Handover& operator=(const Handover&) = delete;
    Handover(Handover&&) = delete;
    Handover& operator=(Handover&&) = delete;
    ~Handover() = default;

    /// Logic thread: the buffer to fill for the next Publish(). It held a snapshot several
    /// publishes ago, so the caller overwrites all of it.
    [[nodiscard]] State& Writing() {
        return *m_writing;
    }

    /// Logic thread: makes Writing() the newest snapshot, replacing one the main thread hasn't
    /// taken, and appends `events` to the event channel, leaving `events` empty.
    void Publish(std::vector<Event>& events) PN_REQUIRES(!m_mutex) {
        PN_PROFILE_ZONE();
        const std::scoped_lock lock{ m_mutex };
        if (m_ready != nullptr) {
            m_free[m_freeCount++] = m_ready;
        }
        m_ready = std::exchange(m_writing, PopFree());
        if (m_events.empty()) {
            std::swap(m_events, events);
        }
        else {
            // The main thread hasn't drained the last ones: the rare path that copies under the lock.
            m_events.insert(m_events.end(), events.begin(), events.end());
            events.clear();
        }
    }

    /// Logic thread: moves the input sent since the last call into `into`, replacing its contents.
    void TakeInput(std::vector<Input>& into) PN_REQUIRES(!m_mutex) {
        PN_PROFILE_ZONE();
        into.clear();
        const std::scoped_lock lock{ m_mutex };
        std::swap(into, m_input);
    }

    /// Main thread: sends `input` to the logic thread.
    void SendInput(Input input) PN_REQUIRES(!m_mutex) {
        const std::scoped_lock lock{ m_mutex };
        m_input.push_back(std::move(input));
    }

    /// Main thread: the newest snapshot not taken yet, or null. The main thread owns it until it
    /// gives it back with ReturnState(), and holds at most two at once.
    [[nodiscard]] State* TakeState() PN_REQUIRES(!m_mutex) {
        PN_PROFILE_ZONE();
        const std::scoped_lock lock{ m_mutex };
        return std::exchange(m_ready, nullptr);
    }

    /// Main thread: gives back a snapshot from TakeState() that it no longer draws.
    void ReturnState(State* state) PN_REQUIRES(!m_mutex) {
        PN_PROFILE_ZONE();
        const std::scoped_lock lock{ m_mutex };
        m_free[m_freeCount++] = state;
    }

    /// Main thread: moves the events published since the last call into `into`, replacing its
    /// contents.
    void TakeEvents(std::vector<Event>& into) PN_REQUIRES(!m_mutex) {
        PN_PROFILE_ZONE();
        into.clear();
        const std::scoped_lock lock{ m_mutex };
        std::swap(into, m_events);
    }

private:
    State* PopFree() PN_REQUIRES(m_mutex) {
        PN_ASSERT(m_freeCount > 0, "the main thread holds more than two snapshots");
        return m_free[--m_freeCount];
    }

    Mutex m_mutex{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::array<State, BUFFERS> m_states{};
    std::array<State*, BUFFERS> m_free PN_GUARDED_BY(m_mutex){};
    std::size_t m_freeCount PN_GUARDED_BY(m_mutex) = 0;
    State* m_writing = m_states.data(); // the logic thread's alone
    State* m_ready PN_GUARDED_BY(m_mutex) = nullptr;
    std::vector<Event> m_events PN_GUARDED_BY(m_mutex){};
    std::vector<Input> m_input PN_GUARDED_BY(m_mutex){};
};

/// The main thread's side of the state channel: the snapshot it draws, and a newer one it has
/// taken but that isn't due yet. A snapshot is adopted (drawn) only once it is due (IsDue()), so
/// the picture moves from one snapshot into the next without judder. `State` has a
/// `u64 scheduledNs` and the `u64 publishedNs` the logic thread published it at.
template<typename State, typename Event, typename Input>
class Display {
public:
    /// Adopts the held snapshot if it is due at `nowNs`; then, with none held, takes the newest
    /// waiting one and adopts it too if due. Returns whether a new snapshot was adopted (to upload).
    bool Update(Handover<State, Event, Input>& handover, u64 nowNs, u64 delayNs) {
        PN_PROFILE_ZONE();
        bool adopted = false;
        if (m_held != nullptr && IsDue(m_held->scheduledNs, nowNs, delayNs)) {
            Adopt(handover);
            adopted = true;
        }
        if (m_held == nullptr) {
            m_held = handover.TakeState();
            if (m_held != nullptr && IsDue(m_held->scheduledNs, m_held->publishedNs, delayNs)) {
                ++m_lateAdoptions;
            }
            if (m_held != nullptr && IsDue(m_held->scheduledNs, nowNs, delayNs)) {
                Adopt(handover);
                adopted = true;
            }
        }
        return adopted;
    }

    /// The snapshot to draw, or null before the first one is due.
    [[nodiscard]] const State* Drawn() const {
        return m_drawn;
    }

    /// Snapshots published after they were due: their tick took longer than the display delay.
    [[nodiscard]] u64 LateAdoptions() const {
        return m_lateAdoptions;
    }

private:
    void Adopt(Handover<State, Event, Input>& handover) {
        if (m_drawn != nullptr) {
            handover.ReturnState(m_drawn);
        }
        m_drawn = std::exchange(m_held, nullptr);
    }

    State* m_drawn = nullptr;
    State* m_held = nullptr;
    u64 m_lateAdoptions = 0;
};

/// How far behind the render clock a cosmetic event may fall before it is dropped, not played.
inline constexpr f64 STALE_EVENT_TICKS = 30.0;

/// Plays, in order, the `pending` events of the drawn snapshot's `epoch` whose tick the render
/// clock has reached (RenderTick()), and removes them; drops events of older epochs, and cosmetic
/// ones more than STALE_EVENT_TICKS behind the clock. Events of later ticks or newer epochs stay.
/// `Event` has `u64 tick`, `u64 epoch` and `bool cosmetic`; `play` takes a `const Event&`.
template<typename Event, typename Play>
void PlayDue(std::vector<Event>& pending, u64 epoch, f64 renderTick, Play play) {
    PN_PROFILE_ZONE();
    std::size_t kept = 0;
    for (Event& event : pending) {
        const auto tick = static_cast<f64>(event.tick);
        if (event.epoch > epoch || (event.epoch == epoch && tick > renderTick)) {
            if (&pending[kept] != &event) { // a self-move could empty the event's contents
                pending[kept] = std::move(event);
            }
            ++kept;
        }
        else if (event.epoch == epoch && !(event.cosmetic && renderTick - tick > STALE_EVENT_TICKS)) {
            play(std::as_const(event));
        }
    }
    pending.resize(kept);
}

} // namespace core
