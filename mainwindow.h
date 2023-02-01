#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFileSystemModel>
#include <QItemSelectionModel>
#include <QStandardItemModel>
#include <QtConcurrent/QtConcurrent>
#include <QGraphicsScene>
#include <QSettings>

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
    void thumbnailReady(int num);
    void thumbnailerIdle();
    void moveWatcher(const QModelIndex &index);

private:
    Ui::MainWindow *ui;
    QFileSystemModel *fs;
    QItemSelectionModel *fsSelection;

    QStandardItemModel *metadata;

    QFutureWatcher<QStringList> *thumbnailer;

    QString toplevel;
    QString program_player;
    QString program_thumbnailer;

    QModelIndex currentIndex;
    QString currentFile;
    QString currentPath;
};

#endif // MAINWINDOW_H
