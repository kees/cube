/*
 * cube media player
 * Copyright 2018-2026 Kees Cook <kees@outflux.net>
 * License: GPLv3+
 */
#ifndef MEDIAINFO_H
#define MEDIAINFO_H

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

// Parses a `mediainfo --Output=JSON` dump (the `.json` sidecar the
// thumbnailer script writes next to each PNG) and returns a list of
// (label, value) rows ready for the metadata table. Pure function; all
// I/O is the caller's responsibility. Kept out of mainwindow.cpp so it
// can be unit-tested without dragging in Qt Widgets.
//
// The output preserves the order the UI has historically rendered:
// General/Format+size, Duration, Video, per-track Audio, Subtitles.
QList<QPair<QString, QString>> parseMediaInfo(const QByteArray &json);

#endif // MEDIAINFO_H
