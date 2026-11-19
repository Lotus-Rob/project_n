#pragma once

/// @file
/// @ingroup core
/// The handover between the logic thread and the main thread: one object
/// with three channels behind one mutex, held only to swap pointers and vectors. Also the main
/// thread's side of the state channel (Display) and of the event channel (PlayDue()).

#include "core/assert.hpp"
#include "core/profile.hpp"
#include "core/thread_safety.hpp"
#include "core/tick_clock.hpp"
#include "core/types.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace core {

/// How far behind the render clock a cosmetic event may fall before it is dropped, not played.
inline constexpr f64 STALE_EVENT_TICKS = 30.0;

/// The channels between the threads. State (logic to main) keeps the two newest finished snapshots
/// waiting, in the order published: a third drops the oldest, which is harmless, since each one is
/// complete. Events (logic to main) and input (main to logic) accumulate: none is lost or delivered
/// twice, and each arrives in the order sent. Held input (main to logic) is newest-wins: a value the
/// logic reads as often as it likes. So is the view (main to logic), of which there is none until
/// the first is sent.
///
/// The state channel rotates five `State` buffers: one the logic writes, up to two finished and
/// waiting, and up to two the main thread holds (one taken but not yet due, one drawn). Buffers and
/// vectors keep their capacity, so the steady state allocates nothing.
template<typename State, typename Event, typename Input, typename Held, typename View>
class Handover {
public:
    /// The most finished snapshots waiting for the main thread to take them.
    static constexpr std::size_t WAITING = 2;
    /// The number of `State` buffers.
    static constexpr std::size_t BUFFERS = 5;

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

    /// Logic thread: makes Writing() the newest waiting snapshot, first dropping the oldest when WAITING snapshots
    /// already wait, and appends `events` to the event channel, leaving `events` empty. While the main thread hasn't
    /// taken the earlier events, drops the cosmetic ones of the newest epoch PlayDue() would drop anyway: more than
    /// STALE_EVENT_TICKS + MAX_CATCH_UP + 1 behind the newest, since the render clock reaches at least the older
    /// waiting snapshot's tick minus one before it reaches them, and that snapshot is at most a burst of MAX_CATCH_UP
    /// ticks behind the newest.
    void Publish(std::vector<Event>& events) PN_REQUIRES(!m_mutex) {
        PN_PROFILE_ZONE();
        const std::scoped_lock lock{ m_mutex };
        if (m_waitingCount == WAITING) {
            m_free[m_freeCount++] = PopWaiting();
        }
        m_waiting[m_waitingCount++] = std::exchange(m_writing, PopFree());
        if (m_events.empty()) {
            std::swap(m_events, events);
        }
        else {
            // The main thread hasn't drained the last ones: the rare path that copies under the lock. Cosmetic events
            // PlayDue() would drop are dropped here, so a stalled main thread doesn't grow the queue. The snapshots it
            // holds are older than these events, so the render clock (RenderTick()) first reaches them by adopting a
            // waiting snapshot: at worst the older of two, a burst of up to MAX_CATCH_UP ticks behind the newest,
            // drawn from its tick minus one. So those more than STALE_EVENT_TICKS + MAX_CATCH_UP + 1 behind the
            // newest can't play. An older epoch's are kept: its snapshot may still be drawn, and nothing adds to them
            // once a newer epoch has begun. Other events still accumulate; their coalescing arrives with their kinds.
            m_events.insert(m_events.end(), events.begin(), events.end());
            events.clear();
            const u64 epoch = m_events.back().epoch;
            const auto tick = static_cast<f64>(m_events.back().tick);
            std::erase_if(m_events, [epoch, tick](const Event& event) {
                return event.cosmetic && event.epoch == epoch && tick - static_cast<f64>(event.tick) > STALE_MARGIN_TICKS;
            });
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

    /// Main thread: replaces the held input with `held`.
    void SendHeld(const Held& held) PN_REQUIRES(!m_mutex) {
        const std::scoped_lock lock{ m_mutex };
        m_held = held;
    }

    /// Logic thread: the held input the main thread sent last (a default one before any). Reading
    /// doesn't consume it.
    [[nodiscard]] Held ReadHeld() PN_REQUIRES(!m_mutex) {
        const std::scoped_lock lock{ m_mutex };
        return m_held;
    }

    /// Main thread: replaces the view with `view`.
    void SendView(const View& view) PN_REQUIRES(!m_mutex) {
        const std::scoped_lock lock{ m_mutex };
        m_view = view;
    }

    /// Logic thread: the view the main thread sent last, or none before the first. Reading doesn't consume it.
    [[nodiscard]] std::optional<View> ReadView() const PN_REQUIRES(!m_mutex) {
        const std::scoped_lock lock{ m_mutex };
        return m_view;
    }

    /// Main thread: the oldest waiting snapshot, or null when none waits. The main thread owns it
    /// until it gives it back with ReturnState(), and holds at most two at once.
    [[nodiscard]] State* TakeState() PN_REQUIRES(!m_mutex) {
        PN_PROFILE_ZONE();
        const std::scoped_lock lock{ m_mutex };
        return m_waitingCount == 0 ? nullptr : PopWaiting();
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
    // How far behind the newest event a cosmetic one of its epoch may be and still play.
    static constexpr f64 STALE_MARGIN_TICKS = STALE_EVENT_TICKS + static_cast<f64>(MAX_CATCH_UP) + 1.0;

    State* PopFree() PN_REQUIRES(m_mutex) {
        PN_ASSERT(m_freeCount > 0, "the main thread holds more than two snapshots");
        return m_free[--m_freeCount];
    }

    State* PopWaiting() PN_REQUIRES(m_mutex) {
        State* oldest = m_waiting[0];
        std::shift_left(m_waiting.begin(), m_waiting.end(), 1);
        --m_waitingCount;
        return oldest;
    }

    mutable Mutex m_mutex;
    std::array<State, BUFFERS> m_states{};
    std::array<State*, BUFFERS> m_free PN_GUARDED_BY(m_mutex){};
    std::size_t m_freeCount PN_GUARDED_BY(m_mutex) = 0;
    State* m_writing = m_states.data();                             // the logic thread's alone
    std::array<State*, WAITING> m_waiting PN_GUARDED_BY(m_mutex){}; // oldest first
    std::size_t m_waitingCount PN_GUARDED_BY(m_mutex) = 0;
    std::vector<Event> m_events PN_GUARDED_BY(m_mutex);
    std::vector<Input> m_input PN_GUARDED_BY(m_mutex);
    Held m_held PN_GUARDED_BY(m_mutex){};
    std::optional<View> m_view PN_GUARDED_BY(m_mutex) = std::nullopt;
};

/// The main thread's side of the state channel: the snapshot it draws, and a newer one it has
/// taken but that isn't due yet. A snapshot is adopted (drawn) only once it is due (IsDue()), so
/// the picture moves from one snapshot into the next without judder; of several due at once the
/// newest is adopted and the older ones go back undrawn. `State` has a `u64 scheduledNs`, the
/// `u64 publishedNs` the logic thread published it at, and `bool advanced`, whether the simulation
/// ran in its tick.
template<typename State, typename Event, typename Input, typename Held, typename View>
class Display {
public:
    /// Until it holds a snapshot not due at `nowNs` or none waits: adopts the held snapshot if due,
    /// then with none held takes the oldest waiting one. So it adopts the newest due snapshot and
    /// holds the first that isn't due. Returns whether a new snapshot was adopted (to upload).
    bool Update(Handover<State, Event, Input, Held, View>& handover, u64 nowNs, u64 delayNs) {
        PN_PROFILE_ZONE();
        m_takenCount = 0;
        m_takenRead = 0;
        bool adopted = false;
        // Taking at most as many as can wait at once leaves room for their latenesses, and a logic thread that
        // publishes meanwhile can't keep one frame taking.
        for (std::size_t taken = 0;; ++taken) {
            if (m_held != nullptr) {
                if (!IsDue(m_held->scheduledNs, nowNs, delayNs)) {
                    break;
                }
                Adopt(handover);
                adopted = true;
            }
            if (taken == WAITING) {
                break;
            }
            m_held = handover.TakeState();
            if (m_held == nullptr) {
                break;
            }
            Took(*m_held, delayNs);
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

    /// How long after its scheduled time a snapshot the last Update() took was published (0 for one published
    /// before it): each call gives the next of those whose tick advanced, oldest first, once each, then none.
    [[nodiscard]] std::optional<u64> TakenLateness() {
        if (m_takenRead == m_takenCount) {
            return std::nullopt;
        }
        return m_takenLateness[m_takenRead++];
    }

private:
    static constexpr std::size_t WAITING = Handover<State, Event, Input, Held, View>::WAITING;

    void Took(const State& state, u64 delayNs) {
        if (IsDue(state.scheduledNs, state.publishedNs, delayNs)) {
            ++m_lateAdoptions;
        }
        // A frozen tick's snapshot shows what the one before it ends on, so its lateness can't be seen; and the
        // stalls (a month, a save, a load) publish one only once they end, as late as the stall was long.
        if (state.advanced) {
            m_takenLateness[m_takenCount++] = state.publishedNs > state.scheduledNs ? state.publishedNs - state.scheduledNs : 0;
        }
    }

    void Adopt(Handover<State, Event, Input, Held, View>& handover) {
        if (m_drawn != nullptr) {
            handover.ReturnState(m_drawn);
        }
        m_drawn = std::exchange(m_held, nullptr);
    }

    State* m_drawn = nullptr;
    State* m_held = nullptr;
    u64 m_lateAdoptions = 0;
    std::array<u64, WAITING> m_takenLateness{};
    std::size_t m_takenCount = 0; // latenesses the last Update() recorded
    std::size_t m_takenRead = 0;  // of those, the ones TakenLateness() gave
};

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
