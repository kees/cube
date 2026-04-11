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

    // Pre-scan the General track for overall-file bitrate info. This lives
    // alongside FileSize on General, not on the Video track, but we want to
    // display it *in* the Video row so the format/fps/rate info all sits
    // together. Using a separate pass rather than assuming General comes
    // first in the JSON (it usually does, but relying on order is fragile).
    QString overallBitRateMode;
    QString overallBitRate;
    for (int i = 0; i < track.count(); i++) {
        QJsonObject info = track[i].toObject();
        if (info["@type"] == "General") {
            if (info["OverallBitRate_Mode"].isString())
                overallBitRateMode = info["OverallBitRate_Mode"].toString();
            if (info["OverallBitRate"].isString())
                overallBitRate = info["OverallBitRate"].toString();
            break;
        }
    }

    for (int i = 0; i < track.count(); i++) {
        QJsonObject info = track[i].toObject();

        if (info["@type"] == "General" && info["Format"].isString()) {
            const QString format = info["Format"].toString();

            // --- Duration row ---
            // Emitted first so the duration appears above the format-name
            // row in the UI. (Conceptually odd — duration before you know
            // what the file is — but it's the requested layout.)
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

            // --- File size ("1.5MiB"), optional ---
            // Computed from FileSize. Omitted entirely when FileSize is
            // absent or zero (rather than rendered as the nonsensical "0B"
            // the old code would produce). Parsed via toLongLong, not
            // toFloat, because single-precision float only has 24 bits of
            // mantissa and silently rounds any FileSize above ~16 MiB —
            // which used to mis-format anything just over 1 GiB as
            // "1024MiB".
            QString sizeStr;
            if (info["FileSize"].isString()) {
                const size_t size = info["FileSize"].toString().toLongLong();
                if (size > 0) {
                    size_t divider = 1;
                    QString si = "B";

                    // `>=` (not `>`) at each boundary so exactly 1024 B is
                    // 1 KiB, exactly 1 MiB is 1 MiB, etc. The `>` form used
                    // to render 1024 B as "1024B" and 1 MiB as "1024KiB".
                    if (size >= 1024 * divider) {
                        si = "KiB";
                        divider *= 1024;
                    }
                    if (size >= 1024 * divider) {
                        si = "MiB";
                        divider *= 1024;
                    }
                    if (size >= 1024 * divider) {
                        si = "GiB";
                        divider *= 1024;
                    }
                    size_t whole = size;
                    size_t tenths = 0;
                    if (divider > 10) {
                        // Multiply before dividing so we don't lose
                        // precision from `divider / 10` truncating (e.g.
                        // 1024/10 = 102 instead of 102.4, which skews
                        // near-boundary sizes by ~0.4%). Safe from
                        // overflow: even a 1 PB file (2^50 B) times 10
                        // fits comfortably in size_t on 64-bit platforms.
                        whole = size * 10 / divider;
                        tenths = whole % 10;
                        whole /= 10;
                    }
                    sizeStr = QString::number(whole);
                    if (tenths != 0)
                        sizeStr += QString(".%1").arg(tenths);
                    sizeStr += si;
                }
            }

            // --- File modification date ("2010 Dec 11"), optional ---
            // Source is mediainfo's File_Modified_Date_Local field, a
            // fixed "yyyy-MM-dd HH:mm:ss" string; only the date portion is
            // used. Month name is looked up in a C-locale table rather
            // than via Qt's locale-dependent "MMM" format, so a non-
            // English host still renders "Dec" (and the tests stay
            // deterministic).
            QString dateStr;
            if (info["File_Modified_Date_Local"].isString()) {
                const QString raw = info["File_Modified_Date_Local"].toString();
                if (raw.length() >= 10 && raw[4] == '-' && raw[7] == '-') {
                    bool yearOk = false, monthOk = false, dayOk = false;
                    const int year  = raw.left(4).toInt(&yearOk);
                    const int month = raw.mid(5, 2).toInt(&monthOk);
                    const int day   = raw.mid(8, 2).toInt(&dayOk);
                    if (yearOk && monthOk && dayOk
                            && year >= 1
                            && month >= 1 && month <= 12
                            && day >= 1 && day <= 31) {
                        static const char *const monthNames[12] = {
                            "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
                        };
                        dateStr = QString("%1 %2 %3")
                                      .arg(year)
                                      .arg(QLatin1String(monthNames[month - 1]))
                                      .arg(day);
                    }
                }
            }

            // --- Assemble the format-name row ---
            // Label is the format name itself (like "MPEG-4 ", "Matroska ",
            // "AVI "), mirroring how the Video codec row uses the video
            // codec name as its label. Value is "<size> (<date>)" when both
            // are available. With only one part, the parens are dropped
            // (no point wrapping a lone annotation). With neither, the
            // value is empty and the row still appears so the format name
            // itself is visible.
            QString value;
            if (!sizeStr.isEmpty() && !dateStr.isEmpty())
                value = sizeStr + " (" + dateStr + ")";
            else if (!sizeStr.isEmpty())
                value = sizeStr;
            else if (!dateStr.isEmpty())
                value = dateStr;
            rows.append({format + " ", value});
        }
        if (info["@type"] == "Video") {
            QString video = info["Format"].toString();
            // Strip out "Visual" from "MPEG-4 Visual"
            if (video.endsWith(" Visual"))
                video.chop(7);

            // Prefer the original (pre-pulldown) frame rate when mediainfo
            // provides it: for 3:2-telecined NTSC content, FrameRate reports
            // the 29.97 playback rate but FrameRate_Original reports the
            // 23.976 source rate, which is the more informative value for a
            // media-browsing UI ("this is a 24p film, not a broadcast").
            QString fps = info["FrameRate_Original"].isString()
                          ? info["FrameRate_Original"].toString()
                          : info["FrameRate"].toString();
            // Remove trailing zeros
            while ((fps.contains(".") && fps.endsWith("0")) || fps.endsWith("."))
                fps.chop(1);

            // Codec row: "23.976fps [@ [VBR ]5Mbps]". Bitrate is appended
            // only if OverallBitRate was collected from General during the
            // pre-scan and is parseable and positive. Mode is optional: if
            // mediainfo doesn't tell us VBR/CBR, we just show the rate.
            QString details = fps + "fps";
            if (!overallBitRate.isEmpty()) {
                bool ok = false;
                const double bps = overallBitRate.toDouble(&ok);
                if (ok && bps > 0) {
                    const double mbps = bps / 1000000.0;
                    QString rateStr = QString::number(mbps, 'f', 1);
                    if (rateStr.endsWith(".0"))
                        rateStr.chop(2);
                    rateStr += "Mbps";
                    if (!overallBitRateMode.isEmpty())
                        details += QString(" @ %1 %2").arg(overallBitRateMode, rateStr);
                    else
                        details += " @ " + rateStr;
                }
            }
            rows.append({video + " ", details});

            // Size row: "WxH (aspect)". Prefer mediainfo's own
            // DisplayAspectRatio (which already accounts for anamorphic
            // SAR — e.g. reports 1.778 for a 720x480 NTSC 16:9 DVD even
            // though raw w/h would give 1.5), with the usual _Original-
            // first preference for telecined content. If neither DAR field
            // is present, fall back to raw width/height arithmetic. Format
            // the aspect to exactly 2 decimal places so the standard
            // cinema values (1.33, 1.78, 1.85, 2.39, ...) round cleanly.
            const QString width = info["Width"].toString();
            const QString height = info["Height"].toString();
            if (!width.isEmpty() && !height.isEmpty()) {
                double dar = 0;
                if (info["DisplayAspectRatio_Original"].isString())
                    dar = info["DisplayAspectRatio_Original"].toString().toDouble();
                else if (info["DisplayAspectRatio"].isString())
                    dar = info["DisplayAspectRatio"].toString().toDouble();
                else {
                    bool wOk = false, hOk = false;
                    const double w = width.toDouble(&wOk);
                    const double h = height.toDouble(&hOk);
                    if (wOk && hOk && h > 0)
                        dar = w / h;
                }
                QString sizeValue = width + "x" + height;
                if (dar > 0)
                    sizeValue += QString(" (%1)").arg(QString::number(dar, 'f', 2));
                rows.append({QStringLiteral("Size "), sizeValue});
            }
        }
        if (info["@type"] == "Audio") {
            QString audio = info["Format"].toString();
            if (info["Language"].isString())
                audio += QString(" (%1)").arg(info["Language"].toString());

            // Fallback chain for the channel description:
            //   1. ChannelPositions_Original  — canonical position groups,
            //      describing the actual audio content before any downmix.
            //      mediainfo fills this in for AC-3/E-AC-3/DTS tracks where
            //      the base Channels field reports the bit-stream's downmix
            //      target ("2") rather than the real 5.1 layout.
            //   2. ChannelPositions           — same format, for tracks
            //      where there's no downmix distinction.
            //   3. ChannelLayout_Original     — terser "L R C LFE Ls Rs"
            //      form; some containers emit this instead of Positions.
            //   4. ChannelLayout              — same, base variant.
            //   5. Channels_Original          — raw count, real.
            //   6. Channels                   — raw count, possibly wrong
            //      (e.g. "2" for an AC-3 5.1 stream).
            //   7. "2 (presumed)"             — no channel info at all.
            QString channels;
            if (info["ChannelPositions_Original"].isString())
                channels = info["ChannelPositions_Original"].toString();
            else if (info["ChannelPositions"].isString())
                channels = info["ChannelPositions"].toString();
            else if (info["ChannelLayout_Original"].isString())
                channels = info["ChannelLayout_Original"].toString();
            else if (info["ChannelLayout"].isString())
                channels = info["ChannelLayout"].toString();
            else if (info["Channels_Original"].isString())
                channels = info["Channels_Original"].toString();
            else if (info["Channels"].isString())
                channels = info["Channels"].toString();
            else
                channels = "Front: L R (presumed)"; // no channel info at all

            // When the only info we have is a bare count of "2", render it
            // in the same "Front: L R" position-group form that multichannel
            // rows use, so stereo files read consistently in the metadata
            // table. Other counts stay as-is (there's no universal position
            // notation for a bare "6" etc. without the real layout names,
            // and rewriting would hide the fact that mediainfo didn't give
            // us layout info).
            if (channels == "2")
                channels = "Front: L R";

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
