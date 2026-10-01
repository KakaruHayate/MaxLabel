#pragma once

// MaxLabel main window.
//
// Layout follows the plan: the segment list on the left, the PFML editor in
// the middle, the audio panel on the right (M2 — not wired up yet).  The
// editor is text-first because PFML is what the aligner reads; the structured
// views (language spans, word boundaries, phonemes) layer on top of it later.
//
// Data safety: every navigation commits the editor back into the model and
// writes the segment, so there is never a pending edit to lose.  This is the
// one behaviour worth getting right early — Aegisub's timing edits are pending
// and are silently dropped when the selection moves, which is exactly the trap
// to avoid.

#include "maxlabel/core.h"

#include <QMainWindow>
#include <QString>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QAction;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget * parent = nullptr);

private slots:
    void openDirectory();
    void onRowChanged(int row);
    void saveCurrent();
    void goPrevious();
    void goNext();
    void validateCurrent();

private:
    // Editor -> model, then model -> disk.  Returns false (and leaves the model
    // untouched) when the fragment does not parse.
    bool commitCurrent(bool quiet);
    void selectRow(int row);
    void refreshList();
    void refreshStatus();
    void updateActions();

    maxlabel::Project project_;
    int  current_  = -1;
    bool loading_  = false;   // guards the signals fired while repopulating

    QListWidget *   list_       = nullptr;
    QPlainTextEdit * editor_    = nullptr;
    QLabel *        status_     = nullptr;
    QAction *       saveAction_ = nullptr;
    QAction *       prevAction_ = nullptr;
    QAction *       nextAction_ = nullptr;
};
