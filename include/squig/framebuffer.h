#ifndef FRAMEBUFFER_H_
#define FRAMEBUFFER_H_
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <vector>
// every client operates 2 Framebuffers:
// an RTMPBuf and a CVBuf
namespace Squig {
constexpr int kRingBufferSize{6};
}  // namespace Squig

// ringbuffer: overwrite elements on wraparound
// push:
// buf[] = frame
// adjust m_head, m_tail i.e element returned on pop()
// FIFO Queue
template <typename T>
class FrameBuffer {
   public:
    FrameBuffer(size_t bufSize = kRingBufferSize) : m_buffer(bufSize) {}

    // There may be a race on the m_count
    // if threads A and B push and pop simultaneously
    // non-deterministic due to thread instruction interleaving.
    void push(const T& frame) {
        // can be unlocked by condition variable.
        std::unique_lock<std::mutex> lock(m_mutex);

        // [this] -> predicate can acess variables
        // of the 'this' scope
        m_bufReady.wait(lock, [this]() {
            return m_count < m_buffer.size();
        });  // sleep unti m_count < buffersize
        // increment m_head but returns old value
        m_buffer[m_head++] = frame;
        m_head = m_head % m_buffer.size();
        m_count++;
        m_bufReady.notify_one();
    }
    // Risk with returning a reference:
    // after a pop by decode thread B,
    // if reference isnt used before the queue
    // wraps around with new RTMP messages by thread A push(), the
    // reference that B owns will be corrupted.
    // Fix:
    // 1. Ensure decoder thread uses the reference before m_buffer.size()
    // rtmp messages are recd over the network (fast decoder thread)
    // OR
    // 2. Return by value (overhead, but safer)
    // For squig, it likely that the decode of the RTMPMediaMessage
    // reffered to by pop() will be used up immediately by the
    // ffmpeg codecs. (1. should be safe)
    // --
    // Cannot return const, pNALUtoAnnexB() needs to modify message data
    T& pop() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_bufReady.wait(lock, [this]() {
            return m_count > 0;
        });  // wait until m_count > 0

        T& f = m_buffer[m_tail++];
        m_tail = m_tail % m_buffer.size();
        m_count--;
        m_bufReady.notify_one();
        return f;
    }  // lock_guard() unlocks here, after returning ref. For condvar, use
       // unique_lock()

   private:
    std::vector<T> m_buffer;

    size_t m_head{};  // where to place a new frame. m_head - 1 is index of
                      // last-written frame.
    size_t m_tail{};  // earliest consumed frame

    // need to track #elems in the queue
    // If queue is full, should block writer (push) thread
    // If queue is empty, should block reader (pop) thread
    // head=tail may be queue full or queue empty.
    // So track seperately
    size_t m_count{};

    std::mutex m_mutex;

    // How to block while holding a mutex?
    // if (mcount == 0/m_buffer.size()) {
    // - unlock mutex (so push/pop can fill/empty)
    // - Other thread (earlier blocked on mutex)
    // can pop/push
    // ^ above is spin waiting, too much CPU, better to use
    // condition variables - signal the other thread
    // exactly when the necessary condition is satisfied.
    // --
    // Condition variable wait(predicate):
    // - unlock mutex
    // - put calling thread to sleep
    // when other thread does signal():
    // - lock the mutex, check the predicate
    // - wake up if predicate satisfied.
    std::condition_variable m_bufReady;
}

#endif  // FRAMEBUFFER_H_
