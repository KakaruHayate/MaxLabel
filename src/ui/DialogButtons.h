#pragma once

// Standard dialog buttons, in the application's language.
//
// QDialogButtonBox asks the platform theme for its labels, and on Windows that
// theme answers in English no matter which translators are installed — so
// loading Qt's own catalogue is not enough and every dialog in a Chinese
// window still ends in "OK / Cancel / Close".  Setting the labels from this
// application's catalogue is the only way to get them translated on every
// platform.

class QDialogButtonBox;
class QString;
class QWidget;

namespace maxlabel::ui {

void localise_buttons(QDialogButtonBox * box);

// A message box whose button is in the application's language.  QMessageBox
// builds its own button box from the platform theme, so the same problem as
// localise_buttons applies and there is no handle on it — hence the helpers
// rather than the static QMessageBox::warning/about.
void warn(QWidget * parent, const QString & title, const QString & text);
void about(QWidget * parent, const QString & title, const QString & text);

}  // namespace maxlabel::ui
