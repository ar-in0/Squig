#ifndef RTMP_CAPI_H
#define RTMP_CAPI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *RtmpServerHandle;

enum RtmpLogLevel {
  RTMP_LOG_ERROR = 0,
  RTMP_LOG_WARN = 1,
  RTMP_LOG_INFO = 2,
  RTMP_LOG_DEBUG = 3
};

struct RtmpStreamStats {
  uint64_t bytes_sent;
  uint64_t bytes_received;
  uint32_t video_frames;
  uint32_t audio_frames;
  uint32_t dropped_frames;
  double bitrate_kbps;
  double uptime_seconds;
};

typedef void (*RtmpOnConnectCallback)(const char *client_ip, void *user_data);
typedef void (*RtmpOnPublishCallback)(const char *client_ip, const char *app,
                                      const char *stream_key, void *user_data);
typedef void (*RtmpOnPlayCallback)(const char *client_ip, const char *app,
                                   const char *stream_key, void *user_data);
typedef void (*RtmpOnAudioDataCallback)(const char *app, const char *stream_key,
                                        const uint8_t *data, uint32_t length,
                                        uint32_t timestamp, void *user_data);
typedef void (*RtmpOnVideoDataCallback)(const char *app, const char *stream_key,
                                        const uint8_t *data, uint32_t length,
                                        uint32_t timestamp, void *user_data);
typedef void (*RtmpOnDisconnectCallback)(const char *client_ip, const char *app,
                                         const char *stream_key,
                                         bool was_publishing, bool was_playing,
                                         void *user_data);
typedef bool (*RtmpAuthCallback)(const char *app, const char *stream_key,
                                 const char *client_ip, void *user_data);

// Create and destroy
RtmpServerHandle rtmp_server_create(int port);
void rtmp_server_destroy(RtmpServerHandle handle);
bool rtmp_server_start(RtmpServerHandle handle, bool* isRunning);
void rtmp_server_stop(RtmpServerHandle handle);
bool rtmp_server_is_running(RtmpServerHandle handle);

// Callbacks
void rtmp_server_set_on_connect(RtmpServerHandle handle,
                                RtmpOnConnectCallback cb, void *user_data);
void rtmp_server_set_on_publish(RtmpServerHandle handle,
                                RtmpOnPublishCallback cb, void *user_data);
void rtmp_server_set_on_play(RtmpServerHandle handle, RtmpOnPlayCallback cb,
                             void *user_data);
void rtmp_server_set_on_audio_data(RtmpServerHandle handle,
                                   RtmpOnAudioDataCallback cb, void *user_data);
void rtmp_server_set_on_video_data(RtmpServerHandle handle,
                                   RtmpOnVideoDataCallback cb, void *user_data);
void rtmp_server_set_on_disconnect(RtmpServerHandle handle,
                                   RtmpOnDisconnectCallback cb,
                                   void *user_data);
void rtmp_server_set_auth_callback(RtmpServerHandle handle, RtmpAuthCallback cb,
                                   void *user_data);

// Configuration
void rtmp_server_enable_gop_cache(RtmpServerHandle handle, bool enable);
void rtmp_server_set_max_publishers_per_stream(RtmpServerHandle handle,
                                               int max);
void rtmp_server_set_max_players_per_stream(RtmpServerHandle handle, int max);
void rtmp_server_set_max_total_connections(RtmpServerHandle handle, int max);
void rtmp_server_set_connection_timeout(RtmpServerHandle handle, int seconds);
void rtmp_server_enable_ping_pong(RtmpServerHandle handle, bool enable,
                                  int interval_seconds);

// Stats
int rtmp_server_get_active_publishers(RtmpServerHandle handle);
int rtmp_server_get_active_players(RtmpServerHandle handle);
int rtmp_server_get_total_connections(RtmpServerHandle handle);
struct RtmpStreamStats rtmp_server_get_stream_stats(RtmpServerHandle handle,
                                                    const char *app,
                                                    const char *stream_key);

// Recording
bool rtmp_server_start_recording(RtmpServerHandle handle, const char *app,
                                 const char *stream_key, const char *filename);
void rtmp_server_stop_recording(RtmpServerHandle handle, const char *app,
                                const char *stream_key);
bool rtmp_server_is_recording(RtmpServerHandle handle, const char *app,
                              const char *stream_key);

// Broadcasting
bool rtmp_server_broadcast_audio(RtmpServerHandle handle, const char *app,
                                 const char *stream_key, const uint8_t *data,
                                 uint32_t length, uint32_t timestamp);
bool rtmp_server_broadcast_video(RtmpServerHandle handle, const char *app,
                                 const char *stream_key, const uint8_t *data,
                                 uint32_t length, uint32_t timestamp);
// FIXED: Added missing declaration
bool rtmp_server_broadcast_metadata(RtmpServerHandle handle, const char *app,
                                    const char *stream_key, const uint8_t *data,
                                    uint32_t length);

// Logger
void rtmp_logger_set_level(enum RtmpLogLevel level);

#ifdef __cplusplus
}
#endif

#endif // RTMP_CAPI_H