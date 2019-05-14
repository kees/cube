#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFileSystemModel>
#include <QItemSelectionModel>

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

private:
    Ui::MainWindow *ui;
    QFileSystemModel *fs;
    QItemSelectionModel *fsSelection;

    void populateMetadata(const QString &path);
};

#endif // MAINWINDOW_H
