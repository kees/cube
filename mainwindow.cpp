/*
 * cube media player
 * Copyright 2018-2020 Kees Cook <kees@outflux.net>
 * License: GPLv3+
 */
#include <QApplication>
#include <QDesktopWidget>
#include <QKeyEvent>
#include <QFont>
#include <QProcess>
#include <QBrush>
#include <QDebug>

#include <QFileIconProvider>

#include <unistd.h>

#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    QSettings settings("Outflux", "playback-walker");

    toplevel = settings.value("toplevel", "/media").toString();
    program_player = settings.value("player", "vidplay").toString();
    program_thumbnailer = settings.value("thumbnailer", "thumbnailer").toString();
    // Save our settings so they can be discovered later
    settings.setValue("toplevel", toplevel);
    settings.setValue("player", program_player);
    settings.setValue("thumbnailer", program_thumbnailer);

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
    ui->lstFiles->sortByColumn(0, Qt::AscendingOrder);
    ui->lstFiles->setHeaderHidden(true);
    ui->lstFiles->setColumnHidden(1, true);
    ui->lstFiles->setColumnHidden(2, true);
    ui->lstFiles->setColumnHidden(3, true);

    // Select toplevel directory.
    fs->setRootPath(toplevel);
    ui->lstFiles->setRootIndex(fs->index(toplevel));
    ui->lblDirectory->setText(toplevel);

    // Regularly poke the filesystem model because we might be watching a network mount without inotify support
    fsTimer = new QTimer();
    connect(fsTimer, &QTimer::timeout, this, &MainWindow::FileSystemTimeout);
    fsTimer->setInterval(2000);

    // Show the top-level directory expanded
    QModelIndex mappedIndex = fs->index( 0, 0 );
    ui->lstFiles->setExpanded( mappedIndex, true );

    //ui->lstFiles->font().setPointSize(20);
    qDebug() << "Font size: " << ui->lstFiles->font().pointSize();

    thumbnailer = new QFutureWatcher<QStringList>(this);
    connect(thumbnailer, &QFutureWatcher<QStringList>::resultReadyAt, this, &MainWindow::thumbnailReady);
    connect(thumbnailer, &QFutureWatcher<QStringList>::finished, this, &MainWindow::thumbnailerIdle);

    // Prepare thumbnail area
    ui->grThumbnail->setScene(new QGraphicsScene());
    ui->grThumbnail->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->grThumbnail->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->grThumbnail->setBackgroundBrush(QBrush(Qt::black, Qt::SolidPattern));

    showMaximized();
    showFullScreen();
    ui->lstFiles->setFocus();
}

MainWindow::~MainWindow()
{
    thumbnailer->cancel();
    thumbnailer->waitForFinished();
    delete thumbnailer;

    fsTimer->stop();
    delete fsTimer;

    delete ui->grThumbnail->scene();
    delete fsSelection;
    delete fs;
    delete ui;
}

void MainWindow::FileSystemHighlight(const QItemSelection &selected, const QItemSelection &deselected)
{
    QModelIndex index = ui->lstFiles->currentIndex();
    QString path = fs->filePath(index);

    currentPath = path;
    qDebug() << "Highlight changed: " << fs->fileName(index);
    ui->statusBar->showMessage(path);

    ui->grThumbnail->scene()->clear();

    QString dir = path;
    QString heading;

    if (fs->isDir(index)) {
        heading = dir.split(toplevel+"/")[1];
        ui->lblDirectory->setText(heading);
        return;
    }

    dir = path.left(path.lastIndexOf("/"));
    heading = dir.split(toplevel+"/")[1];

    ui->lblDirectory->setText(heading);

    // Request thumbnail
    QString program = program_thumbnailer;
    std::function<QStringList(const QString&)> thumbnail = [program](const QString &imageFileName) {
        QProcess thumbnailerProcess;
        QStringList tuple;
        QStringList params;
        bool okay;
        int exitcode;
        //static int count = 0;

        tuple.append(imageFileName);

        //qDebug() << "getting thumbnail for " << imageFileName;
        params.append(imageFileName);
        //qDebug() << "launching " << count++ << program << " " << params.join(" ");
        thumbnailerProcess.start(program, params);
        okay = thumbnailerProcess.waitForFinished(100000);
        exitcode = thumbnailerProcess.exitCode();
        if (!okay || exitcode) {
            if (exitcode == 0) {
                qDebug() << "thumbnailer timed out";
            } else {
                qDebug() << "thumbnailer failed: " << exitcode;
            }
            tuple.append("");
            return tuple;
        }

        //qDebug() << "thumbnailer stderr: " << thumbnailerProcess.readAllStandardError();
        QString thumbnail(thumbnailerProcess.readAllStandardOutput());
        thumbnail = thumbnail.split("\n")[0];
        //qDebug() << "thumbnailer done with " << imageFileName << " got " << thumbnail;

        tuple.append(thumbnail);
        return tuple;
    };

    QStringList files;
    files.append(path);

    // Use mapped to run the thread safe scale function on the files.
    qDebug() << "want thumbnail for " << path;
    thumbnailer->setFuture(QtConcurrent::mapped(files, thumbnail));
}

void MainWindow::FileSystemExpanded(const QModelIndex &index)
{
    current = fs->filePath(index);
    qDebug() << "Expanded: " << current;
    // https://doc.qt.io/qt-5/qfilesystemmodel.html#setRootPath
    // This is a weird and badly named API: only 1 directory at a time can be watched...
    fs->setRootPath(current);
    // Restart timer for updates.
    fsTimer->start();
}

void MainWindow::FileSystemTimeout()
{
    // https://bugreports.qt.io/browse/QTBUG-2276
    fs->setRootPath("");
    fs->setRootPath(current);
    //qDebug() << "tick: " << current;
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
    case Qt::Key_Right:
    case Qt::Key_Play:
        index = ui->lstFiles->currentIndex();
        qDebug() << "Chosen: " << fs->fileName(index);

        // Expand a chosen directory
        if (fs->isDir(index)) {
            ui->lstFiles->setExpanded(index, true );
        } else {
            QString path = fs->filePath(index);
            QString status;
            QStringList args;

            args << "--" << path;

            status = "Launching: " + program_player + " " + args.join(" ");
            qDebug() << status;
            ui->statusBar->showMessage(status);

            // Stop polling for filesystem updates while running player.
            fsTimer->stop();
            QProcess::execute(program_player, args);
            fsTimer->start();

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

void MainWindow::thumbnailReady(int num)
{
    QStringList tuple;
    QString path, thumbnail;

    tuple = thumbnailer->resultAt(num);
    path = tuple[0];
    thumbnail = tuple[1];

    if (thumbnail == "")
        return;

    qDebug() << "Thumbnail for " << path << " ready: " << thumbnail;

    qDebug() << "current:" << currentPath << " path:" << path;
    if (currentPath == path) {
        qDebug() << "want to show " << thumbnail;
        /*
        QGraphicsPixmapItem image(QPixmap((thumbnail)));
        scene.clear();
        scene.addItem(image);
        */

        //ui->grThumbnail->setBackgroundBrush(QImage(thumbnail));

        QImage image(thumbnail);
        ui->grThumbnail->scene()->clear();

        // Build a new scene because nothing seems to actually work to recenter the image. :(
        QGraphicsScene *old = ui->grThumbnail->scene();
        QGraphicsScene *scene = new QGraphicsScene();
        ui->grThumbnail->setScene(scene);
        delete old;

        ui->grThumbnail->scene()->addPixmap(QPixmap::fromImage(image, 0));
        ui->grThumbnail->fitInView(image.rect(), Qt::KeepAspectRatio);
        ui->grThumbnail->centerOn(ui->grThumbnail->scene()->items()[0]);
    }

}

void MainWindow::thumbnailerIdle()
{
    qDebug() << "Thumbnailer idle";
}
