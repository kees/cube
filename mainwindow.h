#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFileSystemModel>
#include <QItemSelectionModel>
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
    void resizeEvent(QResizeEvent *event);

private slots:
    void FileSystemHighlight(const QItemSelection &selected, const QItemSelection &deselected);
    void FileSystemExpanded(const QModelIndex &index);
    void FileSystemTimeout();

    void thumbnailReady(int num);
    void thumbnailerIdle();

private:
    Ui::MainWindow *ui;
    QFileSystemModel *fs;
    QItemSelectionModel *fsSelection;
    QTimer *fsTimer;

    QFutureWatcher<QStringList> *thumbnailer;

    QString toplevel;
    QString current;

    QString program_player;
    QString program_thumbnailer;

    QString currentPath;
};

#endif // MAINWINDOW_H
