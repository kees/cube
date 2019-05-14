#include <QApplication>
#include <QDesktopWidget>
#include <QKeyEvent>
#include <QFont>
#include <QProcess>
#include <QDebug>

#include <QFileIconProvider>

#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    toplevel = "/MEDIA";
    player = "vidplay";

    ui->setupUi(this);

    // Aim filesystem model at toplevel directory.
    fs = new QFileSystemModel;
    ui->lstFiles->setModel(fs);

    // Prepare selections
    fsSelection = new QItemSelectionModel(fs);
    ui->lstFiles->setSelectionModel(fsSelection);

    // Notification of selection changes
    QObject::connect(fsSelection, SIGNAL(selectionChanged(const QItemSelection&, const QItemSelection&)),
                     this, SLOT(FileSystemHighlight(const QItemSelection&, const QItemSelection&)));
    QObject::connect(ui->lstFiles, SIGNAL(expanded(const QModelIndex&)), this, SLOT(FileSystemExpanded(const QModelIndex&)));

    // Demonstrating look and feel features
    ui->lstFiles->setAnimated(true);
    ui->lstFiles->setIndentation(20);
    ui->lstFiles->setSortingEnabled(true);
    ui->lstFiles->setHeaderHidden(true);
    ui->lstFiles->setColumnHidden(1, true);
    ui->lstFiles->setColumnHidden(2, true);
    ui->lstFiles->setColumnHidden(3, true);

    // Select toplevel directory.
    fs->setRootPath(toplevel);
    ui->lblDirectory->setText(toplevel);

    // Show the top-level directory expanded
    QModelIndex mappedIndex = fs->index( 0, 0 );
    ui->lstFiles->setExpanded( mappedIndex, true );

    //ui->lstFiles->font().setPointSize(20);
    qDebug() << "Font size: " << ui->lstFiles->font().pointSize();

    showMaximized();
    showFullScreen();
    ui->lstFiles->setFocus();
}

MainWindow::~MainWindow()
{
    delete ui;
    delete fsSelection;
    delete fs;
}

void MainWindow::FileSystemHighlight(const QItemSelection &selected, const QItemSelection &deselected)
{
    QModelIndex index = ui->lstFiles->currentIndex();
    QString path = fs->filePath(index);

    qDebug() << "Highlight changed: " << fs->fileName(index);
    if (fs->isDir(index)) {
        ui->lblDirectory->setText(path);
    } else {
        ui->lblDirectory->setText(path.left(path.lastIndexOf("/")));
        populateMetadata(path);
    }
}

void MainWindow::populateMetadata(const QString &path)
{
    qDebug() << "Want metadata for " << path;
}

void MainWindow::FileSystemExpanded(const QModelIndex &index)
{
    qDebug() << "Expanded: " << fs->fileName(index);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    QModelIndex index;
    QVariant data;
    QString text;

    //qDebug() << "key press: " << event->key();
    switch (event->key()) {
    case Qt::Key_Escape:
        QApplication::quit();
        break;
    case Qt::Key_Enter:
    case Qt::Key_Return:
        index = ui->lstFiles->currentIndex();
        qDebug() << "Chosen: " << fs->fileName(index);

        // Expand a chosen directory
        if (fs->isDir(index)) {
            ui->lstFiles->setExpanded(index, true );
        } else {
            QString path = fs->filePath(index);
            QString status;
            QString program;
            QStringList args;

            program = player;
            args << "--" << path;

            status = "Launching: " + program + " " + args.join(" ");
            qDebug() << status;
            ui->statusBar->showMessage(status);

            QProcess::execute(program, args);

            ui->statusBar->clearMessage();
        }

        break;
    default:
        qDebug() << "Unhandled: " << event->key();
        return;
    }
    event->accept();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    const QSize availableSize = this->size(); // QApplication::desktop()->availableGeometry(this).size();
    int width = availableSize.width() / 4;
    float ratio = (float)availableSize.width() / (float)availableSize.height();
    qDebug() << "window size available: " << availableSize.width() << "x" << availableSize.height() << " (" << ratio << ")";
    qDebug() << "window size chosen: " << width << "x" << (int)(width * ratio);
//    ui->grThumbnail->setMaximumSize(width, width / ratio);
//    ui->grThumbnail->setMinimumSize(width, width / ratio);
//    ui->tblMetadata->setMaximumHeight(width / ratio);
    ui->grThumbnail->setMinimumSize(width, width / ratio);
//    ui->grThumbnail->setMaximumSize(width, width / ratio);
    ui->tblMetadata->setMaximumWidth(width);
}
