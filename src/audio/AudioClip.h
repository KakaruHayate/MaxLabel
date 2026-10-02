#pragma once

// Decoding an audio file to samples the waveform can draw and the player can
// push out.
//
// Decoded once, mono, at the file's own rate: the waveform wants one channel
// and the transport wants a buffer it can seek in, and re-decoding for either
// would only add a second source of truth for where we are.

#include <QObject>
#include <QString>

#include <memory>
#include <vector>

class QAudioDecoder;

namespace maxlabel {

class AudioClip : public QObject {
    Q_OBJECT

public:
    explicit AudioClip(QObject * parent = nullptr);
    ~AudioClip() override;

    // Starts decoding; the result arrives through `loaded` or `failed`.
    void load(const QString & path);
    void clear();

    bool ready() const { return ready_; }
    const QString & path() const { return path_; }
    const std::vector<float> & samples() const { return samples_; }
    int sampleRate() const { return sample_rate_; }
    double duration() const;   // seconds; 0 when nothing is loaded

    // Peak magnitude over [begin, end) seconds, for a coarse level readout.
    float peak(double begin, double end) const;

signals:
    void loaded();
    void failed(const QString & message);

private:
    std::unique_ptr<QAudioDecoder> decoder_;
    QString path_;
    std::vector<float> samples_;
    int sample_rate_ = 0;
    bool ready_ = false;
};

}  // namespace maxlabel
