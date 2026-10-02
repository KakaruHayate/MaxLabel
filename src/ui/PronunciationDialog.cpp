#include "PronunciationDialog.h"

#include "DialogButtons.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <sstream>

namespace {

QString join(const std::vector<std::string> & parts) {
    QString out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) out += QLatin1Char(' ');
        out += QString::fromStdString(parts[i]);
    }
    return out;
}

std::vector<std::string> split(const QString & text) {
    std::vector<std::string> out;
    std::istringstream stream(text.toStdString());
    std::string token;
    while (stream >> token) out.push_back(token);
    return out;
}

}  // namespace

PronunciationDialog::PronunciationDialog(const QString & selected_text,
                                         const QString & language,
                                         bool insertion,
                                         const maxlabel::G2PContext * g2p,
                                         const maxlabel::Vocabulary * vocabulary,
                                         QWidget * parent)
    : QDialog(parent), vocabulary_(vocabulary), language_(language.toStdString()) {
    setWindowTitle(insertion ? tr("Insert Phonemes") : tr("Set Pronunciation"));
    setMinimumWidth(460);

    QVBoxLayout * layout = new QVBoxLayout(this);

    if (!insertion && !selected_text.isEmpty()) {
        QLabel * target = new QLabel(tr("For: %1").arg(selected_text), this);
        target->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(target);
    }

    // The dictionary's own answers, when there is a pipeline to ask.
    std::vector<tifa_ggml::G2PWordCandidates> offered;
    if (!insertion && g2p != nullptr && g2p->ready()) {
        offered = g2p->candidates(selected_text.toStdString(),
                                  language.isEmpty() ? std::vector<std::string>{}
                                                     : std::vector<std::string>{ language_ });
    }
    if (!offered.empty()) {
        candidates_ = new QListWidget(this);
        for (const tifa_ggml::G2PWordCandidates & word : offered) {
            for (const tifa_ggml::G2PCandidate & candidate : word.candidates) {
                QString label = QString::fromStdString(candidate.script);
                if (label.isEmpty()) label = join(candidate.phonemes);
                label += QStringLiteral("    (");
                label += join(candidate.phonemes);
                label += QLatin1Char(')');
                QListWidgetItem * item = new QListWidgetItem(label, candidates_);
                item->setData(Qt::UserRole, QString::fromStdString(candidate.script));
                item->setData(Qt::UserRole + 1, join(candidate.phonemes));
            }
        }
        if (candidates_->count() != 0) {
            layout->addWidget(new QLabel(tr("From the dictionary:"), this));
            layout->addWidget(candidates_);
            connect(candidates_, &QListWidget::itemDoubleClicked, this,
                    &PronunciationDialog::useSelectedCandidate);
            connect(candidates_, &QListWidget::itemActivated, this,
                    &PronunciationDialog::useSelectedCandidate);
        } else {
            delete candidates_;
            candidates_ = nullptr;
        }
    }
    if (candidates_ == nullptr && !insertion) {
        QLabel * hint = new QLabel(
            tr("No dictionary candidates (no G2P configured, or it claims nothing here). "
               "Type the phonemes instead."),
            this);
        hint->setWordWrap(true);
        layout->addWidget(hint);
    }

    QFormLayout * form = new QFormLayout();
    if (!insertion) {
        script_ = new QLineEdit(this);
        script_->setPlaceholderText(tr("pinyin / romaji label, e.g. chong"));
        form->addRow(tr("Script"), script_);
    }
    phonemes_ = new QLineEdit(this);
    phonemes_->setPlaceholderText(insertion ? tr("e.g. n   or   AP") : tr("e.g. ch ong"));
    form->addRow(tr("Phonemes"), phonemes_);
    layout->addLayout(form);

    verdict_ = new QLabel(this);
    verdict_->setWordWrap(true);
    layout->addWidget(verdict_);

    QDialogButtonBox * buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    maxlabel::ui::localise_buttons(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Only for a run that can have a pronunciation: an inserted sound has
    // nothing to take off, and the button would be a lie.
    if (!insertion) {
        QPushButton * remove = buttons->addButton(tr("Remove"),
                                                  QDialogButtonBox::DestructiveRole);
        remove->setToolTip(tr("Take the written pronunciation off this run."));
        connect(remove, &QPushButton::clicked, this, [this]() {
            removed_ = true;
            accept();
        });
    }

    connect(phonemes_, &QLineEdit::textChanged, this, &PronunciationDialog::revalidate);
    revalidate();
}

void PronunciationDialog::setExisting(const QString & script, const QString & phonemes) {
    if (script_ != nullptr) script_->setText(script);
    if (phonemes_ != nullptr) phonemes_->setText(phonemes);
}

void PronunciationDialog::useSelectedCandidate() {
    if (candidates_ == nullptr) return;
    QListWidgetItem * item = candidates_->currentItem();
    if (item == nullptr) return;
    if (script_ != nullptr) script_->setText(item->data(Qt::UserRole).toString());
    phonemes_->setText(item->data(Qt::UserRole + 1).toString());
}

void PronunciationDialog::revalidate() {
    const std::vector<std::string> symbols = phonemes();
    if (symbols.empty()) {
        verdict_->setText(tr("Enter at least one phoneme."));
        return;
    }
    if (vocabulary_ == nullptr || vocabulary_->empty()) {
        verdict_->setText(tr("No vocabulary loaded — the phonemes cannot be checked."));
        return;
    }

    const std::vector<std::string> languages =
        language_.empty() ? std::vector<std::string>{} : std::vector<std::string>{ language_ };
    QStringList unknown;
    for (const std::string & symbol : symbols) {
        if (!maxlabel::check_phoneme(*vocabulary_, symbol, languages).known) {
            unknown << QString::fromStdString(symbol);
        }
    }
    if (unknown.isEmpty()) {
        verdict_->setText(tr("All phonemes resolve against the model vocabulary."));
    } else {
        // Named individually: "one of these is wrong" is not actionable.
        verdict_->setText(tr("Not in the model vocabulary: %1 — the aligner would "
                             "not resolve these.")
                              .arg(unknown.join(QStringLiteral(", "))));
    }
}

QString PronunciationDialog::script() const {
    return script_ == nullptr ? QString() : script_->text().trimmed();
}

std::vector<std::string> PronunciationDialog::phonemes() const {
    return phonemes_ == nullptr ? std::vector<std::string>{} : split(phonemes_->text());
}
