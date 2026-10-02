#include "AudioPanel.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

QString format_time(double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    const int total = static_cast<int>(seconds * 1000.0);
    return QStringLiteral("%1:%2.%3")
        .arg(total / 60000)
        .arg((total / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg((total / 100) % 10);
}

}  // namespace

AudioPanel::AudioPanel(QWidget * parent) : QWidget(parent) {
    QVBoxLayout * layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    view_ = new WaveformView(this);
    layout->addWidget(view_, 1);

    timer_ = new QTimer(this);
    timer_->setInterval(30);
    connect(timer_, &QTimer::timeout, this, &AudioPanel::onTick);

    connect(&clip_, &maxlabel::AudioClip::loaded, this, [this]() {
        rebuildPcm();
        view_->setClip(&clip_);
        view_->update();
        emit audioAvailabilityChanged(true);
        emit statusMessage(tr("%1  (%2 s)")
                               .arg(clip_.path().section(QLatin1Char('/'), -1))
                               .arg(clip_.duration(), 0, 'f', 2));
    });
    connect(&clip_, &maxlabel::AudioClip::failed, this, [this](const QString & message) {
        // Nothing decoded: collapse rather than show an empty pane.
        emit audioAvailabilityChanged(false);
        emit statusMessage(message);
    });
    connect(view_, &WaveformView::seekRequested, this, &AudioPanel::seek);

    // Aegisub's transport keys, scoped to this panel.  Space has to insert a
    // space while the editor has focus, so the audio keys only apply once the
    // panel itself is focused — clicking it is the mode switch.
    setFocusPolicy(Qt::StrongFocus);
    const auto bind = [this](QKeySequence key, void (AudioPanel::*slot)()) {
        auto * shortcut = new QShortcut(key, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, slot);
    };
    bind(QKeySequence(Qt::Key_Space), &AudioPanel::playSelectionOrAll);
    bind(QKeySequence(Qt::Key_B), &AudioPanel::playSelectionOrAll);
    bind(QKeySequence(Qt::Key_H), &AudioPanel::stop);
    for (const std::pair<QKeySequence, double> & pair :
         std::vector<std::pair<QKeySequence, double>>{
             { QKeySequence(Qt::Key_Q), -0.5 },
             { QKeySequence(Qt::Key_W), 0.5 },
             { QKeySequence(Qt::Key_Left), -0.5 },
             { QKeySequence(Qt::Key_Right), 0.5 } }) {
        auto * shortcut = new QShortcut(pair.first, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        const double delta = pair.second;
        connect(shortcut, &QShortcut::activated, this, [this, delta]() { nudge(delta); });
    }
}

AudioPanel::~AudioPanel() { stop(); }

void AudioPanel::setAudioFile(const QString & path) {
    stop();
    position_ = 0.0;
    view_->setClip(nullptr);
    if (path.isEmpty()) {
        clip_.clear();
        pcm_.clear();
        view_->update();
        emit audioAvailabilityChanged(false);
        return;
    }
    clip_.load(path);
}

void AudioPanel::rebuildPcm() {
    const std::vector<float> & samples = clip_.samples();
    pcm_.resize(static_cast<int>(samples.size() * sizeof(std::int16_t)));
    auto * out = reinterpret_cast<std::int16_t *>(pcm_.data());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const float clamped = std::min(1.0f, std::max(-1.0f, samples[i]));
        out[i] = static_cast<std::int16_t>(clamped * 32767.0f);
    }
}

void AudioPanel::setSpectrumMode(bool spectrum) {
    view_->setMode(spectrum ? WaveformView::Mode::Spectrum : WaveformView::Mode::Waveform);
}

void AudioPanel::toggleMode() {
    setSpectrumMode(view_->mode() == WaveformView::Mode::Waveform);
}

void AudioPanel::playSelectionOrAll() {
    if (!clip_.ready()) {
        emit statusMessage(tr("No audio for this segment."));
        return;
    }
    if (view_->hasSelection()) {
        play(view_->selectionBegin(), view_->selectionEnd());
    } else {
        play(position_, 0.0);
    }
}

void AudioPanel::play(double from, double to) {
    if (!clip_.ready() || pcm_.isEmpty()) return;
    stop();

    const double duration = clip_.duration();
    const double begin = std::min(std::max(0.0, from), duration);
    play_end_ = (to <= from || to > duration) ? duration : to;

    QAudioFormat format;
    format.setSampleRate(clip_.sampleRate());
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    sink_ = new QAudioSink(format, this);
    buffer_ = new QBuffer(this);
    buffer_->setData(pcm_);
    buffer_->open(QIODevice::ReadOnly);
    buffer_->seek(static_cast<qint64>(begin * clip_.sampleRate()) * 2);

    position_ = begin;
    play_start_ = begin;
    sink_->start(buffer_);
    timer_->start();
    emit playingChanged(true);
}

void AudioPanel::stop() {
    const bool was_playing = sink_ != nullptr;
    timer_->stop();
    if (sink_ != nullptr) {
        sink_->stop();
        sink_->deleteLater();
        sink_ = nullptr;
    }
    if (buffer_ != nullptr) {
        buffer_->close();
        buffer_->deleteLater();
        buffer_ = nullptr;
    }
    if (was_playing) emit playingChanged(false);
}

void AudioPanel::seek(double seconds) {
    position_ = std::min(std::max(0.0, seconds), clip_.ready() ? clip_.duration() : 0.0);
    view_->setPosition(position_);
    emit positionChanged(position_);
    if (sink_ != nullptr) {
        // Restart from the new point rather than trying to move a live stream.
        play(position_, play_end_);
    }
}

void AudioPanel::nudge(double delta) { seek(position_ + delta); }

void AudioPanel::onTick() {
    if (sink_ == nullptr) return;
    // Measured from where this playback started, not from the last tick:
    // accumulating per tick would drift against the sink's own clock.
    const double played = static_cast<double>(sink_->processedUSecs()) / 1000000.0;
    const double at = std::min(play_end_, play_start_ + played);
    position_ = at;
    view_->setPosition(at);
    emit positionChanged(at);

    if (sink_->state() == QAudio::IdleState || at >= play_end_) stop();
}
