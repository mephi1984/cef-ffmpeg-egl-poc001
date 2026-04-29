#include "video_decoder.h"

#include <algorithm>
#include <cstdio>

extern "C" {
#include <libavutil/imgutils.h>
}

namespace {

void LogAvError(const char* what, int err) {
    char buf[256] = {};
    av_strerror(err, buf, sizeof(buf));
    std::fprintf(stderr, "[video] %s: %s\n", what, buf);
}

AVFrame* AllocBgraFrame(int w, int h) {
    AVFrame* f = av_frame_alloc();
    if (!f) return nullptr;
    f->format = AV_PIX_FMT_BGRA;
    f->width  = w;
    f->height = h;
    // align=1 -> tightly packed rows (linesize == width*4). That makes the
    // buffer trivial to feed to glTexImage2D without GL_UNPACK_ROW_LENGTH.
    int err = av_frame_get_buffer(f, 1);
    if (err < 0) {
        av_frame_free(&f);
        return nullptr;
    }
    return f;
}

} // namespace

VideoDecoder::~VideoDecoder() {
    if (m_sws)     sws_freeContext(m_sws);
    if (m_codec)   avcodec_free_context(&m_codec);
    if (m_fmt)     avformat_close_input(&m_fmt);
    if (m_yuv)     av_frame_free(&m_yuv);
    if (m_pending) av_frame_free(&m_pending);
    if (m_current) av_frame_free(&m_current);
    if (m_packet)  av_packet_free(&m_packet);
}

bool VideoDecoder::Open(const std::string& path) {
    int err = avformat_open_input(&m_fmt, path.c_str(), nullptr, nullptr);
    if (err < 0) { LogAvError("avformat_open_input", err); return false; }

    err = avformat_find_stream_info(m_fmt, nullptr);
    if (err < 0) { LogAvError("avformat_find_stream_info", err); return false; }

    m_streamIdx = av_find_best_stream(m_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_streamIdx < 0) {
        std::fprintf(stderr, "[video] no video stream in %s\n", path.c_str());
        return false;
    }

    AVStream* stream = m_fmt->streams[m_streamIdx];
    const AVCodec* dec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!dec) {
        std::fprintf(stderr, "[video] no decoder for codec id %d\n",
                     stream->codecpar->codec_id);
        return false;
    }

    m_codec = avcodec_alloc_context3(dec);
    if (!m_codec) return false;
    if ((err = avcodec_parameters_to_context(m_codec, stream->codecpar)) < 0) {
        LogAvError("avcodec_parameters_to_context", err); return false;
    }
    if ((err = avcodec_open2(m_codec, dec, nullptr)) < 0) {
        LogAvError("avcodec_open2", err); return false;
    }

    m_width  = m_codec->width;
    m_height = m_codec->height;
    m_streamTimeBase = av_q2d(stream->time_base);
    m_streamDuration = (stream->duration != AV_NOPTS_VALUE)
        ? stream->duration * m_streamTimeBase
        : (m_fmt->duration > 0 ? m_fmt->duration / (double)AV_TIME_BASE : 0.0);

    m_sws = sws_getContext(
        m_width, m_height, m_codec->pix_fmt,
        m_width, m_height, AV_PIX_FMT_BGRA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_sws) {
        std::fprintf(stderr, "[video] sws_getContext failed\n");
        return false;
    }

    m_yuv     = av_frame_alloc();
    m_pending = AllocBgraFrame(m_width, m_height);
    m_current = AllocBgraFrame(m_width, m_height);
    m_packet  = av_packet_alloc();
    if (!m_yuv || !m_pending || !m_current || !m_packet) {
        std::fprintf(stderr, "[video] av_frame_alloc/av_packet_alloc failed\n");
        return false;
    }

    // Pre-decode the first frame so the texture has content from frame 0.
    if (!DecodeNextPending()) {
        std::fprintf(stderr, "[video] failed to decode initial frame\n");
        return false;
    }

    std::fprintf(stderr, "[video] %s: %dx%d, codec=%s, duration=%.2fs\n",
                 path.c_str(), m_width, m_height, dec->name, m_streamDuration);
    return true;
}

bool VideoDecoder::DecodeNextPending() {
    // Read+decode until we get one frame, or until the stream is fully drained.
    for (;;) {
        int err = avcodec_receive_frame(m_codec, m_yuv);
        if (err == 0) {
            // Got a frame. Convert to BGRA into m_pending.
            sws_scale(m_sws,
                      m_yuv->data, m_yuv->linesize,
                      0, m_height,
                      m_pending->data, m_pending->linesize);

            int64_t pts = m_yuv->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE) pts = m_yuv->pts;
            const double seconds =
                (pts == AV_NOPTS_VALUE) ? 0.0 : pts * m_streamTimeBase;
            m_pendingPtsSeconds = seconds + m_loopOffset;
            m_havePending = true;
            return true;
        }
        if (err == AVERROR_EOF) {
            // Decoder fully drained — loop.
            SeekToStart();
            continue;
        }
        if (err != AVERROR(EAGAIN)) {
            LogAvError("avcodec_receive_frame", err);
            return false;
        }

        // EAGAIN: feed more packets.
        if (m_eof) {
            // No more packets, but decoder isn't drained yet — flush.
            avcodec_send_packet(m_codec, nullptr);
            continue;
        }

        err = av_read_frame(m_fmt, m_packet);
        if (err == AVERROR_EOF) {
            m_eof = true;
            avcodec_send_packet(m_codec, nullptr);
            continue;
        }
        if (err < 0) { LogAvError("av_read_frame", err); return false; }

        if (m_packet->stream_index == m_streamIdx) {
            err = avcodec_send_packet(m_codec, m_packet);
            if (err < 0 && err != AVERROR(EAGAIN)) {
                LogAvError("avcodec_send_packet", err);
            }
        }
        av_packet_unref(m_packet);
    }
}

void VideoDecoder::SeekToStart() {
    // Bump the playback offset so the next frame's effective PTS continues
    // monotonically. We use stream duration (or fall back to the last
    // pending PTS) so the loop joins seamlessly.
    const double last = m_pendingPtsSeconds;
    const double advance =
        (m_streamDuration > 0.0) ? m_streamDuration : (last - m_loopOffset);
    m_loopOffset += std::max(advance, 1.0 / 60.0);

    av_seek_frame(m_fmt, m_streamIdx, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_codec);
    m_eof = false;
}

bool VideoDecoder::Advance(double now) {
    bool committed = false;
    while (m_havePending && m_pendingPtsSeconds <= now) {
        // Commit pending -> current. av_frame_ref / unref would also work,
        // but the refcount swap is fiddly; copying ~1MB at 24fps is cheap.
        std::swap(m_pending, m_current);
        m_havePending = false;
        committed = true;

        if (!DecodeNextPending()) {
            // Decoder broke; stop trying. Caller will keep showing m_current.
            break;
        }
    }
    return committed;
}

const uint8_t* VideoDecoder::current_bgra() const {
    return m_current ? m_current->data[0] : nullptr;
}
