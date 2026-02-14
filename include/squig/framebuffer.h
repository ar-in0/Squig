#ifndef FRAMEBUFFER_H_
#define FRAMEBUFFER_H_

#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <vector>

// constexpr: evaluated at compile time. The compiler substitutes the value
// directly -- no runtime variable, no storage. Preferred over #define for
// typed constants (type-safe, scoped, debuggable).
constexpr size_t kRingBufferSize{6};

template <std::movable T>
class FrameBuffer {
   public:
    // explicit: prevents implicit conversion. Without it,
    // FrameBuffer<T> fb = 6; would compile (size_t -> FrameBuffer).
    // Rule: single-argument constructors should almost always be explicit.
    explicit FrameBuffer(size_t bufSize) : m_buffer(bufSize) {}

    void push(T frame) {
        std::unique_lock lock(m_mutex);
        m_bufReady.wait(lock, [this] { return m_count < m_buffer.size(); });
        m_buffer[m_head] = std::move(frame);
        m_head = (m_head + 1) % m_buffer.size();
        m_count++;
        m_bufReady.notify_one();
    }

    // Returns by value via move. For RTMPMediaMessage
    // this transfers the vector pointer (O(1)), no payload copy.
    // Avoids the reference-invalidation bug from buffer wraparound.
    // [[nodiscard]]: compiler warning if caller ignores the return value.
    // pop() moves the element out and decrements count -- discarding the
    // result is always a bug (data consumed, no way to retrieve it).
    [[nodiscard]] T pop() {
        std::unique_lock lock(m_mutex);
        m_bufReady.wait(lock, [this] { return m_count > 0; });
        T val = std::move(m_buffer[m_tail]);
        m_tail = (m_tail + 1) % m_buffer.size();
        m_count--;
        m_bufReady.notify_one();
        return val;
    }

   private:
    std::vector<T> m_buffer;

    // value-initialization with {}: these are zero-initialized.
    // Without the braces, built-in types like size_t are left
    // indeterminate (reading them before assignment is UB).
    size_t m_head{};
    size_t m_tail{};
    size_t m_count{};

    std::mutex m_mutex;
    std::condition_variable m_bufReady;
};

#endif  // FRAMEBUFFER_H_
