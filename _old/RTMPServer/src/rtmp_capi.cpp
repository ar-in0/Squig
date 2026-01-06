#include "../include/rtmp_capi.hpp"
#include "../include/rtmp_server.hpp"
#include <string>
#include <vector>

namespace rtmp_capi_internal {
struct RtmpServerImpl {
  rtmp::RTMPServer *server;
  RtmpOnConnectCallback on_connect_cb;
  void *on_connect_userdata;
  RtmpOnPublishCallback on_publish_cb;
  void *on_publish_userdata;
  RtmpOnPlayCallback on_play_cb;
  void *on_play_userdata;
  RtmpOnAudioDataCallback on_audio_cb;
  void *on_audio_userdata;
  RtmpOnVideoDataCallback on_video_cb;
  void *on_video_userdata;
  RtmpOnDisconnectCallback on_disconnect_cb;
  void *on_disconnect_userdata;
  RtmpAuthCallback auth_cb;
  void *auth_userdata;
  RtmpServerImpl()
      : server(nullptr), on_connect_cb(nullptr), on_connect_userdata(nullptr),
        on_publish_cb(nullptr), on_publish_userdata(nullptr),
        on_play_cb(nullptr), on_play_userdata(nullptr), on_audio_cb(nullptr),
        on_audio_userdata(nullptr), on_video_cb(nullptr),
        on_video_userdata(nullptr), on_disconnect_cb(nullptr),
        on_disconnect_userdata(nullptr), auth_cb(nullptr),
        auth_userdata(nullptr) {}
};
} // namespace rtmp_capi_internal

using Impl = rtmp_capi_internal::RtmpServerImpl;

extern "C" {
RtmpServerHandle rtmp_server_create(int port) {
  Impl *impl = new Impl();
  impl->server = new rtmp::RTMPServer(port);
  impl->server->setOnConnect(
      [impl](std::shared_ptr<rtmp::RTMPSession> session) {
        if (!impl || !impl->on_connect_cb)
          return;
        const auto &info = session->getStreamInfo();
        impl->on_connect_cb(info.client_ip.c_str(), impl->on_connect_userdata);
      });
  impl->server->setOnPublish([impl](std::shared_ptr<rtmp::RTMPSession> session,
                                    const std::string &app,
                                    const std::string &stream_key) {
    if (!impl || !impl->on_publish_cb)
      return;
    const auto &info = session->getStreamInfo();
    impl->on_publish_cb(info.client_ip.c_str(), app.c_str(), stream_key.c_str(),
                        impl->on_publish_userdata);
  });
  impl->server->setOnPlay([impl](std::shared_ptr<rtmp::RTMPSession> session,
                                 const std::string &app,
                                 const std::string &stream_key) {
    if (!impl || !impl->on_play_cb)
      return;
    const auto &info = session->getStreamInfo();
    impl->on_play_cb(info.client_ip.c_str(), app.c_str(), stream_key.c_str(),
                     impl->on_play_userdata);
  });
  impl->server->setOnAudioData(
      [impl](std::shared_ptr<rtmp::RTMPSession> session,
             const std::vector<uint8_t> &data, uint32_t timestamp) {
        if (!impl || !impl->on_audio_cb)
          return;
        const auto &info = session->getStreamInfo();
        impl->on_audio_cb(info.app.c_str(), info.stream_key.c_str(),
                          data.data(), static_cast<uint32_t>(data.size()),
                          timestamp, impl->on_audio_userdata);
      });
  impl->server->setOnVideoData(
      [impl](std::shared_ptr<rtmp::RTMPSession> session,
             const std::vector<uint8_t> &data, uint32_t timestamp) {
        if (!impl || !impl->on_video_cb)
          return;
        const auto &info = session->getStreamInfo();
        impl->on_video_cb(info.app.c_str(), info.stream_key.c_str(),
                          data.data(), static_cast<uint32_t>(data.size()),
                          timestamp, impl->on_video_userdata);
      });
  impl->server->setOnDisconnect(
      [impl](std::shared_ptr<rtmp::RTMPSession> session) {
        if (!impl || !impl->on_disconnect_cb)
          return;
        const auto &info = session->getStreamInfo();
        impl->on_disconnect_cb(info.client_ip.c_str(), info.app.c_str(),
                               info.stream_key.c_str(), info.is_publishing,
                               info.is_playing, impl->on_disconnect_userdata);
      });
  impl->server->setAuthCallback([impl](const std::string &app,
                                       const std::string &stream_key,
                                       const std::string &client_ip) -> bool {
    if (!impl || !impl->auth_cb)
      return true;
    return impl->auth_cb(app.c_str(), stream_key.c_str(), client_ip.c_str(),
                         impl->auth_userdata);
  });
  return impl;
}
// add all other functions as above
void rtmp_server_destroy(RtmpServerHandle handle) {
  if (!handle)
    return;
  Impl *impl = static_cast<Impl *>(handle);
  delete impl->server;
  delete impl;
}
bool rtmp_server_start(RtmpServerHandle handle, bool *isRunning) {
  if (!handle || !isRunning)
    return false;

  Impl *impl = static_cast<Impl *>(handle);
  bool result = impl->server->start(*isRunning);
  return result;
}

void rtmp_server_stop(RtmpServerHandle handle) {
  if (!handle)
    return;
  Impl *impl = static_cast<Impl *>(handle);
  impl->server->stop();
}
bool rtmp_server_is_running(RtmpServerHandle handle) {
  if (!handle)
    return false;
  Impl *impl = static_cast<Impl *>(handle);
  return impl->server->isRunning();
}
void rtmp_server_set_on_connect(RtmpServerHandle handle,
                                RtmpOnConnectCallback cb, void *user_data) {
  if (!handle)
    return;
  Impl *impl = static_cast<Impl *>(handle);
  impl->on_connect_cb = cb;
  impl->on_connect_userdata = user_data;
}
void rtmp_server_set_on_publish(RtmpServerHandle handle,
                                RtmpOnPublishCallback cb, void *user_data) {
  if (!handle)
    return;
  Impl *impl = static_cast<Impl *>(handle);
  impl->on_publish_cb = cb;
  impl->on_publish_userdata = user_data;
}
void rtmp_server_set_on_play(RtmpServerHandle handle, RtmpOnPlayCallback cb,
                             void *user_data) {
  if (!handle)
    return;
  Impl *impl = static_cast<Impl *>(handle);
  impl->on_play_cb = cb;
  impl->on_play_userdata = user_data;
}
void rtmp_server_set_on_audio_data(RtmpServerHandle handle,
                                   RtmpOnAudioDataCallback cb,
                                   void *user_data) {
  if (!handle)
    return;
  Impl *impl = static_cast<Impl *>(handle);
  impl->on_audio_cb = cb;
  impl->on_audio_userdata = user_data;
}
void rtmp_server_set_on_video_data(RtmpServerHandle handle,
                                   RtmpOnVideoDataCallback cb,
                                   void *user_data) {
  if (!handle)
    return;
  Impl *impl = static_cast<Impl *>(handle);
  impl->on_video_cb = cb;
  impl->on_video_userdata = user_data;
}
void rtmp_server_set_on_disconnect(RtmpServerHandle handle,
                                   RtmpOnDisconnectCallback cb,
                                   void *user_data) {
  if (!handle)
    return;
  Impl *impl = static_cast<Impl *>(handle);
  impl->on_disconnect_cb = cb;
  impl->on_disconnect_userdata = user_data;
}
void rtmp_server_set_auth_callback(RtmpServerHandle handle, RtmpAuthCallback cb,
                                   void *user_data) {
  if (!handle)
    return;
  Impl *impl = static_cast<Impl *>(handle);
  impl->auth_cb = cb;
  impl->auth_userdata = user_data;
}
void rtmp_server_enable_gop_cache(RtmpServerHandle handle, bool enable) {
  if (!handle)
    return;
  static_cast<Impl *>(handle)->server->enableGOPCache(enable);
}
void rtmp_server_set_max_publishers_per_stream(RtmpServerHandle handle,
                                               int max) {
  if (!handle)
    return;
  static_cast<Impl *>(handle)->server->setMaxPublishersPerStream(max);
}
void rtmp_server_set_max_players_per_stream(RtmpServerHandle handle, int max) {
  if (!handle)
    return;
  static_cast<Impl *>(handle)->server->setMaxPlayersPerStream(max);
}
void rtmp_server_set_max_total_connections(RtmpServerHandle handle, int max) {
  if (!handle)
    return;
  static_cast<Impl *>(handle)->server->setMaxTotalConnections(max);
}
void rtmp_server_set_connection_timeout(RtmpServerHandle handle, int seconds) {
  if (!handle)
    return;
  static_cast<Impl *>(handle)->server->setConnectionTimeout(seconds);
}
void rtmp_server_enable_ping_pong(RtmpServerHandle handle, bool enable,
                                  int interval_seconds) {
  if (!handle)
    return;
  static_cast<Impl *>(handle)->server->enablePingPong(enable, interval_seconds);
}
int rtmp_server_get_active_publishers(RtmpServerHandle handle) {
  if (!handle)
    return 0;
  return static_cast<Impl *>(handle)->server->getActivePublishers();
}
int rtmp_server_get_active_players(RtmpServerHandle handle) {
  if (!handle)
    return 0;
  return static_cast<Impl *>(handle)->server->getActivePlayers();
}
int rtmp_server_get_total_connections(RtmpServerHandle handle) {
  if (!handle)
    return 0;
  return static_cast<Impl *>(handle)->server->getTotalConnections();
}
struct RtmpStreamStats rtmp_server_get_stream_stats(RtmpServerHandle handle,
                                                    const char *app,
                                                    const char *stream_key) {
  struct RtmpStreamStats stats = {0};
  if (!handle || !app || !stream_key)
    return stats;
  Impl *impl = static_cast<Impl *>(handle);
  auto cstats = impl->server->getStreamStats(app, stream_key);
  stats.bytes_sent = cstats.bytes_sent;
  stats.bytes_received = cstats.bytes_received;
  stats.video_frames = cstats.video_frames;
  stats.audio_frames = cstats.audio_frames;
  stats.dropped_frames = cstats.dropped_frames;
  stats.bitrate_kbps = cstats.getBitrate();
  stats.uptime_seconds = cstats.getUptime();
  return stats;
}
bool rtmp_server_start_recording(RtmpServerHandle handle, const char *app,
                                 const char *stream_key, const char *filename) {
  if (!handle || !app || !stream_key || !filename)
    return false;
  return static_cast<Impl *>(handle)->server->startRecording(app, stream_key,
                                                             filename);
}
void rtmp_server_stop_recording(RtmpServerHandle handle, const char *app,
                                const char *stream_key) {
  if (!handle || !app || !stream_key)
    return;
  static_cast<Impl *>(handle)->server->stopRecording(app, stream_key);
}
bool rtmp_server_is_recording(RtmpServerHandle handle, const char *app,
                              const char *stream_key) {
  if (!handle || !app || !stream_key)
    return false;
  return static_cast<Impl *>(handle)->server->isRecording(app, stream_key);
}
bool rtmp_server_broadcast_audio(RtmpServerHandle handle, const char *app,
                                 const char *stream_key, const uint8_t *data,
                                 uint32_t length, uint32_t timestamp) {
  if (!handle || !app || !stream_key || !data || length == 0)
    return false;
  Impl *impl = static_cast<Impl *>(handle);
  std::vector<uint8_t> vec(data, data + length);
  return impl->server->sendAudioToPlayers(app, stream_key, vec, timestamp);
}
bool rtmp_server_broadcast_video(RtmpServerHandle handle, const char *app,
                                 const char *stream_key, const uint8_t *data,
                                 uint32_t length, uint32_t timestamp) {
  if (!handle || !app || !stream_key || !data || length == 0)
    return false;
  Impl *impl = static_cast<Impl *>(handle);
  std::vector<uint8_t> vec(data, data + length);
  return impl->server->sendVideoToPlayers(app, stream_key, vec, timestamp);
}
bool rtmp_server_broadcast_metadata(RtmpServerHandle handle, const char *app,
                                    const char *stream_key, const uint8_t *data,
                                    uint32_t length) {
  if (!handle || !app || !stream_key || !data || length == 0)
    return false;
  Impl *impl = static_cast<Impl *>(handle);
  std::vector<uint8_t> vec(data, data + length);
  impl->server->sendMetadataToPlayers(app, stream_key, vec);
  return true;
}
void rtmp_logger_set_level(RtmpLogLevel level) {
  rtmp::Logger::getInstance().setLevel((rtmp::LogLevel)level);
}
}