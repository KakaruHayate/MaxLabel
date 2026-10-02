// Dialog buttons — see DialogButtons.h.

#include "DialogButtons.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>

namespace maxlabel::ui {

namespace {

// Spelled out through QT_TRANSLATE_NOOP so lupdate finds them: it extracts
// tr() by the enclosing class and QT_TRANSLATE_NOOP by its explicit context,
// and a bare translate() call is invisible to it.  The context matches Qt's
// own, which is where a translator would look for them anyway.
const char * const kOk     = QT_TRANSLATE_NOOP("QDialogButtonBox", "OK");
const char * const kCancel = QT_TRANSLATE_NOOP("QDialogButtonBox", "Cancel");
const char * const kClose  = QT_TRANSLATE_NOOP("QDialogButtonBox", "Close");
const char * const kYes    = QT_TRANSLATE_NOOP("QDialogButtonBox", "Yes");
const char * const kNo     = QT_TRANSLATE_NOOP("QDialogButtonBox", "No");
const char * const kSave   = QT_TRANSLATE_NOOP("QDialogButtonBox", "Save");

}  // namespace

void localise_buttons(QDialogButtonBox * box) {
    if (box == nullptr) return;
    const auto set = [box](QDialogButtonBox::StandardButton which, const char * text) {
        if (QPushButton * button = box->button(which)) {
            button->setText(QCoreApplication::translate("QDialogButtonBox", text));
        }
    };
    set(QDialogButtonBox::Ok,     kOk);
    set(QDialogButtonBox::Cancel, kCancel);
    set(QDialogButtonBox::Close,  kClose);
    set(QDialogButtonBox::Yes,    kYes);
    set(QDialogButtonBox::No,     kNo);
    set(QDialogButtonBox::Save,   kSave);
}

void warn(QWidget * parent, const QString & title, const QString & text) {
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::NoButton, parent);
    QPushButton * ok = box.addButton(QMessageBox::Ok);
    ok->setText(QCoreApplication::translate("QDialogButtonBox", kOk));
    box.exec();
}

void about(QWidget * parent, const QString & title, const QString & text) {
    QMessageBox box(QMessageBox::Information, title, text, QMessageBox::NoButton, parent);
    QPushButton * ok = box.addButton(QMessageBox::Ok);
    ok->setText(QCoreApplication::translate("QDialogButtonBox", kOk));
    box.exec();
}

}  // namespace maxlabel::ui
