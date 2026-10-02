#include "WaveformView.h"

#include "AudioClip.h"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace {

const QColor kBackground(0x1B, 0x1F, 0x24);
const QColor kWave(0x6F, 0xC2, 0x7A);
const QColor kSelection(0x2C, 0x5A, 0x8A, 0x90);
const QColor kPlayhead(0xE7, 0x4C, 0x3C);

// In-place radix-2 FFT.  `re` and `im` must be the same power-of-two length.
void fft(std::vector<float> & re, std::vector<float> & im) {
    const std::size_t n = re.size();
    if (n < 2) return;

    // Bit-reversal permutation.
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    // Butterflies.
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * 3.14159265358979323846 / static_cast<double>(len);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u(re[i + k], im[i + k]);
                const std::complex<double> v =
                    std::complex<double>(re[i + k + len / 2], im[i + k + len / 2]) * w;
                re[i + k] = static_cast<float>((u + v).real());
                im[i + k] = static_cast<float>((u + v).imag());
                re[i + k + len / 2] = static_cast<float>((u - v).real());
                im[i + k + len / 2] = static_cast<float>((u - v).imag());
                w *= step;
            }
        }
    }
}

}  // namespace

WaveformView::WaveformView(QWidget * parent) : QWidget(parent) {
    setMinimumHeight(120);
    setMouseTracking(false);
    setCursor(Qt::PointingHandCursor);
    setAutoFillBackground(false);
}

void WaveformView::setClip(const maxlabel::AudioClip * clip) {
    clip_ = clip;
    position_ = 0.0;
    clearSelection();
    invalidateSpectrum();
    update();
}

void WaveformView::setMode(Mode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    invalidateSpectrum();
    update();
}

void WaveformView::setPosition(double seconds) {
    if (std::fabs(seconds - position_) < 1e-4) return;
    position_ = seconds;
    update();
}

void WaveformView::clearSelection() {
    selection_begin_ = 0.0;
    selection_end_ = 0.0;
    emit selectionChanged(0.0, 0.0);
    update();
}

double WaveformView::time_at(int x) const {
    if (clip_ == nullptr || !clip_->ready() || width() <= 0) return 0.0;
    const double fraction =
        std::min(1.0, std::max(0.0, static_cast<double>(x) / static_cast<double>(width())));
    return fraction * clip_->duration();
}

int WaveformView::x_at(double seconds) const {
    if (clip_ == nullptr || !clip_->ready() || clip_->duration() <= 0.0) return 0;
    return static_cast<int>(seconds / clip_->duration() * static_cast<double>(width()));
}

void WaveformView::resizeEvent(QResizeEvent * event) {
    QWidget::resizeEvent(event);
    invalidateSpectrum();
}

void WaveformView::invalidateSpectrum() { spectrum_ = QImage(); }

void WaveformView::mousePressEvent(QMouseEvent * event) {
    if (clip_ == nullptr || !clip_->ready()) return;
    if (event->button() != Qt::LeftButton) return;
    dragging_ = true;
    drag_anchor_ = time_at(static_cast<int>(event->position().x()));
    selection_begin_ = drag_anchor_;
    selection_end_ = drag_anchor_;
    emit seekRequested(drag_anchor_);
    update();
}

void WaveformView::mouseMoveEvent(QMouseEvent * event) {
    if (!dragging_) return;
    const double at = time_at(static_cast<int>(event->position().x()));
    selection_begin_ = std::min(drag_anchor_, at);
    selection_end_ = std::max(drag_anchor_, at);
    emit selectionChanged(selection_begin_, selection_end_);
    update();
}

void WaveformView::mouseReleaseEvent(QMouseEvent * event) {
    if (event->button() != Qt::LeftButton) return;
    // Only a drag that started here ends here: a bare release (no clip loaded,
    // or a press that was ignored) has nothing to finish.
    if (!dragging_) return;
    dragging_ = false;
    if (clip_ == nullptr || !clip_->ready()) {
        clearSelection();
        return;
    }
    // A click with no drag is a seek, not a one-sample selection.
    const double tolerance = clip_->duration() / std::max(1, width());
    if (selection_end_ - selection_begin_ < tolerance) clearSelection();
}

const QImage & WaveformView::spectrogram() {
    const int w = std::max(1, width());
    const int h = std::max(1, height());
    if (!spectrum_.isNull() && spectrum_.width() == w && spectrum_.height() == h) return spectrum_;

    spectrum_ = QImage(w, h, QImage::Format_RGB32);
    spectrum_.fill(kBackground);

    if (clip_ == nullptr || !clip_->ready() || clip_->sampleRate() <= 0) return spectrum_;

    const std::vector<float> & samples = clip_->samples();
    const int rate = clip_->sampleRate();
    constexpr int kWindow = 512;
    const int bins = kWindow / 2;

    std::vector<float> re(kWindow);
    std::vector<float> im(kWindow);
    std::vector<float> window(kWindow);
    for (int i = 0; i < kWindow; ++i) {
        window[i] = 0.5f - 0.5f * std::cos(2.0f * 3.14159265358979323846f * i / (kWindow - 1));
    }

    for (int x = 0; x < w; ++x) {
        const std::size_t centre = static_cast<std::size_t>(
            static_cast<double>(x) / w * static_cast<double>(samples.size()));
        const std::size_t from = centre > kWindow / 2 ? centre - kWindow / 2 : 0;

        for (int i = 0; i < kWindow; ++i) {
            const std::size_t index = from + static_cast<std::size_t>(i);
            const float value = index < samples.size() ? samples[index] : 0.0f;
            re[i] = value * window[i];
            im[i] = 0.0f;
        }
        fft(re, im);

        for (int bin = 0; bin < bins; ++bin) {
            const float magnitude = std::sqrt(re[bin] * re[bin] + im[bin] * im[bin]);
            // Log scale: linear magnitude leaves everything but the loudest
            // harmonic invisible, which is the opposite of useful here.
            const float db = 20.0f * std::log10(magnitude + 1e-6f);
            const float level = std::min(1.0f, std::max(0.0f, (db + 60.0f) / 60.0f));
            const int y = h - 1 - bin * h / bins;
            spectrum_.setPixelColor(x, y,
                                    QColor(static_cast<int>(level * 40),
                                           static_cast<int>(level * 200),
                                           static_cast<int>(120 + level * 120)));
        }
    }
    return spectrum_;
}

void WaveformView::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), kBackground);

    if (clip_ == nullptr || !clip_->ready()) {
        painter.setPen(QColor(0x8A, 0x8F, 0x98));
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("No audio for this segment."));
        return;
    }

    const int w = width();
    const int h = height();

    if (hasSelection()) {
        const int from = x_at(selection_begin_);
        const int to = x_at(selection_end_);
        painter.fillRect(QRect(from, 0, std::max(1, to - from), h), kSelection);
    }

    if (mode_ == Mode::Spectrum) {
        painter.drawImage(0, 0, spectrogram());
    } else {
        const std::vector<float> & samples = clip_->samples();
        const std::size_t total = samples.size();
        painter.setPen(kWave);
        for (int x = 0; x < w; ++x) {
            const std::size_t from = total * static_cast<std::size_t>(x) / static_cast<std::size_t>(w);
            const std::size_t to =
                std::max(from + 1, total * static_cast<std::size_t>(x + 1) / static_cast<std::size_t>(w));
            float low = 0.0f;
            float high = 0.0f;
            for (std::size_t i = from; i < to && i < total; ++i) {
                low = std::min(low, samples[i]);
                high = std::max(high, samples[i]);
            }
            const int mid = h / 2;
            const int y0 = mid - static_cast<int>(high * mid);
            const int y1 = mid - static_cast<int>(low * mid);
            painter.drawLine(x, y0, x, std::max(y0 + 1, y1));
        }
    }

    const int playhead = x_at(position_);
    painter.setPen(kPlayhead);
    painter.drawLine(playhead, 0, playhead, h);
}
