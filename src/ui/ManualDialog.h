#pragma once

// The manual: what the tool is for, how the interaction works, and every key
// that does something.
//
// It is a dialog rather than a README because the question it answers ("how do
// I take this mark back?") is asked while looking at the window, and a window
// that can only be explained by a file elsewhere is one where the answer is
// always somewhere else.

#include <QDialog>

class ManualDialog : public QDialog {
    Q_OBJECT

public:
    explicit ManualDialog(QWidget * parent = nullptr);
};
