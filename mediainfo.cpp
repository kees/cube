/*
 * cube media player
 * Copyright 2018-2026 Kees Cook <kees@outflux.net>
 * License: GPLv3+
 */
#include "mediainfo.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

QList<QPair<QString, QString>> parseMediaInfo(const QByteArray &json)
{
    QList<QPair<QString, QString>> rows;

    QJsonDocument doc = QJsonDocument::fromJson(json);
    QJsonObject root = doc.object().value("media").toObject();
    QJsonArray track = root["track"].toArray();

    for (int i = 0; i < track.count(); i++) {
        QJsonObject info = track[i].toObject();

        if (info["@type"] == "General" && info["Format"].isString()) {
            QString format = info["Format"].toString();
            size_t size = info["FileSize"].toString().toFloat();
            size_t divider = 1;
            QString si = "B";

            if (size > 1024 * divider) {
                si = "KiB";
                divider *= 1024;
            }
            if (size > 1024 * divider) {
                si = "MiB";
                divider *= 1024;
            }
            if (size > 1024 * divider) {
                si = "GiB";
                divider *= 1024;
            }
            size_t whole = size;
            size_t tenths = 0;
            if (divider > 10) {
                whole = size / (divider / 10);
                tenths = whole % 10;
                whole /= 10;
            }
            format += QString(" (%1").arg(whole);
            if (tenths != 0)
                format += QString(".%1").arg(tenths);
            format += QString("%1)").arg(si);

            rows.append({QStringLiteral("Format "), format});

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

                rows.append({QStringLiteral("Duration "), duration});
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

            rows.append({video + " ", details});
        }
        if (info["@type"] == "Audio") {
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

            rows.append({audio + " ", channels});
        }
        if (info["@type"] == "Text") {
            QString lang;
            if (info["Language"].isString())
                lang = info["Language"].toString();
            else
                lang = "unspecified";

            if (info["Title"].isString())
                lang += QString(" (%1)").arg(info["Title"].toString());

            rows.append({QStringLiteral("Subtitles "), lang});
        }
    }

    return rows;
}
