/*
 * cube media player
 * Copyright 2018-2026 Kees Cook <kees@outflux.net>
 * License: GPLv3+
 */
#include <QApplication>
//#include <QDesktopWidget>
#include <QKeyEvent>
#include <QFont>
#include <QProcess>
#include <QBrush>
#include <QDebug>
#include <QTimer>
#include <QThread>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QCollator>
#include <QStandardPaths>

#include <QFileIconProvider>

#include <unistd.h>

#include "mainwindow.h"
#include "mediainfo.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    QSettings settings("Outflux", "playback-walker");

    toplevel = settings.value("toplevel", "/").toString();
    program_player = settings.value("player", "vidplay").toString();
    program_thumbnailer = settings.value("thumbnailer", "thumbnailer").toString();

    // Resolve the thumbnailer program's absolute path once up front so the
    // per-keystroke thumbnailCacheLookup() doesn't walk $PATH every time.
    // Empty result means "not found" — in that case cache lookups skip the
    // script-mtime check (see thumbnailCacheLookup for rationale).
    thumbnailerPath = QStandardPaths::findExecutable(program_thumbnailer);
    qDebug() << "thumbnailer program:" << program_thumbnailer
             << "resolved to:" << (thumbnailerPath.isEmpty() ? QString("<not found on PATH>") : thumbnailerPath);

    // OMDb API key for ratings lookups on /Movies/ files. Empty means
    // "not configured"; the thumbnailer skips the fetch in that case.
    // Future: letterboxd_apikey, letterboxd_apisecret.
    QString omdb_apikey = settings.value("omdb_apikey", "").toString();

    // Save our settings so they can be discovered later
    settings.setValue("toplevel", toplevel);
    settings.setValue("player", program_player);
    settings.setValue("thumbnailer", program_thumbnailer);
    settings.setValue("omdb_apikey", omdb_apikey);

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

    thumbnailMaxConcurrent = QThread::idealThreadCount();
    if (thumbnailMaxConcurrent < 1)
        thumbnailMaxConcurrent = 1;
    qDebug() << "thumbnailer concurrency: " << thumbnailMaxConcurrent;

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
    thumbnailQueue.clear();
    // Disconnect first so the finished() lambda doesn't run against
    // half-destroyed member containers while we're tearing them down.
    for (QProcess *proc : thumbnailProcs) {
        proc->disconnect(this);
        proc->kill();
        proc->waitForFinished();
        delete proc;
    }
    thumbnailProcs.clear();
    thumbnailsInFlight.clear();

    delete ui->grThumbnail->scene();
    delete fsSelection;
    delete fs;
    delete metadata;
    delete ui;
}

void MainWindow::moveWatcher(const QModelIndex &index)
{
    QString path = fs->filePath(index);

    // Move filesystem watcher into target.
    fs->setRootPath(path);
    //qDebug() << "Flip Watching: " << path;

    // Then move filesystem watcher back up to parent, to force a refresh (inotify doesn't work on NFS).
    QString dir = path;
    dir = path.left(path.lastIndexOf("/"));
    fs->setRootPath(dir);
    //qDebug() << "Flop Watching: " << dir;
}

void MainWindow::FileSystemHighlight(const QItemSelection &selected, const QItemSelection &deselected)
{
    const QModelIndex index = ui->lstFiles->currentIndex();
    QString path = fs->filePath(index);

    currentPath = path;
    //qDebug() << "Highlight changed: " << fs->fileName(index);
    ui->statusBar->showMessage(path);

    ui->grThumbnail->scene()->clear();
    metadata->clear();

    QString heading;

    QStringList halves = path.split(toplevel + "/");
    if (halves.count() < 2)
        heading = path;
    else
        heading = halves[1];

    currentIndex = index;

    if (fs->isDir(index)) {
        // Draw window heading. Clear currentFile so any stale per-file
        // pipeline state doesn't linger into the directory selection; the
        // debounced keyReleaseEvent will resolve it to the first contained
        // media file once the user settles here.
        currentFile.clear();
        ui->lblDirectory->setText(heading);
        return;
    }

    ui->lblDirectory->setText(heading.left(heading.lastIndexOf("/")));
    currentFile = path;
}

QString MainWindow::thumbnailCacheLookup(const QString &mediaPathName) const
{
    // Replicate the thumbnailer script's cache layout:
    //   CACHE=~/.cache/playback/thumbnails
    //   HASH=sha256(realpath($MEDIA))       // echo -n, no trailing newline
    //   THUMB=$CACHE/${HASH:0:2}/$HASH.png
    //   JSON=$THUMB.json
    // If both sidecar files exist and neither is older than the media file,
    // the script would just print the cached paths, so we can skip the
    // subprocess entirely. Invalidation is "delete from ~/.cache/playback".
    const QString canonical = QFileInfo(mediaPathName).canonicalFilePath();
    if (canonical.isEmpty())
        return QString();

    const QString hashHex = QString::fromLatin1(
        QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha256).toHex());
    const QString cacheRoot = QDir::homePath() + "/.cache/playback/thumbnails";
    const QString thumb = QString("%1/%2/%3.png").arg(cacheRoot, hashHex.left(2), hashHex);
    const QString json = thumb + ".json";

    const QFileInfo thumbInfo(thumb);
    const QFileInfo jsonInfo(json);
    if (!thumbInfo.exists() || !jsonInfo.exists())
        return QString();

    const QDateTime mediaMtime = QFileInfo(canonical).lastModified();
    if (mediaMtime > thumbInfo.lastModified() || mediaMtime > jsonInfo.lastModified())
        return QString();

    // Also invalidate if the thumbnailer program itself is newer than the
    // cached sidecars — i.e. the generating code has been updated (e.g. a
    // filter fix) and existing cached output may be wrong. `thumbnailerPath`
    // was resolved once in the constructor via QStandardPaths::findExecutable
    // so this path is hot (just a stat, no PATH walk per call). Empty means
    // the program couldn't be located at startup; in that case skip the
    // check rather than invalidating everything, since a spawn would fail
    // later anyway and we'd rather serve cache than blank the UI.
    if (!thumbnailerPath.isEmpty()) {
        const QDateTime thumbnailerMtime = QFileInfo(thumbnailerPath).lastModified();
        if (thumbnailerMtime > thumbInfo.lastModified() || thumbnailerMtime > jsonInfo.lastModified())
            return QString();
    }

    return thumb;
}

void MainWindow::thumbnailRequest(QString &path)
{
    // Fast path: if the thumbnailer's on-disk cache is already fresh for
    // this file, display it directly without spawning a subprocess. Keeps
    // revisiting files snappy (including across app restarts) and lets the
    // user invalidate by just deleting files under ~/.cache/playback.
    const QString cachedThumb = thumbnailCacheLookup(path);
    if (!cachedThumb.isEmpty()) {
        qDebug() << "thumbnail cache hit for " << path << " -> " << cachedThumb;
        // Drop any stale pending request for this file so the worker pool
        // doesn't regenerate it needlessly later.
        thumbnailQueue.removeAll(path);
        if (currentFile == path)
            thumbnailDisplay(cachedThumb);
        return;
    }

    // Dedup: if this exact file is already being generated, let the user
    // see that (progress/queue counters) instead of silently returning.
    if (thumbnailsInFlight.contains(path)) {
        thumbnailStatusUpdate();
        return;
    }

    // Dedup: drop any earlier queue entry for the same file so the most
    // recent request is the one that determines its LIFO priority.
    thumbnailQueue.removeAll(path);

    // LIFO: newest request goes to the back; thumbnailStartNext() pops from
    // the back so the most temporally recent request runs next.
    thumbnailQueue.append(path);

    qDebug() << "want thumbnail for " << path << " (queue depth " << thumbnailQueue.size() << ")";

    thumbnailStartNext();
    thumbnailStatusUpdate();
}

void MainWindow::thumbnailStartNext()
{
    while (thumbnailProcs.size() < thumbnailMaxConcurrent && !thumbnailQueue.isEmpty()) {
        // LIFO: take the most recently requested entry.
        const QString mediaPathName = thumbnailQueue.takeLast();
        const QStringList args = QStringList() << mediaPathName;

        QProcess *proc = new QProcess(this);
        thumbnailProcs.append(proc);
        thumbnailsInFlight.insert(mediaPathName);

        connect(proc, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                this, [this, proc, mediaPathName](int exitCode, QProcess::ExitStatus status) {
            thumbnailProcs.removeOne(proc);
            thumbnailsInFlight.remove(mediaPathName);
            proc->deleteLater();

            if (status != QProcess::NormalExit || exitCode != 0) {
                qDebug() << "thumbnailer failed for " << mediaPathName
                         << " exit " << exitCode << " status " << status
                         << " stderr: " << proc->readAllStandardError();
            } else {
                //qDebug() << "thumbnailer stderr: " << proc->readAllStandardError();
                QString thumbnail = QString::fromUtf8(proc->readAllStandardOutput()).split('\n').value(0);
                qDebug() << "thumbnailer done with " << mediaPathName << " got " << thumbnail;

                // Sanity-check the stdout: if the script ever leaks a stderr
                // line ahead of its final `echo "$THUMB"`, `thumbnail` won't
                // point at a real file. Reject it here so we don't feed
                // garbage into QImage/QFile downstream.
                if (thumbnail.isEmpty() || !QFileInfo(thumbnail).isFile()) {
                    qDebug() << "thumbnailer returned bogus path for " << mediaPathName
                             << " : " << thumbnail;
                } else {
                    qDebug() << "current:" << currentFile << " path:" << mediaPathName;
                    if (currentFile == mediaPathName)
                        thumbnailDisplay(thumbnail);
                }
            }

            thumbnailStartNext();
            // Refresh the "generating/queued/N running/M queued" status if
            // the selected file is still waiting on the pool. No-op (leaves
            // real metadata in place) if the selected file just finished
            // and was painted by thumbnailDisplay above.
            thumbnailStatusUpdate();
        });

        qDebug() << "launching " << program_thumbnailer << " " << args.join(" ")
                 << " (" << thumbnailProcs.size() << "/" << thumbnailMaxConcurrent << ")";
        proc->start(program_thumbnailer, args);

        // Match the old waitForFinished(100000) behaviour: kill any thumbnailer
        // that runs longer than 100s. The finished() handler will still fire
        // (with CrashExit) and clean up via deleteLater.
        QTimer::singleShot(100000, proc, [proc, mediaPathName]() {
            if (proc->state() != QProcess::NotRunning) {
                qDebug() << "thumbnailer timed out for " << mediaPathName;
                proc->kill();
            }
        });
    }
}

void MainWindow::thumbnailStatusUpdate()
{
    // Only overwrite the metadata table with a status placeholder if the
    // currently-selected file (or first-file-in-directory) is actually
    // waiting on the pool. If its real JSON metadata has already been
    // displayed (or it's a cache hit), leave the table alone.
    const bool running = thumbnailsInFlight.contains(currentFile);
    const bool queued = thumbnailQueue.contains(currentFile);
    if (!running && !queued)
        return;

    metadata->clear();

    QList<QStandardItem *> row;

    row.append(new QStandardItem("Thumbnailer "));
    row.append(new QStandardItem(running ? "generating..." : "queued"));
    metadata->appendRow(row);

    row.clear();
    row.append(new QStandardItem("Running "));
    row.append(new QStandardItem(QString("%1 / %2")
                                 .arg(thumbnailsInFlight.size())
                                 .arg(thumbnailMaxConcurrent)));
    metadata->appendRow(row);

    row.clear();
    row.append(new QStandardItem("Queued "));
    row.append(new QStandardItem(QString::number(thumbnailQueue.size())));
    metadata->appendRow(row);
}

void MainWindow::FileSystemExpanded(const QModelIndex &index)
{
    qDebug() << "Expanded: " << fs->fileName(index);

    this->moveWatcher(index);
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
        qDebug() << "Unhandled keyPress: " << event->key();
        event->ignore();
        return;
    }
    event->accept();
}

void MainWindow::thumbnailRequestCurrent()
{
    // If we've settled on a directory, resolve it to its first contained
    // media file so single-file directories show their thumbnail/metadata
    // without the user drilling in. FileSystemHighlight cleared currentFile
    // when the selection landed on a directory.
    //
    // Sort with a QCollator configured to match QFileSystemModel's own
    // ordering (case-insensitive, numeric/natural, locale-aware) so the
    // file we pick is actually the one the tree view displays first.
    // QDir::Name alone is raw ASCII case-sensitive, which disagrees with
    // the tree view whenever extension case differs.
    if (currentFile.isEmpty() && fs->isDir(currentIndex)) {
        QFileInfoList entries = QDir(currentPath).entryInfoList(
            QDir::Files | QDir::NoDotAndDotDot, QDir::NoSort);
        if (!entries.isEmpty()) {
            QCollator collator;
            collator.setNumericMode(true);
            collator.setCaseSensitivity(Qt::CaseInsensitive);
            std::sort(entries.begin(), entries.end(),
                      [&collator](const QFileInfo &a, const QFileInfo &b) {
                return collator.compare(a.fileName(), b.fileName()) < 0;
            });
            currentFile = entries.first().absoluteFilePath();
        }
    }
    if (!currentFile.isEmpty())
        this->thumbnailRequest(currentFile);
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right:
        /* Only request a directory refresh when we've come to a stop in a single position. */
        if (!event->isAutoRepeat()) {
            this->moveWatcher(currentIndex);
            // Left/Right also moves the selection (to parent dir / first
            // child), so the new selection needs its thumbnail requested
            // just like Up/Down/Page does. Without this, navigating up a
            // level via Left used to leave the thumbnail/metadata blank
            // until the user pressed Up/Down to "nudge" it.
            this->thumbnailRequestCurrent();
        }
        break;
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
        /* Only request a thumbnail when we've come to a stop in a single position. */
        if (!event->isAutoRepeat()) {
            this->thumbnailRequestCurrent();
        }
        break;
    default:
        //qDebug() << "unhandled keyRelease: " << event->key() << " is repeat: " << event->isAutoRepeat();
        event->ignore();
        return;
    }

    event->accept(); /* ??? */
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

void MainWindow::thumbnailDisplay(const QString &thumbnail)
{
    qDebug() << "want to show " << thumbnail;

    // Wipe any status-placeholder rows that thumbnailStatusUpdate() may have
    // written while we were waiting on the pool; the JSON walk below appends
    // the real rows and assumes it starts empty.
    metadata->clear();

    QImage image(thumbnail);

    // Build a new scene because nothing seems to actually work to recenter the image. :(
    QGraphicsScene *old = ui->grThumbnail->scene();
    QGraphicsScene *scene = new QGraphicsScene();
    ui->grThumbnail->setScene(scene);
    delete old;

    scene->addPixmap(QPixmap::fromImage(image));
    ui->grThumbnail->fitInView(image.rect(), Qt::KeepAspectRatio);
    ui->grThumbnail->centerOn(scene->items()[0]);

    /* Ratings sidecar (optional — only exists for /Movies/ files whose
     * name matched "Title (Year)" and had a configured OMDb API key).
     * Ratings rows go above the media-info rows so they're the first
     * thing visible in the metadata table. Parsing lives in mediainfo.cpp
     * (parseRatings) so it can be unit-tested without Qt Widgets.
     *
     * Each rating row gets a small service-logo icon via ratingIcon():
     * looks for a user-provided PNG at ~/.config/Outflux/icons/<key>.png
     * (e.g. rt.png, imdb.png, metacritic.png, letterboxd.png), and if
     * the file isn't there, falls back to a brand-coloured square so the
     * table always has *something* in the icon column. To upgrade from
     * placeholders to real logos, just drop the PNGs there — no rebuild. */
    static auto ratingIcon = [](const QString &key) -> QIcon {
        static QHash<QString, QIcon> cache;
        if (cache.contains(key))
            return cache[key];

        const QString path = QDir::homePath()
            + "/.config/Outflux/icons/" + key + ".png";
        if (QFile::exists(path)) {
            QIcon icon(path);
            cache[key] = icon;
            return icon;
        }

        // Brand-coloured placeholder: a solid square in the service's
        // primary colour, so the rating rows are visually distinct even
        // without real logos installed.
        QPixmap pm(32, 32);
        if      (key == "rt")         pm.fill(QColor("#FA320A"));
        else if (key == "imdb")       pm.fill(QColor("#F5C518"));
        else if (key == "metacritic") pm.fill(QColor("#66CC33"));
        else if (key == "letterboxd") pm.fill(QColor("#FF8000"));
        else                          pm.fill(Qt::gray);
        QIcon icon(pm);
        cache[key] = icon;
        return icon;
    };

    // Map label text → icon key for the services we know about.
    static const QHash<QString, QString> iconKeys = {
        {QStringLiteral("RT "),         QStringLiteral("rt")},
        {QStringLiteral("IMDb "),       QStringLiteral("imdb")},
        {QStringLiteral("Metacritic "), QStringLiteral("metacritic")},
        {QStringLiteral("Letterboxd "), QStringLiteral("letterboxd")},
    };

    QFile ratings_file(thumbnail + ".ratings");
    if (ratings_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QList<QPair<QString, QString>> ratingRows = parseRatings(ratings_file.readAll());
        ratings_file.close();
        for (const auto &entry : ratingRows) {
            QList<QStandardItem *> row;
            QStandardItem *label = new QStandardItem(entry.first);
            auto it = iconKeys.constFind(entry.first);
            if (it != iconKeys.constEnd())
                label->setIcon(ratingIcon(*it));
            row.append(label);
            row.append(new QStandardItem(entry.second));
            metadata->appendRow(row);
        }
    }

    /* Media info JSON — parsing lives in mediainfo.cpp so it can be
     * unit-tested without dragging in Qt Widgets. */
    QFile json_file(thumbnail + ".json");
    json_file.open(QIODevice::ReadOnly | QIODevice::Text);
    const QByteArray json_bytes = json_file.readAll();
    json_file.close();

    const QList<QPair<QString, QString>> rows = parseMediaInfo(json_bytes);
    for (const auto &entry : rows) {
        QList<QStandardItem *> row;
        row.append(new QStandardItem(entry.first));
        row.append(new QStandardItem(entry.second));
        metadata->appendRow(row);
    }
}
