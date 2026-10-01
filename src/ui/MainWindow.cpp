#include "MainWindow.h"

#include <QAction>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <exception>

MainWindow::MainWindow(QWidget * parent) : QMainWindow(parent) {
    setWindowTitle(tr("MaxLabel"));
    resize(1100, 700);

    // --- toolbar ------------------------------------------------------------
    QToolBar * toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(false);

    QAction * openAction = toolbar->addAction(tr("Open Folder…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openDirectory);

    toolbar->addSeparator();

    prevAction_ = toolbar->addAction(tr("Previous"));
    prevAction_->setShortcut(QKeySequence(Qt::Key_Z));
    connect(prevAction_, &QAction::triggered, this, &MainWindow::goPrevious);

    nextAction_ = toolbar->addAction(tr("Next"));
    nextAction_->setShortcut(QKeySequence(Qt::Key_X));
    connect(nextAction_, &QAction::triggered, this, &MainWindow::goNext);

    toolbar->addSeparator();

    saveAction_ = toolbar->addAction(tr("Save"));
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, &MainWindow::saveCurrent);

    QAction * validateAction = toolbar->addAction(tr("Check"));
    connect(validateAction, &QAction::triggered, this, &MainWindow::validateCurrent);

    // --- central widget -----------------------------------------------------
    QSplitter * splitter = new QSplitter(this);

    list_ = new QListWidget(splitter);
    list_->setMinimumWidth(200);
    connect(list_, &QListWidget::currentRowChanged, this, &MainWindow::onRowChanged);

    editor_ = new QPlainTextEdit(splitter);
    editor_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    editor_->setTabChangesFocus(false);
    editor_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    editor_->setPlaceholderText(
        tr("PFML fragment, e.g.\n"
           "<word text=\"重\" language=\"zh\" script=\"zhong\" phonemes=\"zh ong\"/>"));

    splitter->addWidget(list_);
    splitter->addWidget(editor_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    // --- status bar ---------------------------------------------------------
    status_ = new QLabel(this);
    statusBar()->addWidget(status_);

    refreshStatus();
    updateActions();
}

void MainWindow::openDirectory() {
    const QString directory = QFileDialog::getExistingDirectory(this, tr("Open a project folder"));
    if (directory.isEmpty()) return;

    try {
        project_ = maxlabel::scan(directory.toStdString());
    } catch (const std::exception & error) {
        QMessageBox::warning(this, tr("MaxLabel"), QString::fromUtf8(error.what()));
        return;
    }
    current_ = -1;
    refreshList();
    if (!project_.segments.empty()) selectRow(0);
    refreshStatus();
}

void MainWindow::refreshList() {
    loading_ = true;
    list_->clear();
    for (const maxlabel::Segment & segment : project_.segments) {
        QString label = QString::fromStdString(segment.id);
        if (!segment.pfml_valid) label += tr("   [invalid]");
        else if (segment.source == maxlabel::TextSource::None) label += tr("   [empty]");
        if (segment.audio_path.empty()) label += tr("   (no audio)");
        list_->addItem(label);
    }
    loading_ = false;
}

void MainWindow::selectRow(int row) {
    if (row < 0 || row >= static_cast<int>(project_.segments.size())) return;
    // Commit before moving: nothing may be left pending, or navigating would
    // discard the edit.
    if (current_ != row && !commitCurrent(false)) return;

    loading_ = true;
    current_ = row;
    const maxlabel::Segment & segment = project_.segments[static_cast<std::size_t>(row)];
    editor_->setPlainText(QString::fromStdString(segment.pfml));
    list_->setCurrentRow(row);
    loading_ = false;

    refreshStatus();
    updateActions();
}

void MainWindow::onRowChanged(int row) {
    if (loading_) return;
    selectRow(row);
}

bool MainWindow::commitCurrent(bool quiet) {
    if (current_ < 0 || current_ >= static_cast<int>(project_.segments.size())) return true;

    maxlabel::Segment & segment = project_.segments[static_cast<std::size_t>(current_)];
    const std::string edited = editor_->toPlainText().trimmed().toStdString();
    if (edited == segment.pfml) return true;   // nothing to do

    maxlabel::Segment candidate = segment;
    candidate.pfml = edited;
    try {
        maxlabel::save(candidate);
    } catch (const std::exception & error) {
        if (!quiet) {
            QMessageBox::warning(this, tr("PFML did not parse"),
                                 tr("The fragment was not written, because the aligner "
                                    "would skip this sample:\n\n%1")
                                     .arg(QString::fromUtf8(error.what())));
        }
        return false;
    }
    segment = candidate;
    return true;
}

void MainWindow::saveCurrent() {
    if (commitCurrent(false)) refreshStatus();
}

void MainWindow::validateCurrent() {
    const std::string text = editor_->toPlainText().trimmed().toStdString();
    if (text.empty()) {
        QMessageBox::information(this, tr("MaxLabel"), tr("Nothing to check."));
        return;
    }
    try {
        maxlabel::validate(text);
    } catch (const std::exception & error) {
        QMessageBox::warning(this, tr("PFML did not parse"),
                             QString::fromUtf8(error.what()));
        return;
    }
    QMessageBox::information(this, tr("MaxLabel"), tr("The fragment parses."));
}

void MainWindow::goPrevious() {
    if (current_ > 0) selectRow(current_ - 1);
}

void MainWindow::goNext() {
    if (current_ + 1 < static_cast<int>(project_.segments.size())) selectRow(current_ + 1);
}

void MainWindow::refreshStatus() {
    if (project_.directory.empty()) {
        status_->setText(tr("Open a folder to begin."));
        return;
    }
    QString text = tr("%1 segment(s) — %2")
                       .arg(project_.segments.size())
                       .arg(QString::fromStdString(project_.directory));
    if (current_ >= 0) {
        const maxlabel::Segment & segment = project_.segments[static_cast<std::size_t>(current_)];
        text += tr("   |   %1: %2")
                    .arg(QString::fromStdString(segment.id))
                    .arg(QString::fromUtf8(maxlabel::to_string(segment.source)));
    }
    status_->setText(text);
}

void MainWindow::updateActions() {
    const int count = static_cast<int>(project_.segments.size());
    prevAction_->setEnabled(current_ > 0);
    nextAction_->setEnabled(current_ >= 0 && current_ + 1 < count);
    saveAction_->setEnabled(current_ >= 0);
}
