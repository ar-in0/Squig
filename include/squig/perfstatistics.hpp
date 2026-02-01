#ifndef PERFSTATISTICS_H_
#define PERFSTATISTICS_H_
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>
// Squig server on Zephyrus G14 in all cases.
enum TestType {
    kFfmpegLocalhost,  // ffmpeg rtmp stream from zephyrus G14, ubuntu 24.04
    kLarixIos,         // larix client on ios
    kFfmpegRpi,        // ffmpeg v4l2 rtmp stream from rpi, ubuntu 24.04
};

// inline for ODR
inline std::string getTestName(TestType test) {
    switch (test) {
        case kLarixIos:
            return "kLarixIos";
        case kFfmpegRpi:
            return "kFfmpegRpi";
        default:
            return "UnknownTest";
    }
}

class PerfStatistics {
   private:
    uint64_t m_iMaxTime{};
    uint64_t m_iMinTime{};  // ignored, list initialized by constructor
    std::vector<uint64_t> m_allTimes{};

    uint64_t m_iTprev;
    std::vector<uint64_t> m_imshowTimesE2E{};  // used to store time interval
                                               // b/w successive cv::imshow.
   public:
    PerfStatistics(uint64_t tStartMs)
        : m_iTprev{tStartMs},
          m_iMinTime{std::numeric_limits<uint64_t>::max()} {}

    void updateImshowTime(uint64_t now) {
        m_imshowTimesE2E.push_back(now - m_iTprev);
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
        std::sort(m_imshowTimesE2E.begin() + 1, m_imshowTimesE2E.end());
        size_t idx = static_cast<size_t>(0.99 * (m_imshowTimesE2E.size() - 1));
        //     for (uint64_t x: m_imshowTimesE2E) {
        //         std::cout << x << " ";
        //     }
        //     std::cout << "\n";
        return m_imshowTimesE2E[idx];
    }
    void writeToCSV(std::string_view csvNameStr, TestType test) {
        // open csv file
        // case based on the test type
        // write values in m_imshowTimes in correct row.
        std::string fName(csvNameStr);
        std::string testName = getTestName(test);

        // check if empty
        std::ifstream checkFile(fName);
        bool isEmpty = checkFile.peek() == std::ifstream::traits_type::eof();
        checkFile.close();
        // open in append mode
        std::ofstream outFile;
        outFile.open(fName, std::ios::out | std::ios::app);

        // write header if empty
        if (isEmpty) {
            outFile << "TestType, FrameIdx, TimeDelta\n";
        }
        // Format: TestName, FrameIndex, Value
        for (size_t i = 0; i < m_imshowTimesE2E.size(); ++i) {
            outFile << testName << "," << i << "," << m_imshowTimesE2E[i]
                    << "\n";
        }

        outFile.close();
    }

    uint64_t min() { return m_iMinTime; }
    uint64_t max() { return m_iMaxTime; }
};

#endif  // PERFSTATISTICS_H_
