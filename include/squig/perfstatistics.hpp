#ifndef PERFSTATISTICS_H_
#define PERFSTATISTICS_H_
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <vector>

class PerfStatistics {
   private:
    uint64_t m_iMaxTime;
    uint64_t m_iMinTime;
    std::vector<uint64_t> m_allTimes{};

    uint64_t m_iTprev;
    std::vector<uint64_t> m_imshowTimes{};  // used to store time interval b/w
                                            // successive cv::imshow.
   public:
    PerfStatistics(uint64_t tStartMs) {
        m_iMaxTime = 0;
        m_iMinTime = std::numeric_limits<uint64_t>::max();  // v large
        m_iTprev = tStartMs;
    }

    void updateImshowTime(uint64_t now) {
        m_imshowTimes.push_back(now - m_iTprev);
        m_iTprev = now;
    }
    // Class method with definition is implicitly inline.
    // In general, inline is just a compiler hint.
    void update(uint64_t duration) {
        if (duration < m_iMinTime) {
            m_iMinTime = duration;
        }
        if (duration > m_iMaxTime) {
            m_iMaxTime = duration;
        }
        m_allTimes.push_back(duration);
    }
    // not helpful for streaming
    uint64_t mean() {
        uint64_t sum = std::accumulate(m_allTimes.begin(), m_allTimes.end(), 0);
        return sum / m_allTimes.size();
    }
    // p99 more relevant
    uint64_t p99E2E() {
        std::sort(m_allTimes.begin(), m_allTimes.end());
        size_t idx = static_cast<size_t>(0.99 * (m_allTimes.size() - 1));

        // for (uint64_t x: m_allTimes) {
        //     std::cout << x << " ";
        // }
        // std::cout << "\n";

        return m_allTimes[idx];
    }
    // p99 more relevant
    uint64_t p99Imshow() {
        // +1 bec the first chrono timestamp is between server init
        // and client start...
        std::sort(m_imshowTimes.begin() + 1, m_imshowTimes.end());
        size_t idx = static_cast<size_t>(0.99 * (m_imshowTimes.size() - 1));
        //     for (uint64_t x: m_imshowTimes) {
        //         std::cout << x << " ";
        //     }
        //     std::cout << "\n";
        return m_imshowTimes[idx];
    }
    uint64_t min() { return m_iMinTime; }
    uint64_t max() { return m_iMaxTime; }
};

#endif  // PERFSTATISTICS_H_
