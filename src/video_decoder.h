#pragma once

#include <cstdint>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

// Decodes a video file via FFmpeg, converts each frame to BGRA, and exposes
// the latest frame as a tightly packed buffer. Loops automatically: when the
// stream reaches EOF, seeks back to the start and continues.
class VideoDecoder {
public:
    VideoDecoder() = default;
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool Open(const std::string& path);

    int width()  const { return m_width; }
    int height() const { return m_height; }

    // Drive the decoder forward to playback time `now` (seconds, monotonic
    // since Open()). Decodes/converts as many frames as needed to catch up,
    // committing only the most recent one. Returns true if the current frame
    // changed (caller should re-upload to the GL texture).
    bool Advance(double now);

    // Pointer to the current BGRA frame; valid until the next Advance() that
    // returns true. Stride is `width()*4` (we ask FFmpeg for tight packing).
    const uint8_t* current_bgra() const;

private:
    bool DecodeNextPending();
    void SeekToStart();

    AVFormatContext* m_fmt   = nullptr;
    AVCodecContext*  m_codec = nullptr;
    SwsContext*      m_sws   = nullptr;
    AVFrame*         m_yuv     = nullptr;   // raw decoded frame (codec format)
    AVFrame*         m_pending = nullptr;   // BGRA, decoded but not yet shown
    AVFrame*         m_current = nullptr;   // BGRA, currently visible
    AVPacket*        m_packet  = nullptr;

    int    m_streamIdx = -1;
    int    m_width  = 0;
    int    m_height = 0;
    double m_streamTimeBase = 0.0;     // av_q2d(stream->time_base)
    double m_streamDuration = 0.0;     // seconds, best-effort
    double m_loopOffset = 0.0;         // accumulated playback time across loops

    bool   m_havePending = false;
    double m_pendingPtsSeconds = 0.0;  // PTS of m_pending (with loop offset baked in)
    bool   m_eof = false;              // demuxer has hit EOF, draining decoder
};
