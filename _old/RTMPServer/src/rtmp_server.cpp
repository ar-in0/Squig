#include "../include/rtmp_server.hpp"
#include <algorithm>
#include <cstring>
#include <ctime>
#include <iomanip>

namespace rtmp {

// Utility functions
static uint16_t readUint16BE(const uint8_t *data) {
  return (data[0] << 8) | data[1];
}

static uint32_t readUint24BE(const uint8_t *data) {
  return (data[0] << 16) | (data[1] << 8) | data[2];
}

static uint32_t readUint32BE(const uint8_t *data) {
  return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

static void writeUint16BE(uint8_t *data, uint16_t val) {
  data[0] = (val >> 8) & 0xFF;
  data[1] = val & 0xFF;
}

static void writeUint24BE(uint8_t *data, uint32_t val) {
  data[0] = (val >> 16) & 0xFF;
  data[1] = (val >> 8) & 0xFF;
  data[2] = val & 0xFF;
}

static void writeUint32BE(uint8_t *data, uint32_t val) {
  data[0] = (val >> 24) & 0xFF;
  data[1] = (val >> 16) & 0xFF;
  data[2] = (val >> 8) & 0xFF;
  data[3] = val & 0xFF;
}

static double readDouble(const uint8_t *data) {
  uint64_t val = ((uint64_t)data[0] << 56) | ((uint64_t)data[1] << 48) |
                 ((uint64_t)data[2] << 40) | ((uint64_t)data[3] << 32) |
                 ((uint64_t)data[4] << 24) | ((uint64_t)data[5] << 16) |
                 ((uint64_t)data[6] << 8) | (uint64_t)data[7];
  double result;
  memcpy(&result, &val, 8);
  return result;
}

static void writeDouble(uint8_t *data, double val) {
  uint64_t bits;
  memcpy(&bits, &val, 8);
  data[0] = (bits >> 56) & 0xFF;
  data[1] = (bits >> 48) & 0xFF;
  data[2] = (bits >> 40) & 0xFF;
  data[3] = (bits >> 32) & 0xFF;
  data[4] = (bits >> 24) & 0xFF;
  data[5] = (bits >> 16) & 0xFF;
  data[6] = (bits >> 8) & 0xFF;
  data[7] = bits & 0xFF;
}

// Logger Implementation
void Logger::log(LogLevel level, const std::string &msg) {
  if (level > current_level)
    return;
  std::lock_guard<std::mutex> lock(log_mutex);
  const LogLevel level_enum[] = {LogLevel::ERROR, LogLevel::WARN, LogLevel::INFO, LogLevel::DEBUG};
  on_log(msg, level);
}

void Logger::error(const std::string &msg) { log(LogLevel::ERROR, msg); }
void Logger::warn(const std::string &msg) { log(LogLevel::WARN, msg); }
void Logger::info(const std::string &msg) { log(LogLevel::INFO, msg); }
void Logger::debug(const std::string &msg) { log(LogLevel::DEBUG, msg); }

// GOP Cache Implementation
bool GOPCache::isKeyframe(const std::vector<uint8_t> &data) {
  if (data.empty())
    return false;
  uint8_t frame_type = (data[0] >> 4) & 0x0F;
  return frame_type == 1;
}

void GOPCache::addVideoFrame(const std::vector<uint8_t> &data,
                             uint32_t timestamp) {
  std::lock_guard<std::mutex> lock(cache_mutex);

  if (data.size() >= 2) {
    uint8_t frame_type = (data[0] >> 4) & 0x0F;
    uint8_t codec_id = data[0] & 0x0F;
    uint8_t avc_packet_type = data[1];

    if (codec_id == 7 && avc_packet_type == 0) {
      CachedFrame frame;
      frame.type = MessageType::VIDEO;
      frame.data = data;
      frame.timestamp = timestamp;

      if (!frames.empty() && frames[0].data.size() >= 2) {
        uint8_t first_codec = frames[0].data[0] & 0x0F;
        uint8_t first_packet = frames[0].data[1];
        if (first_codec == 7 && first_packet == 0) {
          frames.erase(frames.begin());
        }
      }

      frames.insert(frames.begin(), frame);
      has_keyframe = true;
      LOG_INFO("Cached AVC Sequence Header (SPS/PPS)");
      return;
    }
  }

  if (isKeyframe(data)) {
    std::vector<CachedFrame> new_frames;
    if (!frames.empty() && frames[0].data.size() >= 2) {
      uint8_t first_codec = frames[0].data[0] & 0x0F;
      uint8_t first_packet = frames[0].data[1];
      if (first_codec == 7 && first_packet == 0) {
        new_frames.push_back(frames[0]); // Keep sequence header
      }
    }
    frames = new_frames;
    has_keyframe = true;
  }

  if (has_keyframe) {
    CachedFrame frame;
    frame.type = MessageType::VIDEO;
    frame.data = data;
    frame.timestamp = timestamp;
    frames.push_back(frame);

    if (frames.size() > 300) {
      for (size_t i = 1; i < frames.size(); i++) {
        if (frames[i].type == MessageType::VIDEO &&
            isKeyframe(frames[i].data)) {
          std::vector<CachedFrame> new_frames;
          if (!frames.empty() && frames[0].data.size() >= 2) {
            uint8_t first_codec = frames[0].data[0] & 0x0F;
            uint8_t first_packet = frames[0].data[1];
            if (first_codec == 7 && first_packet == 0) {
              new_frames.push_back(frames[0]);
            }
          }
          new_frames.insert(new_frames.end(), frames.begin() + i, frames.end());
          frames = new_frames;
          break;
        }
      }
    }
  }
}

void GOPCache::addAudioFrame(const std::vector<uint8_t> &data,
                             uint32_t timestamp) {
  std::lock_guard<std::mutex> lock(cache_mutex);
  if (has_keyframe) {
    CachedFrame frame;
    frame.type = MessageType::AUDIO;
    frame.data = data;
    frame.timestamp = timestamp;
    frames.push_back(frame);
  }
}

void GOPCache::addMetadata(const std::vector<uint8_t> &data) {
  std::lock_guard<std::mutex> lock(cache_mutex);
  metadata = data;
  LOG_DEBUG("Cached metadata");
}

void GOPCache::sendToPlayer(RTMPSession *session) {
  std::lock_guard<std::mutex> lock(cache_mutex);
  const auto &info = session->getStreamInfo();

  if (!metadata.empty()) {
    session->sendChunk(4, 0, (uint8_t)MessageType::DATA_AMF0, info.stream_id,
                       metadata);
    LOG_DEBUG("Sent metadata to new player");
  }

  for (const auto &frame : frames) {
    uint32_t csid = 4;
    if (frame.type == MessageType::VIDEO) {
      csid = 6;
    } else if (frame.type == MessageType::AUDIO) {
      csid = 4;
    }

    session->sendChunk(csid, frame.timestamp, (uint8_t)frame.type,
                       info.stream_id, frame.data);
  }

  LOG_INFO("Sent " + std::to_string(frames.size()) +
           " cached frames to player");
}

void GOPCache::clear() {
  std::lock_guard<std::mutex> lock(cache_mutex);
  frames.clear();
  metadata.clear();
  has_keyframe = false;
}

// FLV Recorder Implementation
FLVRecorder::FLVRecorder(const std::string &filename) : filename(filename) {}

FLVRecorder::~FLVRecorder() { stop(); }

bool FLVRecorder::start() {
  std::lock_guard<std::mutex> lock(file_mutex);
  file.open(filename, std::ios::binary);
  if (!file.is_open())
    return false;
  writeFLVHeader();
  recording = true;
  return true;
}

void FLVRecorder::stop() {
  std::lock_guard<std::mutex> lock(file_mutex);
  if (file.is_open()) {
    file.close();
  }
  recording = false;
}

void FLVRecorder::writeFLVHeader() {
  uint8_t header[] = {'F', 'L', 'V', 0x01, 0x05, 0x00, 0x00, 0x00, 0x09};
  file.write(reinterpret_cast<char *>(header), sizeof(header));
  uint32_t prev_size = 0;
  file.write(reinterpret_cast<char *>(&prev_size), 4);
}

void FLVRecorder::writeFLVTag(uint8_t tag_type,
                              const std::vector<uint8_t> &data,
                              uint32_t timestamp) {
  if (!recording || !file.is_open())
    return;
  std::lock_guard<std::mutex> lock(file_mutex);
  uint32_t data_size = data.size();
  file.put(tag_type);
  file.put((data_size >> 16) & 0xFF);
  file.put((data_size >> 8) & 0xFF);
  file.put(data_size & 0xFF);
  file.put((timestamp >> 16) & 0xFF);
  file.put((timestamp >> 8) & 0xFF);
  file.put(timestamp & 0xFF);
  file.put((timestamp >> 24) & 0xFF);
  file.put(0);
  file.put(0);
  file.put(0);
  file.write(reinterpret_cast<const char *>(data.data()), data.size());
  uint32_t tag_size = 11 + data_size;
  file.put((tag_size >> 24) & 0xFF);
  file.put((tag_size >> 16) & 0xFF);
  file.put((tag_size >> 8) & 0xFF);
  file.put(tag_size & 0xFF);
  last_timestamp = timestamp;
}

void FLVRecorder::writeVideoFrame(const std::vector<uint8_t> &data,
                                  uint32_t timestamp) {
  writeFLVTag(0x09, data, timestamp);
}

void FLVRecorder::writeAudioFrame(const std::vector<uint8_t> &data,
                                  uint32_t timestamp) {
  writeFLVTag(0x08, data, timestamp);
}

void FLVRecorder::writeMetadata(
    const std::map<std::string, std::shared_ptr<AMF0Value>> &metadata) {
  auto encoded = encodeMetadata(metadata);
  writeFLVTag(0x12, encoded, 0);
}

std::vector<uint8_t> FLVRecorder::encodeMetadata(
    const std::map<std::string, std::shared_ptr<AMF0Value>> &metadata) {
  std::vector<uint8_t> result;
  result.push_back(0x02);
  uint16_t len = 10;
  result.push_back((len >> 8) & 0xFF);
  result.push_back(len & 0xFF);
  std::string meta_str = "onMetaData";
  result.insert(result.end(), meta_str.begin(), meta_str.end());
  result.push_back(0x08);
  uint32_t count = metadata.size();
  result.push_back((count >> 24) & 0xFF);
  result.push_back((count >> 16) & 0xFF);
  result.push_back((count >> 8) & 0xFF);
  result.push_back(count & 0xFF);
  for (const auto &pair : metadata) {
    uint16_t key_len = pair.first.size();
    result.push_back((key_len >> 8) & 0xFF);
    result.push_back(key_len & 0xFF);
    result.insert(result.end(), pair.first.begin(), pair.first.end());
    if (pair.second->type == AMF0Type::NUMBER) {
      result.push_back(0x00);
      uint64_t bits;
      memcpy(&bits, &pair.second->number, 8);
      for (int i = 7; i >= 0; i--) {
        result.push_back((bits >> (i * 8)) & 0xFF);
      }
    } else if (pair.second->type == AMF0Type::STRING) {
      result.push_back(0x02);
      uint16_t str_len = pair.second->string.size();
      result.push_back((str_len >> 8) & 0xFF);
      result.push_back(str_len & 0xFF);
      result.insert(result.end(), pair.second->string.begin(),
                    pair.second->string.end());
    }
  }
  result.push_back(0x00);
  result.push_back(0x00);
  result.push_back(0x09);
  return result;
}

// RTMPSession Implementation
RTMPSession::RTMPSession(int fd, const std::string &client_ip)
    : client_fd(fd), chunk_size(128), window_ack_size(2500000),
      peer_bandwidth(2500000), bytes_received(0), last_ack_sent(0) {
  stream_info.is_publishing = false;
  stream_info.is_playing = false;
  stream_info.client_fd = fd;
  stream_info.stream_id = 0;
  stream_info.client_ip = client_ip;
  last_activity = std::chrono::steady_clock::now();
}

RTMPSession::~RTMPSession() {
  if (client_fd >= 0) {
    close(client_fd);
  }
}

bool RTMPSession::readExactly(uint8_t *buf, size_t len) {
  size_t total = 0;
  while (total < len) {
    ssize_t n = recv(client_fd, buf + total, len - total, 0);
    if (n <= 0)
      return false;
    total += n;
    onBytesReceived(n);
  }
  updateActivity();
  return true;
}

bool RTMPSession::writeExactly(const uint8_t *buf, size_t len) {
  size_t total = 0;
  while (total < len) {
    ssize_t n = send(client_fd, buf + total, len - total, MSG_NOSIGNAL);
    if (n <= 0)
      return false;
    total += n;
  }
  stats.bytes_sent += len;
  updateActivity();
  return true;
}

void RTMPSession::onBytesReceived(size_t bytes) {
  bytes_received += bytes;
  stats.bytes_received += bytes;
}

bool RTMPSession::shouldSendAck() const {
  return (bytes_received - last_ack_sent) >= window_ack_size;
}

void RTMPSession::sendAcknowledgement() {
  std::vector<uint8_t> ack(4);
  writeUint32BE(ack.data(), bytes_received);
  sendChunk(2, 0, (uint8_t)MessageType::ACKNOWLEDGEMENT, 0, ack);
  last_ack_sent = bytes_received;
  LOG_DEBUG("Sent ACK: " + std::to_string(bytes_received));
}

void RTMPSession::sendPing(uint32_t timestamp) {
  std::vector<uint8_t> ping_msg(6);
  writeUint16BE(ping_msg.data(), (uint16_t)UserControlType::PING_REQUEST);
  writeUint32BE(ping_msg.data() + 2, timestamp);
  sendChunk(2, 0, (uint8_t)MessageType::USER_CONTROL, 0, ping_msg);
}

void RTMPSession::sendPong(uint32_t timestamp) {
  std::vector<uint8_t> pong_msg(6);
  writeUint16BE(pong_msg.data(), (uint16_t)UserControlType::PING_RESPONSE);
  writeUint32BE(pong_msg.data() + 2, timestamp);
  sendChunk(2, 0, (uint8_t)MessageType::USER_CONTROL, 0, pong_msg);
}

bool RTMPSession::handshake() {
  uint8_t c0c1[1537];
  if (!readExactly(c0c1, 1537))
    return false;
  if (c0c1[0] != 3)
    return false;
  uint8_t s0s1[1537];
  s0s1[0] = 3;
  memset(s0s1 + 1, 0, 8);
  for (int i = 9; i < 1537; i++) {
    s0s1[i] = rand() % 256;
  }
  if (!writeExactly(s0s1, 1537))
    return false;
  if (!writeExactly(c0c1 + 1, 1536))
    return false;
  uint8_t c2[1536];
  if (!readExactly(c2, 1536))
    return false;
  return true;
}

bool RTMPSession::parseChunkHeader(ChunkHeader &header) {
  uint8_t basic_header;
  if (!readExactly(&basic_header, 1))
    return false;
  header.fmt = (basic_header >> 6) & 0x03;
  header.csid = basic_header & 0x3F;
  if (header.csid == 0) {
    uint8_t csid_byte;
    if (!readExactly(&csid_byte, 1))
      return false;
    header.csid = 64 + csid_byte;
  } else if (header.csid == 1) {
    uint8_t csid_bytes[2];
    if (!readExactly(csid_bytes, 2))
      return false;
    header.csid = 64 + csid_bytes[0] + (csid_bytes[1] * 256);
  }
  ChunkHeader prev = prev_headers[header.csid];
  header.has_extended_timestamp = false;
  if (header.fmt == 0) {
    uint8_t buf[11];
    if (!readExactly(buf, 11))
      return false;
    header.timestamp = readUint24BE(buf);
    header.msg_length = readUint24BE(buf + 3);
    header.msg_type_id = buf[6];
    header.msg_stream_id =
        buf[7] | (buf[8] << 8) | (buf[9] << 16) | (buf[10] << 24);
    if (header.timestamp == 0xFFFFFF) {
      uint8_t ext_ts[4];
      if (!readExactly(ext_ts, 4))
        return false;
      header.timestamp = readUint32BE(ext_ts);
      header.has_extended_timestamp = true;
    }
  } else if (header.fmt == 1) {
    uint8_t buf[7];
    if (!readExactly(buf, 7))
      return false;
    uint32_t timestamp_delta = readUint24BE(buf);
    header.msg_length = readUint24BE(buf + 3);
    header.msg_type_id = buf[6];
    header.msg_stream_id = prev.msg_stream_id;
    if (timestamp_delta == 0xFFFFFF) {
      uint8_t ext_ts[4];
      if (!readExactly(ext_ts, 4))
        return false;
      timestamp_delta = readUint32BE(ext_ts);
      header.has_extended_timestamp = true;
    }
    header.timestamp = prev.timestamp + timestamp_delta;
  } else if (header.fmt == 2) {
    uint8_t buf[3];
    if (!readExactly(buf, 3))
      return false;
    uint32_t timestamp_delta = readUint24BE(buf);
    header.msg_length = prev.msg_length;
    header.msg_type_id = prev.msg_type_id;
    header.msg_stream_id = prev.msg_stream_id;
    if (timestamp_delta == 0xFFFFFF) {
      uint8_t ext_ts[4];
      if (!readExactly(ext_ts, 4))
        return false;
      timestamp_delta = readUint32BE(ext_ts);
      header.has_extended_timestamp = true;
    }
    header.timestamp = prev.timestamp + timestamp_delta;
  } else {
    header.timestamp = prev.timestamp;
    header.msg_length = prev.msg_length;
    header.msg_type_id = prev.msg_type_id;
    header.msg_stream_id = prev.msg_stream_id;
  }
  prev_headers[header.csid] = header;
  return true;
}

bool RTMPSession::receiveChunk() {
  ChunkHeader header;
  if (!parseChunkHeader(header))
    return false;
  auto &incomplete = incomplete_chunks[header.csid];
  size_t to_read = std::min((size_t)chunk_size,
                            (size_t)(header.msg_length - incomplete.size()));
  std::vector<uint8_t> chunk_data(to_read);
  if (!readExactly(chunk_data.data(), to_read))
    return false;
  incomplete.insert(incomplete.end(), chunk_data.begin(), chunk_data.end());
  if (incomplete.size() >= header.msg_length) {
    RTMPMessage msg;
    msg.header = header;
    msg.payload = incomplete;
    incomplete.clear();
    incomplete_chunks.erase(header.csid);
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      message_queue.push(msg);
    }
    // Send ACK if needed
    if (shouldSendAck()) {
      sendAcknowledgement();
    }
    return processMessage(msg);
  }
  return true;
}

bool RTMPSession::sendChunk(uint32_t csid, uint32_t timestamp, uint8_t msg_type,
                            uint32_t stream_id,
                            const std::vector<uint8_t> &data) {
  size_t sent = 0;
  bool first = true;
  while (sent < data.size()) {
    std::vector<uint8_t> chunk;
    uint8_t fmt = first ? 0 : 3;
    if (csid < 64) {
      chunk.push_back((fmt << 6) | csid);
    } else if (csid < 320) {
      chunk.push_back(fmt << 6);
      chunk.push_back((csid - 64) & 0xFF);
    } else {
      chunk.push_back((fmt << 6) | 1);
      chunk.push_back((csid - 64) & 0xFF);
      chunk.push_back(((csid - 64) >> 8) & 0xFF);
    }
    if (first) {
      uint8_t msg_header[11];
      writeUint24BE(msg_header, timestamp >= 0xFFFFFF ? 0xFFFFFF : timestamp);
      writeUint24BE(msg_header + 3, data.size());
      msg_header[6] = msg_type;
      msg_header[7] = stream_id & 0xFF;
      msg_header[8] = (stream_id >> 8) & 0xFF;
      msg_header[9] = (stream_id >> 16) & 0xFF;
      msg_header[10] = (stream_id >> 24) & 0xFF;
      chunk.insert(chunk.end(), msg_header, msg_header + 11);
      if (timestamp >= 0xFFFFFF) {
        uint8_t ext_ts[4];
        writeUint32BE(ext_ts, timestamp);
        chunk.insert(chunk.end(), ext_ts, ext_ts + 4);
      }
    }
    size_t to_send = std::min(chunk_size, (uint32_t)(data.size() - sent));
    chunk.insert(chunk.end(), data.begin() + sent,
                 data.begin() + sent + to_send);
    if (!writeExactly(chunk.data(), chunk.size()))
      return false;
    sent += to_send;
    first = false;
  }
  return true;
}

std::shared_ptr<AMF0Value> RTMPSession::decodeAMF0(const uint8_t *data,
                                                   size_t len, size_t &offset) {
  if (offset >= len)
    return nullptr;
  auto val = std::make_shared<AMF0Value>();
  val->type = (AMF0Type)data[offset++];
  switch (val->type) {
  case AMF0Type::NUMBER:
    if (offset + 8 > len)
      return nullptr;
    val->number = readDouble(data + offset);
    offset += 8;
    break;
  case AMF0Type::BOOLEAN:
    if (offset >= len)
      return nullptr;
    val->boolean = data[offset++] != 0;
    break;
  case AMF0Type::STRING: {
    if (offset + 2 > len)
      return nullptr;
    uint16_t str_len = readUint16BE(data + offset);
    offset += 2;
    if (offset + str_len > len)
      return nullptr;
    val->string = std::string((char *)(data + offset), str_len);
    offset += str_len;
    break;
  }
  case AMF0Type::OBJECT:
    while (offset < len) {
      if (offset + 2 > len)
        return nullptr;
      uint16_t key_len = readUint16BE(data + offset);
      offset += 2;
      if (key_len == 0 && offset < len &&
          data[offset] == (uint8_t)AMF0Type::OBJECT_END) {
        offset++;
        break;
      }
      if (offset + key_len > len)
        return nullptr;
      std::string key((char *)(data + offset), key_len);
      offset += key_len;
      auto value = decodeAMF0(data, len, offset);
      if (!value)
        return nullptr;
      val->object[key] = value;
    }
    break;
  case AMF0Type::NULL_TYPE:
  case AMF0Type::UNDEFINED:
    break;
  case AMF0Type::ECMA_ARRAY:
    if (offset + 4 > len)
      return nullptr;
    offset += 4;
    while (offset < len) {
      if (offset + 2 > len)
        return nullptr;
      uint16_t key_len = readUint16BE(data + offset);
      offset += 2;
      if (key_len == 0 && offset < len &&
          data[offset] == (uint8_t)AMF0Type::OBJECT_END) {
        offset++;
        break;
      }
      if (offset + key_len > len)
        return nullptr;
      std::string key((char *)(data + offset), key_len);
      offset += key_len;
      auto value = decodeAMF0(data, len, offset);
      if (!value)
        return nullptr;
      val->object[key] = value;
    }
    break;
  case AMF0Type::OBJECT_END:
    break;
  }
  return val;
}

std::vector<uint8_t> RTMPSession::encodeAMF0String(const std::string &str) {
  std::vector<uint8_t> result;
  result.push_back((uint8_t)AMF0Type::STRING);
  uint8_t len_buf[2];
  writeUint16BE(len_buf, str.size());
  result.insert(result.end(), len_buf, len_buf + 2);
  result.insert(result.end(), str.begin(), str.end());
  return result;
}

std::vector<uint8_t> RTMPSession::encodeAMF0Number(double num) {
  std::vector<uint8_t> result;
  result.push_back((uint8_t)AMF0Type::NUMBER);
  uint8_t num_buf[8];
  writeDouble(num_buf, num);
  result.insert(result.end(), num_buf, num_buf + 8);
  return result;
}

std::vector<uint8_t> RTMPSession::encodeAMF0Object(
    const std::map<std::string, std::shared_ptr<AMF0Value>> &obj) {
  std::vector<uint8_t> result;
  result.push_back((uint8_t)AMF0Type::OBJECT);
  for (const auto &pair : obj) {
    uint8_t key_len[2];
    writeUint16BE(key_len, pair.first.size());
    result.insert(result.end(), key_len, key_len + 2);
    result.insert(result.end(), pair.first.begin(), pair.first.end());
    auto encoded = encodeAMF0(*pair.second);
    result.insert(result.end(), encoded.begin(), encoded.end());
  }
  result.push_back(0);
  result.push_back(0);
  result.push_back((uint8_t)AMF0Type::OBJECT_END);
  return result;
}

std::vector<uint8_t> RTMPSession::encodeAMF0(const AMF0Value &value) {
  switch (value.type) {
  case AMF0Type::NUMBER:
    return encodeAMF0Number(value.number);
  case AMF0Type::STRING:
    return encodeAMF0String(value.string);
  case AMF0Type::OBJECT:
    return encodeAMF0Object(value.object);
  case AMF0Type::NULL_TYPE: {
    std::vector<uint8_t> result;
    result.push_back((uint8_t)AMF0Type::NULL_TYPE);
    return result;
  }
  case AMF0Type::BOOLEAN: {
    std::vector<uint8_t> result;
    result.push_back((uint8_t)AMF0Type::BOOLEAN);
    result.push_back(value.boolean ? 1 : 0);
    return result;
  }
  default:
    return std::vector<uint8_t>();
  }
}

bool RTMPSession::processMessage(const RTMPMessage &msg) {
  MessageType type = (MessageType)msg.header.msg_type_id;
  switch (type) {
  case MessageType::SET_CHUNK_SIZE:
    if (msg.payload.size() >= 4) {
      chunk_size = readUint32BE(msg.payload.data()) & 0x7FFFFFFF;
      LOG_DEBUG("Chunk size set to: " + std::to_string(chunk_size));
    }
    break;
  case MessageType::WINDOW_ACK_SIZE:
    if (msg.payload.size() >= 4) {
      window_ack_size = readUint32BE(msg.payload.data());
      LOG_DEBUG("Window ACK size set to: " + std::to_string(window_ack_size));
    }
    break;
  case MessageType::SET_PEER_BANDWIDTH:
    if (msg.payload.size() >= 5) {
      peer_bandwidth = readUint32BE(msg.payload.data());
      LOG_DEBUG("Peer bandwidth set to: " + std::to_string(peer_bandwidth));
    }
    break;
  case MessageType::COMMAND_AMF0:
    return handleCommand(msg);
  case MessageType::AUDIO:
    return handleAudioMessage(msg);
  case MessageType::VIDEO:
    return handleVideoMessage(msg);
  case MessageType::DATA_AMF0:
    return handleDataMessage(msg);
  case MessageType::USER_CONTROL:
    return handleUserControl(msg);
  case MessageType::ACKNOWLEDGEMENT:
    return handleAcknowledgement(msg);
  default:
    break;
  }
  return true;
}

bool RTMPSession::handleUserControl(const RTMPMessage &msg) {
  if (msg.payload.size() < 2)
    return true;
  uint16_t event_type = readUint16BE(msg.payload.data());
  UserControlType type = (UserControlType)event_type;
  switch (type) {
  case UserControlType::PING_REQUEST:
    if (msg.payload.size() >= 6) {
      uint32_t timestamp = readUint32BE(msg.payload.data() + 2);
      sendPong(timestamp);
      LOG_DEBUG("Received PING, sent PONG");
    }
    break;
  case UserControlType::PING_RESPONSE:
    LOG_DEBUG("Received PONG response");
    break;
  default:
    break;
  }
  return true;
}

bool RTMPSession::handleAcknowledgement(const RTMPMessage &msg) {
  if (msg.payload.size() >= 4) {
    uint32_t ack_value = readUint32BE(msg.payload.data());
    LOG_DEBUG("Received ACK: " + std::to_string(ack_value));
  }
  return true;
}

bool RTMPSession::handleAudioMessage(const RTMPMessage &msg) {
  stats.audio_frames++;
  return true;
}


bool RTMPSession::handleVideoMessage(const RTMPMessage &msg) {
  stats.video_frames++;
  stats.printStats();
  return true;
}

bool RTMPSession::handleDataMessage(const RTMPMessage &msg) {
  size_t offset = 0;
  auto command = decodeAMF0(msg.payload.data(), msg.payload.size(), offset);
  if (!command || command->type != AMF0Type::STRING)
    return true;
  if (command->string == "@setDataFrame" || command->string == "onMetaData") {
    if (command->string == "@setDataFrame") {
      auto metadata_name =
          decodeAMF0(msg.payload.data(), msg.payload.size(), offset);
      if (!metadata_name || metadata_name->type != AMF0Type::STRING)
        return true;
    }
    auto metadata_obj =
        decodeAMF0(msg.payload.data(), msg.payload.size(), offset);
    if (metadata_obj && (metadata_obj->type == AMF0Type::OBJECT ||
                         metadata_obj->type == AMF0Type::ECMA_ARRAY)) {
      LOG_INFO("Received metadata");
    }
  }
  return true;
}

bool RTMPSession::handleCommand(const RTMPMessage &msg) {
  size_t offset = 0;
  std::vector<std::shared_ptr<AMF0Value>> args;
  while (offset < msg.payload.size()) {
    auto arg = decodeAMF0(msg.payload.data(), msg.payload.size(), offset);
    if (!arg)
      break;
    args.push_back(arg);
  }
  if (args.empty() || args[0]->type != AMF0Type::STRING)
    return false;
  std::string command = args[0]->string;
  LOG_DEBUG("Received command: " + command);
  if (command == "connect") {
    return handleConnect(args);
  } else if (command == "releaseStream") {
    return handleReleaseStream(args);
  } else if (command == "FCPublish") {
    return handleFCPublish(args);
  } else if (command == "createStream") {
    return handleCreateStream(args);
  } else if (command == "publish") {
    return handlePublish(args);
  } else if (command == "play") {
    return handlePlay(args);
  } else if (command == "deleteStream") {
    return handleDeleteStream(args);
  }
  return true;
}

bool RTMPSession::handleConnect(
    const std::vector<std::shared_ptr<AMF0Value>> &args) {
  if (args.size() < 3)
    return false;
  double transaction_id = args[1]->number;
  if (args[2]->type == AMF0Type::OBJECT) {
    auto &props = args[2]->object;
    if (props.find("app") != props.end() &&
        props["app"]->type == AMF0Type::STRING) {
      stream_info.app = props["app"]->string;
      LOG_INFO("App: " + stream_info.app);
    }
  }
  std::vector<uint8_t> ack_size(4);
  writeUint32BE(ack_size.data(), 2500000);
  sendChunk(2, 0, (uint8_t)MessageType::WINDOW_ACK_SIZE, 0, ack_size);
  std::vector<uint8_t> bandwidth(5);
  writeUint32BE(bandwidth.data(), 2500000);
  bandwidth[4] = 2;
  sendChunk(2, 0, (uint8_t)MessageType::SET_PEER_BANDWIDTH, 0, bandwidth);
  std::vector<uint8_t> stream_begin(6);
  writeUint16BE(stream_begin.data(), (uint16_t)UserControlType::STREAM_BEGIN);
  writeUint32BE(stream_begin.data() + 2, 0);
  sendChunk(2, 0, (uint8_t)MessageType::USER_CONTROL, 0, stream_begin);
  std::vector<uint8_t> chunk_size_msg(4);
  writeUint32BE(chunk_size_msg.data(), 4096);
  sendChunk(2, 0, (uint8_t)MessageType::SET_CHUNK_SIZE, 0, chunk_size_msg);
  chunk_size = 4096;
  return sendConnectResponse(transaction_id);
}

bool RTMPSession::sendConnectResponse(double transaction_id) {
  std::vector<uint8_t> response;
  auto cmd = encodeAMF0String("_result");
  response.insert(response.end(), cmd.begin(), cmd.end());
  auto tid = encodeAMF0Number(transaction_id);
  response.insert(response.end(), tid.begin(), tid.end());
  AMF0Value props;
  props.type = AMF0Type::OBJECT;
  props.object["fmsVer"] = std::make_shared<AMF0Value>();
  props.object["fmsVer"]->type = AMF0Type::STRING;
  props.object["fmsVer"]->string = "FMS/3,0,1,123";
  props.object["capabilities"] = std::make_shared<AMF0Value>();
  props.object["capabilities"]->type = AMF0Type::NUMBER;
  props.object["capabilities"]->number = 31;
  auto props_enc = encodeAMF0(props);
  response.insert(response.end(), props_enc.begin(), props_enc.end());
  AMF0Value info;
  info.type = AMF0Type::OBJECT;
  info.object["level"] = std::make_shared<AMF0Value>();
  info.object["level"]->type = AMF0Type::STRING;
  info.object["level"]->string = "status";
  info.object["code"] = std::make_shared<AMF0Value>();
  info.object["code"]->type = AMF0Type::STRING;
  info.object["code"]->string = "NetConnection.Connect.Success";
  info.object["description"] = std::make_shared<AMF0Value>();
  info.object["description"]->type = AMF0Type::STRING;
  info.object["description"]->string = "Connection succeeded.";
  info.object["objectEncoding"] = std::make_shared<AMF0Value>();
  info.object["objectEncoding"]->type = AMF0Type::NUMBER;
  info.object["objectEncoding"]->number = 0;
  auto info_enc = encodeAMF0(info);
  response.insert(response.end(), info_enc.begin(), info_enc.end());
  return sendChunk(3, 0, (uint8_t)MessageType::COMMAND_AMF0, 0, response);
}

bool RTMPSession::handleReleaseStream(
    const std::vector<std::shared_ptr<AMF0Value>> &args) {
  return true;
}

bool RTMPSession::handleFCPublish(
    const std::vector<std::shared_ptr<AMF0Value>> &args) {
  return true;
}

bool RTMPSession::handleCreateStream(
    const std::vector<std::shared_ptr<AMF0Value>> &args) {
  if (args.size() < 2)
    return false;
  double transaction_id = args[1]->number;
  stream_info.stream_id = 1;
  return sendCreateStreamResponse(transaction_id, 1);
}

bool RTMPSession::sendCreateStreamResponse(double transaction_id,
                                           double stream_id) {
  std::vector<uint8_t> response;
  auto cmd = encodeAMF0String("_result");
  response.insert(response.end(), cmd.begin(), cmd.end());
  auto tid = encodeAMF0Number(transaction_id);
  response.insert(response.end(), tid.begin(), tid.end());
  AMF0Value null_val;
  null_val.type = AMF0Type::NULL_TYPE;
  auto null_enc = encodeAMF0(null_val);
  response.insert(response.end(), null_enc.begin(), null_enc.end());
  auto sid = encodeAMF0Number(stream_id);
  response.insert(response.end(), sid.begin(), sid.end());
  return sendChunk(3, 0, (uint8_t)MessageType::COMMAND_AMF0, 0, response);
}

bool RTMPSession::handlePublish(
    const std::vector<std::shared_ptr<AMF0Value>> &args) {
  if (args.size() < 4)
    return false;
  if (args[3]->type == AMF0Type::STRING) {
    stream_info.stream_key = args[3]->string;
    stream_info.is_publishing = true;
    LOG_INFO("Publishing to: " + stream_info.stream_key);
  }
  return sendPublishResponse();
}

bool RTMPSession::sendPublishResponse() {
  std::vector<uint8_t> response;
  auto cmd = encodeAMF0String("onStatus");
  response.insert(response.end(), cmd.begin(), cmd.end());
  auto tid = encodeAMF0Number(0);
  response.insert(response.end(), tid.begin(), tid.end());
  AMF0Value null_val;
  null_val.type = AMF0Type::NULL_TYPE;
  auto null_enc = encodeAMF0(null_val);
  response.insert(response.end(), null_enc.begin(), null_enc.end());
  AMF0Value info;
  info.type = AMF0Type::OBJECT;
  info.object["level"] = std::make_shared<AMF0Value>();
  info.object["level"]->type = AMF0Type::STRING;
  info.object["level"]->string = "status";
  info.object["code"] = std::make_shared<AMF0Value>();
  info.object["code"]->type = AMF0Type::STRING;
  info.object["code"]->string = "NetStream.Publish.Start";
  info.object["description"] = std::make_shared<AMF0Value>();
  info.object["description"]->type = AMF0Type::STRING;
  info.object["description"]->string = "Stream is now published.";
  auto info_enc = encodeAMF0(info);
  response.insert(response.end(), info_enc.begin(), info_enc.end());
  return sendChunk(5, 0, (uint8_t)MessageType::COMMAND_AMF0,
                   stream_info.stream_id, response);
}

bool RTMPSession::handlePlay(
    const std::vector<std::shared_ptr<AMF0Value>> &args) {
  if (args.size() < 4)
    return false;
  if (args[3]->type == AMF0Type::STRING) {
    stream_info.stream_key = args[3]->string;
    stream_info.is_playing = true;
    LOG_INFO("Playing: " + stream_info.stream_key);
  }

  std::vector<uint8_t> stream_begin(6);
  writeUint16BE(stream_begin.data(), (uint16_t)UserControlType::STREAM_BEGIN);
  writeUint32BE(stream_begin.data() + 2, stream_info.stream_id);
  sendChunk(2, 0, (uint8_t)MessageType::USER_CONTROL, 0, stream_begin);

  std::vector<uint8_t> is_recorded(6);
  writeUint16BE(is_recorded.data(),
                (uint16_t)UserControlType::STREAM_IS_RECORDED);
  writeUint32BE(is_recorded.data() + 2, stream_info.stream_id);
  sendChunk(2, 0, (uint8_t)MessageType::USER_CONTROL, 0, is_recorded);

  return sendPlayResponse();
}

bool RTMPSession::sendPlayResponse() {
  {
    std::vector<uint8_t> response;
    auto cmd = encodeAMF0String("onStatus");
    response.insert(response.end(), cmd.begin(), cmd.end());
    auto tid = encodeAMF0Number(0);
    response.insert(response.end(), tid.begin(), tid.end());
    AMF0Value null_val;
    null_val.type = AMF0Type::NULL_TYPE;
    auto null_enc = encodeAMF0(null_val);
    response.insert(response.end(), null_enc.begin(), null_enc.end());

    AMF0Value info;
    info.type = AMF0Type::OBJECT;
    info.object["level"] = std::make_shared<AMF0Value>();
    info.object["level"]->type = AMF0Type::STRING;
    info.object["level"]->string = "status";
    info.object["code"] = std::make_shared<AMF0Value>();
    info.object["code"]->type = AMF0Type::STRING;
    info.object["code"]->string = "NetStream.Play.Reset";
    info.object["description"] = std::make_shared<AMF0Value>();
    info.object["description"]->type = AMF0Type::STRING;
    info.object["description"]->string = "Playing and resetting stream.";
    auto info_enc = encodeAMF0(info);
    response.insert(response.end(), info_enc.begin(), info_enc.end());

    sendChunk(5, 0, (uint8_t)MessageType::COMMAND_AMF0, stream_info.stream_id,
              response);
  }

  {
    std::vector<uint8_t> response;
    auto cmd = encodeAMF0String("onStatus");
    response.insert(response.end(), cmd.begin(), cmd.end());
    auto tid = encodeAMF0Number(0);
    response.insert(response.end(), tid.begin(), tid.end());
    AMF0Value null_val;
    null_val.type = AMF0Type::NULL_TYPE;
    auto null_enc = encodeAMF0(null_val);
    response.insert(response.end(), null_enc.begin(), null_enc.end());

    AMF0Value info;
    info.type = AMF0Type::OBJECT;
    info.object["level"] = std::make_shared<AMF0Value>();
    info.object["level"]->type = AMF0Type::STRING;
    info.object["level"]->string = "status";
    info.object["code"] = std::make_shared<AMF0Value>();
    info.object["code"]->type = AMF0Type::STRING;
    info.object["code"]->string = "NetStream.Play.Start";
    info.object["description"] = std::make_shared<AMF0Value>();
    info.object["description"]->type = AMF0Type::STRING;
    info.object["description"]->string = "Started playing stream.";
    info.object["details"] = std::make_shared<AMF0Value>();
    info.object["details"]->type = AMF0Type::STRING;
    info.object["details"]->string = stream_info.stream_key;
    info.object["clientid"] = std::make_shared<AMF0Value>();
    info.object["clientid"]->type = AMF0Type::NUMBER;
    info.object["clientid"]->number = stream_info.stream_id;
    auto info_enc = encodeAMF0(info);
    response.insert(response.end(), info_enc.begin(), info_enc.end());

    sendChunk(5, 0, (uint8_t)MessageType::COMMAND_AMF0, stream_info.stream_id,
              response);
  }

  {
    std::vector<uint8_t> response;
    auto cmd = encodeAMF0String("|RtmpSampleAccess");
    response.insert(response.end(), cmd.begin(), cmd.end());

    AMF0Value audio_access;
    audio_access.type = AMF0Type::BOOLEAN;
    audio_access.boolean = true;
    auto audio_enc = encodeAMF0(audio_access);
    response.insert(response.end(), audio_enc.begin(), audio_enc.end());

    AMF0Value video_access;
    video_access.type = AMF0Type::BOOLEAN;
    video_access.boolean = true;
    auto video_enc = encodeAMF0(video_access);
    response.insert(response.end(), video_enc.begin(), video_enc.end());

    sendChunk(5, 0, (uint8_t)MessageType::DATA_AMF0, stream_info.stream_id,
              response);
  }

  return true;
}

bool RTMPSession::handleDeleteStream(
    const std::vector<std::shared_ptr<AMF0Value>> &args) {
  stream_info.is_publishing = false;
  stream_info.is_playing = false;
  LOG_INFO("Stream deleted");
  return true;
}

bool RTMPSession::sendErrorResponse(const std::string &command,
                                    double transaction_id,
                                    const std::string &description) {
  std::vector<uint8_t> response;
  auto cmd = encodeAMF0String("_error");
  response.insert(response.end(), cmd.begin(), cmd.end());
  auto tid = encodeAMF0Number(transaction_id);
  response.insert(response.end(), tid.begin(), tid.end());
  AMF0Value null_val;
  null_val.type = AMF0Type::NULL_TYPE;
  auto null_enc = encodeAMF0(null_val);
  response.insert(response.end(), null_enc.begin(), null_enc.end());
  AMF0Value info;
  info.type = AMF0Type::OBJECT;
  info.object["level"] = std::make_shared<AMF0Value>();
  info.object["level"]->type = AMF0Type::STRING;
  info.object["level"]->string = "error";
  info.object["code"] = std::make_shared<AMF0Value>();
  info.object["code"]->type = AMF0Type::STRING;
  info.object["code"]->string = "NetConnection.Call.Failed";
  info.object["description"] = std::make_shared<AMF0Value>();
  info.object["description"]->type = AMF0Type::STRING;
  info.object["description"]->string = description;
  auto info_enc = encodeAMF0(info);
  response.insert(response.end(), info_enc.begin(), info_enc.end());
  return sendChunk(3, 0, (uint8_t)MessageType::COMMAND_AMF0, 0, response);
}

// RTMPServer Implementation
RTMPServer::RTMPServer(int port) : port(port), server_fd(-1), running(false) {}

RTMPServer::~RTMPServer() { stop(); }

bool RTMPServer::start(bool &isRunning) {
  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    LOG_ERROR("Failed to create socket");
    return false;
  }
  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    close(server_fd);
    return false;
  }
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  if (bind(server_fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
    LOG_ERROR("Failed to bind socket");
    close(server_fd);
    return false;
  }
  if (listen(server_fd, 10) < 0) {
    LOG_ERROR("Failed to listen on socket");
    close(server_fd);
    return false;
  }
  running = true;
  accept_thread = std::thread(&RTMPServer::acceptClients, this);
  if (ping_enabled) {
    ping_thread = std::thread(&RTMPServer::pingClientsRoutine, this);
  }
  timeout_thread = std::thread(&RTMPServer::timeoutCheckRoutine, this);
  LOG_INFO("RTMP Server started on port " + std::to_string(port));
  isRunning = true;
  return true;
}

void RTMPServer::stop() {
  if (!running)
    return;
  running = false;
  if (server_fd >= 0) {
    shutdown(server_fd, SHUT_RDWR);
    close(server_fd);
    server_fd = -1;
  }

  {
    std::lock_guard<std::mutex> lock(sessions_mutex);
    for (auto &session : sessions) {
      shutdown(session->getFd(), SHUT_RDWR);
      close(session->getFd());
    }
  }

  if (accept_thread.joinable())
    accept_thread.join();
  if (ping_thread.joinable())
    ping_thread.join();
  if (timeout_thread.joinable())
    timeout_thread.join();

  for (auto &thread : client_threads) {
    if (thread.joinable())
      thread.join();
  }
  client_threads.clear();

  // Clear sessions
  std::lock_guard<std::mutex> lock(sessions_mutex);
  sessions.clear();

  LOG_INFO("RTMP Server stopped");
}

void RTMPServer::acceptClients() {
  while (running) {
    sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (sockaddr *)&client_addr, &addr_len);
    if (client_fd < 0) {
      if (running) {
        LOG_ERROR("Failed to accept client");
      }
      continue;
    }
    std::string client_ip = inet_ntoa(client_addr.sin_addr);
    LOG_INFO("New client connected: " + client_ip);
    // Check connection limit
    {
      std::lock_guard<std::mutex> lock(sessions_mutex);
      if ((int)sessions.size() >= max_total_connections) {
        LOG_WARN("Max connections reached, rejecting client");
        close(client_fd);
        continue;
      }
    }
    auto session = std::make_shared<RTMPSession>(client_fd, client_ip);
    {
      std::lock_guard<std::mutex> lock(sessions_mutex);
      sessions.push_back(session);
    }
    client_threads.emplace_back(&RTMPServer::handleClient, this, session);
  }
}

void RTMPServer::handleClient(std::shared_ptr<RTMPSession> session) {
  if (!session->handshake()) {
    LOG_ERROR("Handshake failed");
    removeSession(session);
    return;
  }
  LOG_INFO("Handshake completed");
  if (on_connect) {
    on_connect(session);
  }
  bool connection_active = true;
  bool publish_notified = false;
  bool play_notified = false;
  while (running && connection_active) {
    if (!session->receiveChunk()) {
      connection_active = false;
      break;
    }
    auto &info = session->getStreamInfo();
    // Handle publish callback
    if (info.is_publishing && !publish_notified) {
      // Check authentication
      if (auth_callback &&
          !auth_callback(info.app, info.stream_key, info.client_ip)) {
        LOG_WARN("Authentication failed for: " + info.app + "/" +
                 info.stream_key);
        session->sendErrorResponse("publish", 0, "Authentication failed");
        connection_active = false;
        break;
      }
      // Check connection limits
      if (!checkConnectionLimits(info.app, info.stream_key, true)) {
        LOG_WARN("Publisher limit reached for: " + info.app + "/" +
                 info.stream_key);
        session->sendErrorResponse("publish", 0, "Publisher limit reached");
        connection_active = false;
        break;
      }
      if (on_publish) {
        on_publish(session, info.app, info.stream_key);
      }
      publish_notified = true;
      // Initialize GOP cache
      if (use_gop_cache) {
        std::string key = makeStreamKey(info.app, info.stream_key);
        std::lock_guard<std::mutex> lock(gop_mutex);
        if (gop_caches.find(key) == gop_caches.end()) {
          gop_caches[key] = std::make_shared<GOPCache>();
        }
      }
    }
    // Handle play callback
    if (info.is_playing && !play_notified) {
      // Check authentication
      if (auth_callback &&
          !auth_callback(info.app, info.stream_key, info.client_ip)) {
        LOG_WARN("Authentication failed for: " + info.app + "/" +
                 info.stream_key);
        session->sendErrorResponse("play", 0, "Authentication failed");
        connection_active = false;
        break;
      }
      // Check connection limits
      if (!checkConnectionLimits(info.app, info.stream_key, false)) {
        LOG_WARN("Player limit reached for: " + info.app + "/" +
                 info.stream_key);
        session->sendErrorResponse("play", 0, "Player limit reached");
        connection_active = false;
        break;
      }
      if (on_play) {
        on_play(session, info.app, info.stream_key);
      }
      play_notified = true;
      // Send GOP cache to new player
      if (use_gop_cache) {
        std::string key = makeStreamKey(info.app, info.stream_key);
        std::lock_guard<std::mutex> lock(gop_mutex);
        auto it = gop_caches.find(key);
        if (it != gop_caches.end() && it->second->hasKeyframe()) {
          it->second->sendToPlayer(session.get());
          LOG_INFO("Sent GOP cache to new player");
        }
      }
    }
    // Process media messages
    if (info.is_publishing) {
      processMediaMessages(session);
    }
  }
  LOG_INFO("Client disconnected");
  // Clean up GOP cache if last publisher
  auto &info = session->getStreamInfo();
  if (info.is_publishing) {
    std::string key = makeStreamKey(info.app, info.stream_key);
    if (countPublishers(info.app, info.stream_key) <= 1) {
      std::lock_guard<std::mutex> lock(gop_mutex);
      gop_caches.erase(key);
    }
  }
  if (on_disconnect) {
    on_disconnect(session);
  }
  removeSession(session);
}

void RTMPServer::processMediaMessages(std::shared_ptr<RTMPSession> session) {
  std::lock_guard<std::mutex> lock(session->getQueueMutex());
  while (!session->getMessageQueue().empty()) {
    RTMPMessage msg = session->getMessageQueue().front();
    session->getMessageQueue().pop();
    MessageType type = (MessageType)msg.header.msg_type_id;
    const auto &info = session->getStreamInfo();
    std::string key = makeStreamKey(info.app, info.stream_key);
    switch (type) {
    case MessageType::AUDIO:
      if (on_audio_data) {
        on_audio_data(session, msg.payload, msg.header.timestamp);
      }
      // Add to GOP cache
      if (use_gop_cache) {
        std::lock_guard<std::mutex> lock(gop_mutex);
        auto it = gop_caches.find(key);
        if (it != gop_caches.end()) {
          it->second->addAudioFrame(msg.payload, msg.header.timestamp);
        }
      }
      // Record if enabled
      {
        std::lock_guard<std::mutex> lock(recorder_mutex);
        auto it = recorders.find(key);
        if (it != recorders.end() && it->second->isRecording()) {
          it->second->writeAudioFrame(msg.payload, msg.header.timestamp);
        }
      }
      // Relay to players
      sendAudioToPlayers(info.app, info.stream_key, msg.payload,
                         msg.header.timestamp);
      break;
    case MessageType::VIDEO:
      if (on_video_data) {
        on_video_data(session, msg.payload, msg.header.timestamp);
      }
      // Add to GOP cache
      if (use_gop_cache) {
        std::lock_guard<std::mutex> lock(gop_mutex);
        auto it = gop_caches.find(key);
        if (it != gop_caches.end()) {
          it->second->addVideoFrame(msg.payload, msg.header.timestamp);
        }
      }
      // Record if enabled
      {
        std::lock_guard<std::mutex> lock(recorder_mutex);
        auto it = recorders.find(key);
        if (it != recorders.end() && it->second->isRecording()) {
          it->second->writeVideoFrame(msg.payload, msg.header.timestamp);
        }
      }
      // Relay to players
      sendVideoToPlayers(info.app, info.stream_key, msg.payload,
                         msg.header.timestamp);
      break;
    case MessageType::DATA_AMF0: {
      size_t offset = 0;
      auto command =
          session->decodeAMF0(msg.payload.data(), msg.payload.size(), offset);
      if (command && command->type == AMF0Type::STRING) {
        if (command->string == "@setDataFrame") {
          auto metadata_name = session->decodeAMF0(msg.payload.data(),
                                                   msg.payload.size(), offset);
        }
        auto metadata_obj =
            session->decodeAMF0(msg.payload.data(), msg.payload.size(), offset);
        if (metadata_obj && (metadata_obj->type == AMF0Type::OBJECT ||
                             metadata_obj->type == AMF0Type::ECMA_ARRAY)) {
          if (on_metadata) {
            on_metadata(session, metadata_obj->object);
          }
          // Add to GOP cache
          if (use_gop_cache) {
            std::lock_guard<std::mutex> lock(gop_mutex);
            auto it = gop_caches.find(key);
            if (it != gop_caches.end()) {
              it->second->addMetadata(msg.payload);
            }
          }
          // Record if enabled
          {
            std::lock_guard<std::mutex> lock(recorder_mutex);
            auto it = recorders.find(key);
            if (it != recorders.end() && it->second->isRecording()) {
              it->second->writeMetadata(metadata_obj->object);
            }
          }
          // Relay metadata to players
          sendMetadataToPlayers(info.app, info.stream_key, msg.payload);
        }
      }
      break;
    }
    default:
      break;
    }
  }
}

void RTMPServer::removeSession(std::shared_ptr<RTMPSession> session) {
  std::lock_guard<std::mutex> lock(sessions_mutex);
  sessions.erase(std::remove(sessions.begin(), sessions.end(), session),
                 sessions.end());
}

bool RTMPServer::sendAudioToPlayers(const std::string &app,
                                    const std::string &stream_key,
                                    const std::vector<uint8_t> &data,
                                    uint32_t timestamp) {
  std::lock_guard<std::mutex> lock(sessions_mutex);
  for (auto &session : sessions) {
    const auto &info = session->getStreamInfo();
    if (info.is_playing && info.app == app && info.stream_key == stream_key) {
      session->sendChunk(4, timestamp, (uint8_t)MessageType::AUDIO,
                         info.stream_id, data);
    }
  }
  return true;
}

bool RTMPServer::sendVideoToPlayers(const std::string &app,
                                    const std::string &stream_key,
                                    const std::vector<uint8_t> &data,
                                    uint32_t timestamp) {
  std::lock_guard<std::mutex> lock(sessions_mutex);
  for (auto &session : sessions) {
    const auto &info = session->getStreamInfo();
    if (info.is_playing && info.app == app && info.stream_key == stream_key) {
      session->sendChunk(6, timestamp, (uint8_t)MessageType::VIDEO,
                         info.stream_id, data);
    }
  }
  return true;
}

void RTMPServer::sendMetadataToPlayers(const std::string &app,
                                       const std::string &stream_key,
                                       const std::vector<uint8_t> &data) {
  std::lock_guard<std::mutex> lock(sessions_mutex);
  for (auto &session : sessions) {
    const auto &info = session->getStreamInfo();
    if (info.is_playing && info.app == app && info.stream_key == stream_key) {
      session->sendChunk(4, 0, (uint8_t)MessageType::DATA_AMF0, info.stream_id,
                         data);
    }
  }
}

void RTMPServer::pingClientsRoutine() {
  while (running) {
    std::this_thread::sleep_for(std::chrono::seconds(ping_interval));
    if (!running)
      break;
    std::lock_guard<std::mutex> lock(sessions_mutex);
    uint32_t timestamp =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
    for (auto &session : sessions) {
      session->sendPing(timestamp);
    }
    LOG_DEBUG("Sent PING to all clients");
  }
}

void RTMPServer::timeoutCheckRoutine() {
  while (running) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    if (!running)
      break;
    auto now = std::chrono::steady_clock::now();
    std::vector<std::shared_ptr<RTMPSession>> to_remove;
    {
      std::lock_guard<std::mutex> lock(sessions_mutex);
      for (auto &session : sessions) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                           now - session->getLastActivity())
                           .count();
        if (elapsed > connection_timeout) {
          LOG_WARN("Session timeout: " + session->getStreamInfo().client_ip);
          to_remove.push_back(session);
        }
      }
    }
    // Remove timed out sessions
    for (auto &session : to_remove) {
      if (on_disconnect) {
        on_disconnect(session);
      }
      removeSession(session);
    }
  }
}

int RTMPServer::countPublishers(const std::string &app,
                                const std::string &stream_key) const {
  std::lock_guard<std::mutex> lock(sessions_mutex);
  int count = 0;
  for (const auto &session : sessions) {
    const auto &info = session->getStreamInfo();
    if (info.is_publishing && info.app == app &&
        info.stream_key == stream_key) {
      count++;
    }
  }
  return count;
}

int RTMPServer::countPlayers(const std::string &app,
                             const std::string &stream_key) const {
  std::lock_guard<std::mutex> lock(sessions_mutex);
  int count = 0;
  for (const auto &session : sessions) {
    const auto &info = session->getStreamInfo();
    if (info.is_playing && info.app == app && info.stream_key == stream_key) {
      count++;
    }
  }
  return count;
}

bool RTMPServer::checkConnectionLimits(const std::string &app,
                                       const std::string &stream_key,
                                       bool is_publisher) const {
  if (is_publisher) {
    int current = countPublishers(app, stream_key);
    return current >= max_publishers_per_stream; // FIXED: Used propper Op
  } else {
    int current = countPlayers(app, stream_key);
    return current <= max_players_per_stream; // FIXED: Used propper Op
  }
}

int RTMPServer::getActivePublishers() const {
  std::lock_guard<std::mutex> lock(sessions_mutex);
  int count = 0;
  for (const auto &session : sessions) {
    if (session->getStreamInfo().is_publishing) {
      count++;
    }
  }
  return count;
}

int RTMPServer::getActivePlayers() const {
  std::lock_guard<std::mutex> lock(sessions_mutex);
  int count = 0;
  for (const auto &session : sessions) {
    if (session->getStreamInfo().is_playing) {
      count++;
    }
  }
  return count;
}

int RTMPServer::getTotalConnections() const {
  std::lock_guard<std::mutex> lock(sessions_mutex);
  return sessions.size();
}

StreamStatistics
RTMPServer::getStreamStats(const std::string &app,
                           const std::string &stream_key) const {
  std::lock_guard<std::mutex> lock(sessions_mutex);
  StreamStatistics combined;
  for (const auto &session : sessions) {
    const auto &info = session->getStreamInfo();
    if (info.app == app && info.stream_key == stream_key) {
      const auto &stats = session->getStats();
      combined.bytes_sent += stats.bytes_sent;
      combined.bytes_received += stats.bytes_received;
      combined.video_frames += stats.video_frames;
      combined.audio_frames += stats.audio_frames;
      combined.dropped_frames += stats.dropped_frames;
    }
  }
  return combined;
}

std::vector<std::pair<std::string, StreamStatistics>>
RTMPServer::getAllStreamStats() const {
  std::lock_guard<std::mutex> lock(sessions_mutex);
  std::map<std::string, StreamStatistics> stats_map;
  for (const auto &session : sessions) {
    const auto &info = session->getStreamInfo();
    if (info.is_publishing || info.is_playing) {
      std::string key = makeStreamKey(info.app, info.stream_key);
      const auto &stats = session->getStats();
      auto &combined = stats_map[key];
      combined.bytes_sent += stats.bytes_sent;
      combined.bytes_received += stats.bytes_received;
      combined.video_frames += stats.video_frames;
      combined.audio_frames += stats.audio_frames;
      combined.dropped_frames += stats.dropped_frames;
    }
  }
  std::vector<std::pair<std::string, StreamStatistics>> result;
  for (const auto &pair : stats_map) {
    result.push_back(pair);
  }
  return result;
}

bool RTMPServer::startRecording(const std::string &app,
                                const std::string &stream_key,
                                const std::string &filename) {
  std::string key = makeStreamKey(app, stream_key);
  std::lock_guard<std::mutex> lock(recorder_mutex);
  if (recorders.find(key) != recorders.end() && recorders[key]->isRecording()) {
    LOG_WARN("Already recording stream: " + key);
    return false;
  }
  auto recorder = std::make_shared<FLVRecorder>(filename);
  if (!recorder->start()) {
    LOG_ERROR("Failed to start recording: " + filename);
    return false;
  }
  recorders[key] = recorder;
  LOG_INFO("Started recording " + key + " to " + filename);
  return true;
}

void RTMPServer::stopRecording(const std::string &app,
                               const std::string &stream_key) {
  std::string key = makeStreamKey(app, stream_key);
  std::lock_guard<std::mutex> lock(recorder_mutex);
  auto it = recorders.find(key);
  if (it != recorders.end()) {
    it->second->stop();
    recorders.erase(it);
    LOG_INFO("Stopped recording: " + key);
  }
}

bool RTMPServer::isRecording(const std::string &app,
                             const std::string &stream_key) const {
  std::string key = makeStreamKey(app, stream_key);
  std::lock_guard<std::mutex> lock(recorder_mutex);
  auto it = recorders.find(key);
  return (it != recorders.end() && it->second->isRecording());
}

void RTMPServer::enablePingPong(bool enable, int interval_seconds) {
  ping_enabled = enable;
  ping_interval = interval_seconds;
  if (enable && running && !ping_thread.joinable()) {
    ping_thread = std::thread(&RTMPServer::pingClientsRoutine, this);
  }
}

} // namespace rtmp