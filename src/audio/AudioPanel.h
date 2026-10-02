#pragma once

// The audio view and its transport, without any buttons.
//
// The controls live in a bar between the audio and the text (Aegisub's
// layout), so this class is the thing they drive rather than the thing that
// draws them.
//
// Text-first pairing — the segment decides which file is loaded, not the other
// way round.  That is the difference from the old MinLabel flow, where the
// audio came first and the label was attached to it: here the transcript is
// the thing being worked on, and the audio is what you check it against.

#include "AudioClip.h"
#include "WaveformView.h"

#include <QByteArray>
#include <QWidget>

class QAudioSink;
class QBuffer;
class QTimer;

class AudioPanel : public QWidget {
    Q_OBJECT

public:
    explicit AudioPanel(QWidget * parent = nullptr);
    ~AudioPanel() override;

    // Loads the audio for a segment, or clears the panel when there is none.
    void setAudioFile(const QString & path);

    void play(double from, double to);   // to <= from means "to the end"
    void playSelectionOrAll();
    void stop();
    void seek(double seconds);
    void nudge(double delta);
    void setSpectrumMode(bool spectrum);
    void toggleMode();

    bool hasAudio() const { return clip_.ready(); }
    bool isPlaying() const { return sink_ != nullptr; }
    double position() const { return position_; }
    double duration() const { return clip_.ready() ? clip_.duration() : 0.0; }

    WaveformView * view() const { return view_; }

signals:
    void positionChanged(double seconds);
    void statusMessage(const QString & message);
    void playingChanged(bool playing);
    // False when the segment has no audio, or the file would not decode: the
    // window collapses the audio area rather than showing an empty one.
    void audioAvailabilityChanged(bool available);

private:
    void rebuildPcm();
    void onTick();

    maxlabel::AudioClip clip_;
    WaveformView *      view_    = nullptr;
    QAudioSink *        sink_    = nullptr;
    QBuffer *           buffer_  = nullptr;
    QTimer *            timer_   = nullptr;

    QByteArray pcm_;             // the clip as 16-bit mono, what the sink gets
    double     position_ = 0.0;
    double     play_start_ = 0.0;   // where the current playback began
    double     play_end_ = 0.0;
};
