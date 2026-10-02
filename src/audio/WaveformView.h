#pragma once

// The audio view: waveform or spectrogram, with a playhead and a selection.
//
// The interaction model is Aegisub's, because it is the one people who do this
// work already have in their fingers: the whole file is always on screen, a
// click seeks, a drag selects, and the selection is what "play selection"
// plays.  Waveform and spectrum are a toggle rather than two stacked views —
// they answer the same question ("where is the sound?") in two ways, and
// stacking them halves the resolution of both.

#include <QImage>
#include <QWidget>

namespace maxlabel {
class AudioClip;
}

class WaveformView : public QWidget {
    Q_OBJECT

public:
    enum class Mode { Waveform, Spectrum };

    explicit WaveformView(QWidget * parent = nullptr);

    void setClip(const maxlabel::AudioClip * clip);
    void setMode(Mode mode);
    Mode mode() const { return mode_; }

    void setPosition(double seconds);
    double position() const { return position_; }

    double selectionBegin() const { return selection_begin_; }
    double selectionEnd() const { return selection_end_; }
    bool hasSelection() const { return selection_end_ > selection_begin_; }
    void clearSelection();

signals:
    void seekRequested(double seconds);
    void selectionChanged(double begin, double end);

protected:
    void paintEvent(QPaintEvent * event) override;
    void resizeEvent(QResizeEvent * event) override;
    void mousePressEvent(QMouseEvent * event) override;
    void mouseMoveEvent(QMouseEvent * event) override;
    void mouseReleaseEvent(QMouseEvent * event) override;

private:
    double time_at(int x) const;
    int    x_at(double seconds) const;
    void   invalidateSpectrum();
    const QImage & spectrogram();

    const maxlabel::AudioClip * clip_ = nullptr;
    Mode   mode_ = Mode::Waveform;
    double position_ = 0.0;
    double selection_begin_ = 0.0;
    double selection_end_ = 0.0;
    bool   dragging_ = false;
    double drag_anchor_ = 0.0;
    QImage spectrum_;
};
