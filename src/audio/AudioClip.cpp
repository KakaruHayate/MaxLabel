#include "AudioClip.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace maxlabel {

namespace {

// Appends one decoded buffer to `out` as mono floats.  The decoder does not
// always honour the requested format, so every layout Qt can hand back is
// handled rather than assuming one.
void append_as_mono(const QAudioBuffer & buffer, std::vector<float> & out) {
    const QAudioFormat format = buffer.format();
    const int channels = format.channelCount();
    if (channels <= 0) return;

    const int frames = buffer.frameCount();
    if (frames <= 0) return;

    const auto frame_value = [&](int frame, int channel) -> float {
        switch (format.sampleFormat()) {
            case QAudioFormat::UInt8: {
                const auto * data = reinterpret_cast<const std::uint8_t *>(buffer.constData());
                return (static_cast<float>(data[frame * channels + channel]) - 128.0f) / 128.0f;
            }
            case QAudioFormat::Int16: {
                const auto * data = reinterpret_cast<const std::int16_t *>(buffer.constData());
                return static_cast<float>(data[frame * channels + channel]) / 32768.0f;
            }
            case QAudioFormat::Int32: {
                const auto * data = reinterpret_cast<const std::int32_t *>(buffer.constData());
                return static_cast<float>(data[frame * channels + channel]) /
                       static_cast<float>(std::int32_t{ 1 } << 30);
            }
            case QAudioFormat::Float: {
                const auto * data = reinterpret_cast<const float *>(buffer.constData());
                return data[frame * channels + channel];
            }
            default:
                return 0.0f;
        }
    };

    const std::size_t base = out.size();
    out.resize(base + static_cast<std::size_t>(frames));
    for (int frame = 0; frame < frames; ++frame) {
        float sum = 0.0f;
        for (int channel = 0; channel < channels; ++channel) sum += frame_value(frame, channel);
        out[base + static_cast<std::size_t>(frame)] = sum / static_cast<float>(channels);
    }
}

}  // namespace

AudioClip::AudioClip(QObject * parent) : QObject(parent) {}

AudioClip::~AudioClip() = default;

void AudioClip::clear() {
    if (decoder_ != nullptr) {
        decoder_->stop();
        decoder_.reset();
    }
    path_.clear();
    samples_.clear();
    sample_rate_ = 0;
    ready_ = false;
}

void AudioClip::load(const QString & path) {
    clear();

    decoder_ = std::make_unique<QAudioDecoder>(this);

    // Ask for what the waveform wants; whatever comes back is converted.
    QAudioFormat requested;
    requested.setSampleFormat(QAudioFormat::Float);
    requested.setChannelCount(1);
    decoder_->setAudioFormat(requested);
    decoder_->setSource(QUrl::fromLocalFile(path));

    connect(decoder_.get(), &QAudioDecoder::bufferReady, this, [this]() {
        const QAudioBuffer buffer = decoder_->read();
        if (sample_rate_ == 0) sample_rate_ = buffer.format().sampleRate();
        append_as_mono(buffer, samples_);
    });
    connect(decoder_.get(), &QAudioDecoder::finished, this, [this, path]() {
        if (samples_.empty()) {
            emit failed(tr("no audio decoded from %1").arg(path));
            return;
        }
        ready_ = true;
        path_ = path;
        emit loaded();
    });
    connect(decoder_.get(), &QAudioDecoder::errorOccurred, this,
            [this](QAudioDecoder::Error) {
                emit failed(decoder_ != nullptr ? decoder_->errorString()
                                                : tr("decoder failed"));
            });

    decoder_->start();
}

double AudioClip::duration() const {
    if (sample_rate_ <= 0) return 0.0;
    return static_cast<double>(samples_.size()) / static_cast<double>(sample_rate_);
}

float AudioClip::peak(double begin, double end) const {
    if (sample_rate_ <= 0 || samples_.empty()) return 0.0f;
    const auto from = static_cast<std::size_t>(
        std::max(0.0, begin) * static_cast<double>(sample_rate_));
    const auto to = static_cast<std::size_t>(
        std::min(end, duration()) * static_cast<double>(sample_rate_));
    if (from >= to || from >= samples_.size()) return 0.0f;

    float peak = 0.0f;
    const std::size_t last = std::min(to, samples_.size());
    for (std::size_t i = from; i < last; ++i) {
        peak = std::max(peak, std::fabs(samples_[i]));
    }
    return peak;
}

}  // namespace maxlabel
