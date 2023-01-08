#include <QApplication>
//#include <QDesktopWidget>
#include <QKeyEvent>
#include <QFont>
#include <QProcess>
#include <QBrush>
#include <QDebug>

#include <QFileIconProvider>

#include <QJsonDocument>

#include <unistd.h>

#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    QSettings settings("Outflux", "playback-walker");

    toplevel = settings.value("toplevel", "/").toString();
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

    // Model for file metadata.
    metadata = new QStandardItemModel(0, 2);
    ui->tblMetadata->setModel(metadata);
    ui->tblMetadata->horizontalHeader()->setVisible(false);
    ui->tblMetadata->verticalHeader()->setVisible(false);
    ui->tblMetadata->setWordWrap(true);
    ui->tblMetadata->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tblMetadata->horizontalHeader()->setStretchLastSection(true);
    ui->tblMetadata->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

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

    // Show the top-level directory expanded
    QModelIndex mappedIndex = fs->index( 0, 0 );
    ui->lstFiles->setExpanded( mappedIndex, true );

    //ui->lstFiles->font().setPointSize(20);
    //qDebug() << "Font size: " << ui->lstFiles->font().pointSize();

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

    delete ui->grThumbnail->scene();
    delete fsSelection;
    delete fs;
    delete metadata;
    delete ui;
}

void MainWindow::FileSystemHighlight(const QItemSelection &selected, const QItemSelection &deselected)
{
    QModelIndex index = ui->lstFiles->currentIndex();
    QString path = fs->filePath(index);

    currentPath = path;
    //qDebug() << "Highlight changed: " << fs->fileName(index);
    ui->statusBar->showMessage(path);

    ui->grThumbnail->scene()->clear();
    metadata->clear();

    QString dir = path;
    QString heading;

    QStringList halves = path.split(toplevel + "/");
    if (halves.count() < 2)
        heading = path;
    else
        heading = halves[1];

    if (fs->isDir(index)) {
        ui->lblDirectory->setText(heading);
        return;
    }

    dir = path.left(path.lastIndexOf("/"));
    ui->lblDirectory->setText(heading.left(heading.lastIndexOf("/")));

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
                qDebug() << "thumbnailer stderr: " << thumbnailerProcess.readAllStandardError();
            }
            tuple.append("");
            return tuple;
        }

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
    qDebug() << "Expanded: " << fs->fileName(index);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    QModelIndex index;
    QVariant data;
    QString text;
    QString path;
    QStringList args;

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
            path = fs->filePath(index);

            args << "--" << path;

            QString status = "Launching: " + program_player + " " + args.join(" ");
            qDebug() << status;
            ui->statusBar->showMessage(status);

            QProcess::execute(program_player, args);

            ui->statusBar->clearMessage();
        }

        break;

    // Remove an item from the recent list
    case Qt::Key_VolumeMute:
        index = ui->lstFiles->currentIndex();
        path = fs->filePath(index);

        qDebug() << "Muting: " << path;

        if (!path.contains("/Recent/"))
            break;

        args << "--" << path;
        QProcess::execute("rm", args);
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
    qDebug() << "thumbnail size chosen: " << width << "x" << (int)(width * ratio);
    ui->grThumbnail->setMinimumSize(width, width / ratio);
    ui->tblMetadata->setMaximumWidth(width);

    // Figure out metadata font size
    int size = ui->lstFiles->font().pointSize();
    qDebug() << "filename font size: " << size;
    if (availableSize.height() / size < 30)
        size /= 2;
    ui->tblMetadata->setFont(QFont(ui->tblMetadata->font().family(), size));
    qDebug() << "metadata font size: " << size;

    // scale padding by font size. But this doesn't work: it seem to break eliding!
    //int padding = size / 4;
    //ui->tblMetadata->setStyleSheet(QString("QTableView::item { border: 0px; padding: %1px; }").arg(padding));
    // make sure eliding is disabled (doesn't seem to work with padding above??)
    ui->tblMetadata->setTextElideMode(Qt::ElideNone);
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

        ui->grThumbnail->scene()->addPixmap(QPixmap::fromImage(image));
        ui->grThumbnail->fitInView(image.rect(), Qt::KeepAspectRatio);
        ui->grThumbnail->centerOn(ui->grThumbnail->scene()->items()[0]);

        /* Media info JSON */
        QString json_path = thumbnail + ".json";
        QFile json_file;
        QByteArray json_bytes;

        json_file.setFileName(json_path);
        json_file.open(QIODevice::ReadOnly | QIODevice::Text);
        json_bytes = json_file.readAll();
        json_file.close();

        QJsonDocument doc = QJsonDocument::fromJson(json_bytes);
        QJsonObject json = doc.object().value("media").toObject();

        //qDebug() << "mediainfo for " << path << " ready: " << json;

        QList<QStandardItem *> row;
        //row.append(new QStandardItem("Filename"));
        //row.append(new QStandardItem(path));
        //metadata->appendRow(row);

        QJsonArray track = json["track"].toArray();
        for (int i=0; i < track.count(); i++) {
            QJsonObject info = track[i].toObject();
            if (info["@type"] == "General" && info["Format"].isString()) {
                row.clear();
                row.append(new QStandardItem("Format "));
                row.append(new QStandardItem(info["Format"].toString()));
                metadata->appendRow(row);

                if (info["Duration"].isString()) {
                    int seconds = info["Duration"].toString().toFloat();
                    int hours = seconds / 3600;
                    seconds %= 3600;
                    int minutes = seconds / 60;
                    seconds %= 60;

                    QString duration = "";

                    if (hours > 0)
                        duration += QString::asprintf("%dh", hours);
                    if (minutes > 0 || hours > 0)
                        duration += QString::asprintf(hours > 0 ? "%02dm" : "%dm", minutes);
                    duration += QString::asprintf(hours > 0 || minutes > 0 ? "%02ds" : "%ds", seconds);

                    qDebug() << "Duration: " + duration;

                    row.clear();
                    row.append(new QStandardItem("Duration "));
                    row.append(new QStandardItem(duration));
                    metadata->appendRow(row);
                }
            }
            if (info["@type"] == "Video") {
                QString video = info["Format"].toString();
                // Strip out "Visual" from "MPEG-4 Visual"
                if (video.endsWith(" Visual"))
                    video.chop(7);

                QString fps = info["FrameRate"].toString();
                // Remove trailing zeros
                while ((fps.contains(".") && fps.endsWith("0")) || fps.endsWith("."))
                    fps.chop(1);

                QString details = info["Width"].toString() + "x" + info["Height"].toString() + " @ " + fps + "fps";
                qDebug() << details;

                row.clear();
                row.append(new QStandardItem(video + " "));
                row.append(new QStandardItem(details));
                metadata->appendRow(row);
            }
            if (info["@type"] == "Audio") {
                qDebug() << info["Format"].toString() + ": " + info["ChannelPositions"].toString();

                QString audio = info["Format"].toString();
                if (info["Language"].isString())
                    audio += QString(" (%1)").arg(info["Language"].toString());

                QString channels;
                if (info["ChannelPositions"].isString())
                    channels = info["ChannelPositions"].toString();
                else if (info["Channels"].isString())
                    channels = info["Channels"].toString();
                else
                    channels = "2"; // assume missing channel count is in stereo

                row.clear();
                row.append(new QStandardItem(audio + " "));
                row.append(new QStandardItem(channels));
                metadata->appendRow(row);
            }
            if (info["@type"] == "Text") {
                qDebug() << "Subs lang: " + info["Language"].toString();

                QString lang;
                if (info["Language"].isString())
                    lang = info["Language"].toString();
                else
                    lang = "unspecified";

                if (info["Title"].isString())
                    lang += QString(" (%1)").arg(info["Title"].toString());

                row.clear();
                row.append(new QStandardItem("Subtitles "));
                row.append(new QStandardItem(lang));
                metadata->appendRow(row);
            }
        }
    }

}

void MainWindow::thumbnailerIdle()
{
    qDebug() << "Thumbnailer idle";
}
