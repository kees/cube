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

// Parses a `.ratings` sidecar (JSON with keys "rt", "imdb", "metacritic",
// "letterboxd") and returns (label, value) rows for display above the
// media-info rows. Empty/missing fields are silently skipped. Returns an
// empty list when the input is absent or unparseable, so callers can
// unconditionally prepend the result without a "has ratings" check.
// `serviceKeys` controls which rating services to include and in what
// order. Each entry is a JSON field name in the .ratings sidecar (e.g.
// "rt", "imdb", "metacritic", "letterboxd"). Populated from the
// `ratings_display` config setting so the user can reorder or disable
// individual sources at runtime.
QList<QPair<QString, QString>> parseRatings(const QByteArray &json,
                                            const QStringList &serviceKeys);

#endif // MEDIAINFO_H
