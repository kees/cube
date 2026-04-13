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
    QString thumbnailCacheLookup(const QString &mediaPathName) const;
    void thumbnailRequestCurrent();
    void thumbnailStartNext();
    void thumbnailStatusUpdate();
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
    // Absolute path of `program_thumbnailer` resolved at construction time
    // via QStandardPaths::findExecutable, so `thumbnailCacheLookup` doesn't
    // walk $PATH on every selection change. Empty if the program couldn't
    // be located; in that case the cache's "is the script newer than the
    // thumb?" check is skipped.
    QString thumbnailerPath;
    QString iconDir;

    struct RatingService {
        QString label;
        QString domain;
        QColor fallbackColor;
        QIcon icon;
    };
    QHash<QString, RatingService> ratingServices;
    QStringList ratingsDisplayOrder;
    QIcon ratingIcon(const QString &key);

    QModelIndex currentIndex;
    QString currentFile;
    QString currentPath;
};

#endif // MAINWINDOW_H
