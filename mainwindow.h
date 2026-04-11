#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFileSystemModel>
#include <QItemSelectionModel>
#include <QStandardItemModel>
#include <QGraphicsScene>
#include <QSettings>
#include <QSet>

QT_FORWARD_DECLARE_CLASS(QProcess)

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    void keyPressEvent(QKeyEvent *event);
    void keyReleaseEvent(QKeyEvent *event);
    void resizeEvent(QResizeEvent *event);

private slots:
    void FileSystemHighlight(const QItemSelection &selected, const QItemSelection &deselected);
    void FileSystemExpanded(const QModelIndex &index);

    void thumbnailRequest(QString &path);
    void moveWatcher(const QModelIndex &index);

private:
    void thumbnailStartNext();
    void thumbnailDisplay(const QString &thumbnail);

    Ui::MainWindow *ui;
    QFileSystemModel *fs;
    QItemSelectionModel *fsSelection;

    QStandardItemModel *metadata;

    // LIFO queue of pending thumbnail paths (back = most recent). Up to
    // thumbnailMaxConcurrent subprocesses run at once (sized to the CPU
    // count); as each finishes, the most recently requested entry is popped
    // and started. Dedup on path means at most one pending/in-flight request
    // per unique file.
    QStringList thumbnailQueue;
    QList<QProcess *> thumbnailProcs;
    QSet<QString> thumbnailsInFlight;
    int thumbnailMaxConcurrent;

    QString toplevel;
    QString program_player;
    QString program_thumbnailer;

    QModelIndex currentIndex;
    QString currentFile;
    QString currentPath;
};

#endif // MAINWINDOW_H
