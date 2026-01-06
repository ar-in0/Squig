#ifndef RTMP_SERVER_H
#define RTMP_SERVER_H

#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <queue>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <iomanip>

namespace rtmp {

// RTMP Message Types
enum class MessageType : uint8_t {
  SET_CHUNK_SIZE = 1,
  ABORT_MESSAGE = 2,
  ACKNOWLEDGEMENT = 3,
  USER_CONTROL = 4,
  WINDOW_ACK_SIZE = 5,
  SET_PEER_BANDWIDTH = 6,
  AUDIO = 8,
  VIDEO = 9,
  DATA_AMF3 = 15,
  SHARED_OBJECT_AMF3 = 16,
  COMMAND_AMF3 = 17,
  DATA_AMF0 = 18,
  SHARED_OBJECT_AMF0 = 19,
  COMMAND_AMF0 = 20,
  AGGREGATE = 22
};

// User Control Message Types
enum class UserControlType : uint16_t {
  STREAM_BEGIN = 0,
  STREAM_EOF = 1,
  STREAM_DRY = 2,
  SET_BUFFER_LENGTH = 3,
  STREAM_IS_RECORDED = 4,
  PING_REQUEST = 6,
  PING_RESPONSE = 7
};

// AMF0 Data Types
enum class AMF0Type : uint8_t {
  NUMBER = 0x00,
  BOOLEAN = 0x01,
  STRING = 0x02,
  OBJECT = 0x03,
  NULL_TYPE = 0x05,
  UNDEFINED = 0x06,
  ECMA_ARRAY = 0x08,
  OBJECT_END = 0x09
};

// Log Levels
enum class LogLevel { ERROR = 0, WARN = 1, INFO = 2, DEBUG = 3 };

// AMF0 Value
class AMF0Value {
public:
  AMF0Type type;
  double number;
  bool boolean;
  std::string string;
  std::map<std::string, std::shared_ptr<AMF0Value>> object;

  AMF0Value() : type(AMF0Type::NULL_TYPE), number(0), boolean(false) {}
};

// RTMP Chunk Header
struct ChunkHeader {
  uint8_t fmt;
  uint32_t csid;
  uint32_t timestamp;
  uint32_t msg_length;
  uint8_t msg_type_id;
  uint32_t msg_stream_id;
  bool has_extended_timestamp;
};

// RTMP Message
struct RTMPMessage {
  ChunkHeader header;
  std::vector<uint8_t> payload;
};

// Stream Information
struct StreamInfo {
  std::string app;
  std::string stream_key;
  bool is_publishing;
  bool is_playing;
  int client_fd;
  uint32_t stream_id;
  std::string client_ip;
};

// Stream Statistics
struct StreamStatistics {
  uint64_t bytes_sent = 0;
  uint64_t bytes_received = 0;
  uint32_t video_frames = 0;
  uint32_t audio_frames = 0;
  uint32_t dropped_frames = 0;
  std::chrono::steady_clock::time_point start_time;

  StreamStatistics() : start_time(std::chrono::steady_clock::now()) {}

  double getBitrate() const {
    auto now = std::chrono::steady_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::seconds>(now - start_time)
            .count();
    if (duration == 0)
      return 0;
    return (bytes_sent * 8.0) / duration / 1000.0; // kbps
  }

  double getUptime() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now - start_time)
        .count();
  }

  void printStats(std::ostream& os = std::cout) const {
    os << std::fixed << std::setprecision(2);
    os << "Stream Statistics\n";
    os << "-----------------\n";
    os << "Uptime           : " << getUptime() << " s\n";
    os << "Bitrate          : " << getBitrate() << " kbps\n";
    os << "Bytes Sent       : " << bytes_sent << " B\n";
    os << "Bytes Received   : " << bytes_received << " B\n";
    os << "Video Frames     : " << video_frames << '\n';
    os << "Audio Frames     : " << audio_frames << '\n';
    os << "Dropped Frames   : " << dropped_frames << '\n';
  }
};

// GOP Cache for instant playback
class GOPCache {
public:
  void addVideoFrame(const std::vector<uint8_t> &data, uint32_t timestamp);
  void addAudioFrame(const std::vector<uint8_t> &data, uint32_t timestamp);
  void addMetadata(const std::vector<uint8_t> &data);
  void sendToPlayer(class RTMPSession *session);
  void clear();
  bool hasKeyframe() const { return has_keyframe; }

private:
  struct CachedFrame {
    MessageType type;
    std::vector<uint8_t> data;
    uint32_t timestamp;
  };

  std::vector<CachedFrame> frames;
  std::vector<uint8_t> metadata;
  bool has_keyframe = false;
  std::mutex cache_mutex;

  bool isKeyframe(const std::vector<uint8_t> &data);
};

// FLV File Recorder
class FLVRecorder {
public:
  FLVRecorder(const std::string &filename);
  ~FLVRecorder();

  bool start();
  void stop();
  bool isRecording() const { return recording; }

  void writeAudioFrame(const std::vector<uint8_t> &data, uint32_t timestamp);
  void writeVideoFrame(const std::vector<uint8_t> &data, uint32_t timestamp);
  void writeMetadata(
      const std::map<std::string, std::shared_ptr<AMF0Value>> &metadata);

private:
  std::string filename;
  std::ofstream file;
  bool recording = false;
  uint32_t last_timestamp = 0;
  std::mutex file_mutex;

  void writeFLVHeader();
  void writeFLVTag(uint8_t tag_type, const std::vector<uint8_t> &data,
                   uint32_t timestamp);
  std::vector<uint8_t> encodeMetadata(
      const std::map<std::string, std::shared_ptr<AMF0Value>> &metadata);
};

// RTMP Client Session
class RTMPSession {
public:
  RTMPSession(int fd, const std::string &client_ip);
  ~RTMPSession();

  bool handshake();
  bool receiveChunk();
  bool sendMessage(const RTMPMessage &msg);
  bool sendChunk(uint32_t csid, uint32_t timestamp, uint8_t msg_type,
                 uint32_t stream_id, const std::vector<uint8_t> &data);

  int getFd() const { return client_fd; }
  const StreamInfo &getStreamInfo() const { return stream_info; }
  StreamInfo &getStreamInfo() { return stream_info; }

  void setChunkSize(uint32_t size) { chunk_size = size; }
  uint32_t getChunkSize() const { return chunk_size; }

  // Acknowledgement handling
  void onBytesReceived(size_t bytes);
  bool shouldSendAck() const;
  void sendAcknowledgement();

  // Ping/Pong
  void sendPing(uint32_t timestamp);
  void sendPong(uint32_t timestamp);

  // Statistics
  StreamStatistics &getStats() { return stats; }
  const StreamStatistics &getStats() const { return stats; }

  // Message queue access for server
  std::queue<RTMPMessage> &getMessageQueue() { return message_queue; }
  std::mutex &getQueueMutex() { return queue_mutex; }

  // Last activity tracking
  std::chrono::steady_clock::time_point getLastActivity() const {
    return last_activity;
  }
  void updateActivity() { last_activity = std::chrono::steady_clock::now(); }

  // AMF0 public access for server
  std::shared_ptr<AMF0Value> decodeAMF0(const uint8_t *data, size_t len,
                                        size_t &offset);
  std::vector<uint8_t> encodeAMF0(const AMF0Value &value);

  // FIXED: Moved to public section for RTMPServer access
  bool sendErrorResponse(const std::string &command, double transaction_id,
                         const std::string &description);

private:
  int client_fd;
  uint32_t chunk_size;
  uint32_t window_ack_size;
  uint32_t peer_bandwidth;
  uint32_t bytes_received;
  uint32_t last_ack_sent;
  std::map<uint32_t, ChunkHeader> prev_headers;
  std::map<uint32_t, std::vector<uint8_t>> incomplete_chunks;
  StreamInfo stream_info;
  std::queue<RTMPMessage> message_queue;
  std::mutex queue_mutex;
  StreamStatistics stats;
  std::chrono::steady_clock::time_point last_activity;

  bool readExactly(uint8_t *buf, size_t len);
  bool writeExactly(const uint8_t *buf, size_t len);
  bool parseChunkHeader(ChunkHeader &header);
  bool processMessage(const RTMPMessage &msg);
  bool handleCommand(const RTMPMessage &msg);
  bool handleAudioMessage(const RTMPMessage &msg);
  bool handleVideoMessage(const RTMPMessage &msg);
  bool handleDataMessage(const RTMPMessage &msg);
  bool handleUserControl(const RTMPMessage &msg);
  bool handleAcknowledgement(const RTMPMessage &msg);

  // AMF0 Encoding/Decoding
  std::vector<uint8_t> encodeAMF0String(const std::string &str);
  std::vector<uint8_t> encodeAMF0Number(double num);
  std::vector<uint8_t> encodeAMF0Object(
      const std::map<std::string, std::shared_ptr<AMF0Value>> &obj);

  // Command handlers
  bool handleConnect(const std::vector<std::shared_ptr<AMF0Value>> &args);
  bool handleReleaseStream(const std::vector<std::shared_ptr<AMF0Value>> &args);
  bool handleFCPublish(const std::vector<std::shared_ptr<AMF0Value>> &args);
  bool handleCreateStream(const std::vector<std::shared_ptr<AMF0Value>> &args);
  bool handlePublish(const std::vector<std::shared_ptr<AMF0Value>> &args);
  bool handlePlay(const std::vector<std::shared_ptr<AMF0Value>> &args);
  bool handleDeleteStream(const std::vector<std::shared_ptr<AMF0Value>> &args);

  // Response helpers
  bool sendConnectResponse(double transaction_id);
  bool sendCreateStreamResponse(double transaction_id, double stream_id);
  bool sendPublishResponse();
  bool sendPlayResponse();
};

// Callback types
using OnConnectCallback = std::function<void(std::shared_ptr<RTMPSession>)>;
using OnPublishCallback =
    std::function<void(std::shared_ptr<RTMPSession>, const std::string &app,
                       const std::string &stream_key)>;
using OnPlayCallback =
    std::function<void(std::shared_ptr<RTMPSession>, const std::string &app,
                       const std::string &stream_key)>;
using OnAudioDataCallback =
    std::function<void(std::shared_ptr<RTMPSession>,
                       const std::vector<uint8_t> &data, uint32_t timestamp)>;
using OnVideoDataCallback =
    std::function<void(std::shared_ptr<RTMPSession>,
                       const std::vector<uint8_t> &data, uint32_t timestamp)>;
using OnMetaDataCallback = std::function<void(
    std::shared_ptr<RTMPSession>,
    const std::map<std::string, std::shared_ptr<AMF0Value>> &metadata)>;
using OnDisconnectCallback = std::function<void(std::shared_ptr<RTMPSession>)>;
using AuthCallback =
    std::function<bool(const std::string &app, const std::string &stream_key,
                       const std::string &client_ip)>;
using OnLoggerCallback = 
    std::function<bool(const std::string &message, const LogLevel &logLevel)>;

// Logger
class Logger {
public:
  static Logger &getInstance() {
    static Logger instance;
    return instance;
  }

  void setLevel(LogLevel level) { current_level = level; }
  LogLevel getLevel() const { return current_level; }

  void error(const std::string &msg);
  void warn(const std::string &msg);
  void info(const std::string &msg);
  void debug(const std::string &msg);
  void setOnLog(const OnLoggerCallback cb) { on_log = cb; }

private:
  Logger() : current_level(LogLevel::INFO) {}
  LogLevel current_level;
  std::mutex log_mutex;
  
  // Logger Callback
  OnLoggerCallback on_log;

  void log(LogLevel level, const std::string &msg);
};

// RTMP Server
class RTMPServer {
public:
  RTMPServer(int port = 1935);
  ~RTMPServer();

  bool start(bool& isRunning);
  void stop();
  bool isRunning() const { return running; }

  // Callbacks
  void setOnConnect(OnConnectCallback cb) { on_connect = cb; }
  void setOnPublish(OnPublishCallback cb) { on_publish = cb; }
  void setOnPlay(OnPlayCallback cb) { on_play = cb; }
  void setOnAudioData(OnAudioDataCallback cb) { on_audio_data = cb; }
  void setOnVideoData(OnVideoDataCallback cb) { on_video_data = cb; }
  void setOnMetaData(OnMetaDataCallback cb) { on_metadata = cb; }
  void setOnDisconnect(OnDisconnectCallback cb) { on_disconnect = cb; }
  void setAuthCallback(AuthCallback cb) { auth_callback = cb; }

  // GOP Cache
  void enableGOPCache(bool enable) { use_gop_cache = enable; }
  bool isGOPCacheEnabled() const { return use_gop_cache; }

  // Recording
  bool startRecording(const std::string &app, const std::string &stream_key,
                      const std::string &filename);
  void stopRecording(const std::string &app, const std::string &stream_key);
  bool isRecording(const std::string &app, const std::string &stream_key) const;

  // Statistics
  StreamStatistics getStreamStats(const std::string &app,
                                  const std::string &stream_key) const;
  std::vector<std::pair<std::string, StreamStatistics>>
  getAllStreamStats() const;
  int getActivePublishers() const;
  int getActivePlayers() const;
  int getTotalConnections() const;

  // Connection limits
  void setMaxPublishersPerStream(int max) { max_publishers_per_stream = max; }
  void setMaxPlayersPerStream(int max) { max_players_per_stream = max; }
  void setMaxTotalConnections(int max) { max_total_connections = max; }
  int getMaxPublishersPerStream() const { return max_publishers_per_stream; }
  int getMaxPlayersPerStream() const { return max_players_per_stream; }
  int getMaxTotalConnections() const { return max_total_connections; }

  // Ping/Pong
  void enablePingPong(bool enable, int interval_seconds = 30);
  bool isPingPongEnabled() const { return ping_enabled; }

  // Timeout handling
  void setConnectionTimeout(int seconds) { connection_timeout = seconds; }
  int getConnectionTimeout() const { return connection_timeout; }

  // Broadcasting
  bool sendAudioToPlayers(const std::string &app, const std::string &stream_key,
                          const std::vector<uint8_t> &data, uint32_t timestamp);
  bool sendVideoToPlayers(const std::string &app, const std::string &stream_key,
                          const std::vector<uint8_t> &data, uint32_t timestamp);
  void sendMetadataToPlayers(const std::string &app,
                             const std::string &stream_key,
                             const std::vector<uint8_t> &data);

private:
  int port;
  int server_fd;
  bool running;
  std::thread accept_thread;
  std::thread ping_thread;
  std::thread timeout_thread;
  std::vector<std::thread> client_threads;
  std::vector<std::shared_ptr<RTMPSession>> sessions;
  mutable std::mutex sessions_mutex; // FIXED: Added mutable

  // Callbacks
  OnConnectCallback on_connect;
  OnPublishCallback on_publish;
  OnPlayCallback on_play;
  OnAudioDataCallback on_audio_data;
  OnVideoDataCallback on_video_data;
  OnMetaDataCallback on_metadata;
  OnDisconnectCallback on_disconnect;
  AuthCallback auth_callback;

  // GOP Caches
  bool use_gop_cache = true;
  std::map<std::string, std::shared_ptr<GOPCache>> gop_caches;
  std::mutex gop_mutex;

  // Recorders
  std::map<std::string, std::shared_ptr<FLVRecorder>> recorders;
  mutable std::mutex recorder_mutex; // FIXED: Added mutable

  // Statistics
  std::map<std::string, StreamStatistics> stream_stats;
  mutable std::mutex stats_mutex;

  // Connection limits
  int max_publishers_per_stream = 1;
  int max_players_per_stream = 1000;
  int max_total_connections = 1000;

  // Ping/Pong
  bool ping_enabled = false;
  int ping_interval = 30;

  // Timeout
  int connection_timeout = 60;

  void acceptClients();
  void handleClient(std::shared_ptr<RTMPSession> session);
  void removeSession(std::shared_ptr<RTMPSession> session);
  void processMediaMessages(std::shared_ptr<RTMPSession> session);
  void pingClientsRoutine();
  void timeoutCheckRoutine();

  std::string makeStreamKey(const std::string &app,
                            const std::string &stream) const {
    return app + "/" + stream;
  }

  int countPublishers(const std::string &app,
                      const std::string &stream_key) const;
  int countPlayers(const std::string &app, const std::string &stream_key) const;
  bool checkConnectionLimits(const std::string &app,
                             const std::string &stream_key,
                             bool is_publisher) const;
};

// Utility macros
#define LOG_ERROR(msg) rtmp::Logger::getInstance().error(msg)
#define LOG_WARN(msg) rtmp::Logger::getInstance().warn(msg)
#define LOG_INFO(msg) rtmp::Logger::getInstance().info(msg)
#define LOG_DEBUG(msg) rtmp::Logger::getInstance().debug(msg)

} // namespace rtmp

#endif // RTMP_SERVER_H