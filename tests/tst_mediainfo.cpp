/*
 * cube media player
 * Copyright 2018-2026 Kees Cook <kees@outflux.net>
 * License: GPLv3+
 *
 * Regression tests for parseMediaInfo() — the JSON walk that produces
 * metadata-table rows from `mediainfo --Output=JSON` output. These tests
 * pin the *current* behaviour (integer-arithmetic decimal truncation
 * quirks and all) so a future refactor of mediainfo.cpp can be done
 * with confidence that every code path is still covered.
 */
#include <QtTest>

#include "mediainfo.h"

using Row = QPair<QString, QString>;
using Rows = QList<Row>;

// Build a minimal media-info JSON with a single General track carrying
// the given Format/FileSize (and optional Duration). Used by the size
// and duration data-driven tests.
static QByteArray generalJson(const QByteArray &format,
                              const QByteArray &fileSize,
                              const QByteArray &duration = QByteArray())
{
    QByteArray json = "{\"media\":{\"track\":[{"
                      "\"@type\":\"General\","
                      "\"Format\":\"" + format + "\","
                      "\"FileSize\":\"" + fileSize + "\"";
    if (!duration.isEmpty())
        json += ",\"Duration\":\"" + duration + "\"";
    json += "}]}}";
    return json;
}

class TestMediaInfo : public QObject
{
    Q_OBJECT

private slots:
    // --- General: Format + FileSize formatting ---
    void formatSize_data();
    void formatSize();

    // --- General: Duration formatting ---
    void duration_data();
    void duration();

    // --- General: File_Modified_Date_Local merged into format-name row ---
    void date_example();
    void date_allMonths_data();
    void date_allMonths();
    void date_unpaddedDay();
    void date_missing();
    void date_malformed();
    void date_invalidMonth();
    void date_mergesIntoFormatRow();

    // --- Format-name row: combinatoric coverage of size/date presence ---
    void formatRow_noSizeNoDate();
    void formatRow_dateOnly();

    // --- Video: codec row (fps/bitrate) and Size row (WxH/aspect) ---
    void video_fpsTrim_data();
    void video_fpsTrim();
    void video_stripVisual();
    void video_formatUnchanged();
    void video_fpsPrefersOriginal();
    void video_bitrate_withMode();
    void video_bitrate_withoutMode();
    void video_bitrate_fractional();
    void video_bitrate_missing();
    void video_bitrate_preScanWorksRegardlessOfTrackOrder();
    void video_size_computesFromDimensions();
    void video_size_prefersDisplayAspectRatio();
    void video_size_prefersOriginalDar();
    void video_size_missingDimensions();
    void video_size_resolutionLabel_data();
    void video_size_resolutionLabel();

    // --- Audio: language, channel-position fallback chain ---
    void audio_languageOptional();
    void audio_channels_useChannelPositions();
    void audio_channels_preferOriginalPositions();
    void audio_channels_useChannelLayout();
    void audio_channels_preferOriginalLayout();
    void audio_channels_preferOriginalCount();
    void audio_channels_fallbackToChannels();
    void audio_channels_bareTwoRendersAsStereoGroup();
    void audio_channels_fallbackToStereo();

    // --- Subtitles: language + optional title ---
    void subtitles_languageAndTitle();
    void subtitles_languageOnly();
    void subtitles_unspecifiedLanguage();
    void subtitles_unspecifiedWithTitle();

    // --- Dedup of identical rows across tracks ---
    void dedup_duplicateRowsCollapsed();

    // --- Ratings sidecar (parseRatings) ---
    void ratings_allPresent();
    void ratings_partialFields();
    void ratings_emptyValues();
    void ratings_emptyJson();
    void ratings_malformedJson();
    void ratings_futureLetterboxd();
    void ratings_customOrder();
    void ratings_filterSources();
    void ratings_emptyOrder();
    void ratings_imdbConversion();
    void ratings_letterboxdStar();
    void plot_present();
    void plot_missing();
    void plot_empty();
    void plot_malformed();

    // --- Degenerate input ---
    void empty_json();
    void malformed_json();
    void missing_media_key();
    void missing_track_array();
    void track_without_type();
    void general_without_format_skipped();

    // --- Full realistic fixture: all track types in one JSON ---
    void realistic_fixture();
};

// ----------------------------------------------------------------------
// General / FileSize
// ----------------------------------------------------------------------

void TestMediaInfo::formatSize_data()
{
    QTest::addColumn<QByteArray>("fileSize");
    QTest::addColumn<QString>("expectedSize");

    // Sub-KiB: raw byte count, no decimal (divider=1, divider>10 is false).
    QTest::newRow("500 bytes")        << QByteArray("500")      << QString("500B");
    // One byte below the KiB boundary — last value that's still shown as bytes.
    QTest::newRow("1023 bytes")       << QByteArray("1023")     << QString("1023B");
    // Exact KiB boundary: `>=` means 1024 B is already 1 KiB.
    QTest::newRow("1024 -> 1KiB")     << QByteArray("1024")     << QString("1KiB");
    // Just above the boundary.
    QTest::newRow("1025 -> 1KiB")     << QByteArray("1025")     << QString("1KiB");
    // Whole-KiB value with no decimal emitted (tenths == 0).
    QTest::newRow("2048 -> 2KiB")     << QByteArray("2048")     << QString("2KiB");
    // 2.5 KiB: 2560 / 102 = 25, tenths = 5, whole = 2 => "2.5KiB".
    QTest::newRow("2560 -> 2.5KiB")   << QByteArray("2560")     << QString("2.5KiB");
    // One byte below the MiB boundary — regression test for the decimal
    // arithmetic: should be 1023.9 KiB, not 1028 KiB (which is what the
    // old `size / (divider / 10)` form used to compute).
    QTest::newRow("1048575 -> 1023.9KiB")
        << QByteArray("1048575") << QString("1023.9KiB");
    // Exact MiB boundary — regression test for `>=` semantics.
    QTest::newRow("1048576 -> 1MiB")  << QByteArray("1048576")  << QString("1MiB");
    // Just above the MiB boundary.
    QTest::newRow("1048577 -> 1MiB")  << QByteArray("1048577")  << QString("1MiB");
    // 1.5 MiB.
    QTest::newRow("1572864 -> 1.5MiB") << QByteArray("1572864") << QString("1.5MiB");
    // One byte below the GiB boundary — companion to the 1048575 case.
    // Should format as 1023.9 MiB, not 1024 MiB.
    QTest::newRow("1073741823 -> 1023.9MiB")
        << QByteArray("1073741823") << QString("1023.9MiB");
    // Exact GiB boundary — regression test for `>=` semantics.
    QTest::newRow("1073741824 -> 1GiB")
        << QByteArray("1073741824") << QString("1GiB");
    // Just-above-1-GiB boundary — also a regression test for the `toFloat`
    // parsing bug that used to silently lose precision at values above
    // ~16 MiB and mis-format this as "1024MiB". `toLongLong()` handles it.
    QTest::newRow("1073741825 -> 1GiB")
        << QByteArray("1073741825") << QString("1GiB");
    // 2.5 GiB: exercises the GiB branch plus the tenths-decimal code path.
    QTest::newRow("2684354560 -> 2.5GiB")
        << QByteArray("2684354560") << QString("2.5GiB");
}

void TestMediaInfo::formatSize()
{
    QFETCH(QByteArray, fileSize);
    QFETCH(QString, expectedSize);

    const Rows rows = parseMediaInfo(generalJson("MPEG-4", fileSize));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].first, QStringLiteral("MPEG-4 "));
    QCOMPARE(rows[0].second, expectedSize);
}

// ----------------------------------------------------------------------
// General / Duration
// ----------------------------------------------------------------------

void TestMediaInfo::duration_data()
{
    QTest::addColumn<QByteArray>("durationSeconds");
    QTest::addColumn<QString>("expectedDuration");

    // Seconds only (no minutes, no hours) — un-padded "%ds".
    QTest::newRow("0")     << QByteArray("0.000")   << QString("0s");
    QTest::newRow("5")     << QByteArray("5.000")   << QString("5s");
    QTest::newRow("59")    << QByteArray("59.000")  << QString("59s");
    // Minutes without hours — un-padded "%dm" + zero-padded "%02ds".
    QTest::newRow("60")    << QByteArray("60.000")  << QString("1m00s");
    QTest::newRow("65")    << QByteArray("65.000")  << QString("1m05s");
    QTest::newRow("3599")  << QByteArray("3599.000") << QString("59m59s");
    // Hours — both minutes and seconds become zero-padded.
    QTest::newRow("3600")  << QByteArray("3600.000") << QString("1h00m00s");
    QTest::newRow("3665")  << QByteArray("3665.000") << QString("1h01m05s");
    QTest::newRow("36000") << QByteArray("36000.000") << QString("10h00m00s");
}

void TestMediaInfo::duration()
{
    QFETCH(QByteArray, durationSeconds);
    QFETCH(QString, expectedDuration);

    // Duration row is emitted *first*, above the format-name row.
    const Rows rows = parseMediaInfo(generalJson("MP4", "1024", durationSeconds));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0].first, QStringLiteral("Duration "));
    QCOMPARE(rows[0].second, expectedDuration);
    QCOMPARE(rows[1], Row(QStringLiteral("MP4 "), QStringLiteral("1KiB")));
}

// ----------------------------------------------------------------------
// General / File_Modified_Date_Local — merges into the format-name row
// as "(YYYY Mon D)" following the file size. There is no standalone Date
// row; date info is assembled into the value of the "<format> " row.
// ----------------------------------------------------------------------

// Build a minimal General-only fixture with Format, a 1 KiB FileSize, and
// a File_Modified_Date_Local value. With FileSize="1024" the size portion
// of the row value is always "1KiB", so every expected value below has the
// form "1KiB (<date>)" against a label of "MPEG-4 ".
static QByteArray dateJson(const QByteArray &modifiedLocal)
{
    return QByteArray("{\"media\":{\"track\":[{"
                      "\"@type\":\"General\","
                      "\"Format\":\"MPEG-4\","
                      "\"FileSize\":\"1024\","
                      "\"File_Modified_Date_Local\":\"") + modifiedLocal + "\"}]}}";
}

void TestMediaInfo::date_example()
{
    // Verbatim from the user's request: "2010-12-11 23:46:17" → "2010 Dec 11",
    // rendered inside the format-name row as "1KiB (2010 Dec 11)".
    const Rows rows = parseMediaInfo(dateJson("2010-12-11 23:46:17"));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0], Row(QStringLiteral("MPEG-4 "),
                          QStringLiteral("1KiB (2010 Dec 11)")));
}

void TestMediaInfo::date_allMonths_data()
{
    QTest::addColumn<QByteArray>("modified");
    QTest::addColumn<QString>("expectedValue");

    // Verify the month-name lookup table, one row per month. Each row uses
    // a distinct year/day so a bad index (off-by-one, reversed) would show
    // up as a diff in multiple columns simultaneously.
    QTest::newRow("Jan") << QByteArray("2020-01-15 00:00:00") << QString("1KiB (2020 Jan 15)");
    QTest::newRow("Feb") << QByteArray("2021-02-28 10:20:30") << QString("1KiB (2021 Feb 28)");
    QTest::newRow("Mar") << QByteArray("2022-03-10 01:02:03") << QString("1KiB (2022 Mar 10)");
    QTest::newRow("Apr") << QByteArray("2023-04-01 12:00:00") << QString("1KiB (2023 Apr 1)");
    QTest::newRow("May") << QByteArray("2024-05-05 05:05:05") << QString("1KiB (2024 May 5)");
    QTest::newRow("Jun") << QByteArray("1999-06-30 23:59:59") << QString("1KiB (1999 Jun 30)");
    QTest::newRow("Jul") << QByteArray("1999-07-04 12:34:56") << QString("1KiB (1999 Jul 4)");
    QTest::newRow("Aug") << QByteArray("2000-08-20 08:15:00") << QString("1KiB (2000 Aug 20)");
    QTest::newRow("Sep") << QByteArray("2005-09-09 09:09:09") << QString("1KiB (2005 Sep 9)");
    QTest::newRow("Oct") << QByteArray("2010-10-31 18:00:00") << QString("1KiB (2010 Oct 31)");
    QTest::newRow("Nov") << QByteArray("2015-11-11 11:11:11") << QString("1KiB (2015 Nov 11)");
    QTest::newRow("Dec") << QByteArray("2010-12-11 23:46:17") << QString("1KiB (2010 Dec 11)");
}

void TestMediaInfo::date_allMonths()
{
    QFETCH(QByteArray, modified);
    QFETCH(QString, expectedValue);

    const Rows rows = parseMediaInfo(dateJson(modified));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0], Row(QStringLiteral("MPEG-4 "), expectedValue));
}

void TestMediaInfo::date_unpaddedDay()
{
    // Single-digit days render without a leading zero: "Sep 9", not "Sep 09".
    const Rows rows = parseMediaInfo(dateJson("2005-09-09 09:09:09"));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].second, QStringLiteral("1KiB (2005 Sep 9)"));
}

void TestMediaInfo::date_missing()
{
    // No File_Modified_Date_Local field at all: row value drops the
    // "(...)" suffix and is just the bare size.
    const QByteArray json = R"({"media":{"track":[{
        "@type":"General","Format":"MPEG-4","FileSize":"1024"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0], Row(QStringLiteral("MPEG-4 "), QStringLiteral("1KiB")));
}

void TestMediaInfo::date_malformed()
{
    // Garbage in the date field should be silently ignored — the row
    // value falls back to just the size ("1KiB") rather than crashing or
    // producing half-parsed output.
    const QString noDate = QStringLiteral("1KiB");
    Rows rows;

    rows = parseMediaInfo(dateJson("not a date"));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].second, noDate);

    rows = parseMediaInfo(dateJson("2010/12/11 23:46:17")); // wrong separators
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].second, noDate);

    rows = parseMediaInfo(dateJson("short"));               // too short
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].second, noDate);

    rows = parseMediaInfo(dateJson(""));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].second, noDate);
}

void TestMediaInfo::date_invalidMonth()
{
    // Month out of 1..12 range is rejected; row still appears without date.
    const QString noDate = QStringLiteral("1KiB");
    Rows rows;

    rows = parseMediaInfo(dateJson("2010-00-11 00:00:00"));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].second, noDate);

    rows = parseMediaInfo(dateJson("2010-13-11 00:00:00"));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].second, noDate);
}

void TestMediaInfo::date_mergesIntoFormatRow()
{
    // Date info merges into the format-name row's value as the "(...)"
    // annotation. Duration is its own row and is emitted *first*, above
    // the format-name row. Pins that layout.
    const QByteArray json = R"({"media":{"track":[{
        "@type":"General","Format":"MPEG-4","FileSize":"1024",
        "File_Modified_Date_Local":"2010-12-11 23:46:17",
        "Duration":"60.000"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0], Row(QStringLiteral("Duration "),
                          QStringLiteral("1m00s")));
    QCOMPARE(rows[1], Row(QStringLiteral("MPEG-4 "),
                          QStringLiteral("1KiB (2010 Dec 11)")));
}

void TestMediaInfo::formatRow_noSizeNoDate()
{
    // Neither FileSize nor File_Modified_Date_Local present — the format-
    // name row still appears but with an empty value string. (Previously
    // the size formatter would emit "(0B)" in this case; the gate on
    // `size > 0` avoids that.)
    const QByteArray json = R"({"media":{"track":[{
        "@type":"General","Format":"MPEG-4"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0], Row(QStringLiteral("MPEG-4 "), QString()));
}

void TestMediaInfo::formatRow_dateOnly()
{
    // Date present, FileSize absent — the row value becomes just the bare
    // date string. No parentheses, since there's no size for them to
    // annotate; the date is the only piece of info being shown.
    const QByteArray json = R"({"media":{"track":[{
        "@type":"General","Format":"MPEG-4",
        "File_Modified_Date_Local":"2010-12-11 23:46:17"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0], Row(QStringLiteral("MPEG-4 "),
                          QStringLiteral("2010 Dec 11")));
}

// ----------------------------------------------------------------------
// Video
// ----------------------------------------------------------------------

void TestMediaInfo::video_fpsTrim_data()
{
    QTest::addColumn<QByteArray>("fps");
    QTest::addColumn<QString>("expectedFps");

    // "30.000" → loop chops trailing 0s, then the trailing ".", then stops.
    QTest::newRow("30.000 -> 30")    << QByteArray("30.000") << QString("30fps");
    // "29.970" → one trailing 0 chopped, then stops (no more trailing 0/.).
    QTest::newRow("29.970 -> 29.97") << QByteArray("29.970") << QString("29.97fps");
    // "23.976" → nothing chopped.
    QTest::newRow("23.976 unchanged") << QByteArray("23.976") << QString("23.976fps");
    // Integer-looking string with no dot: loop doesn't enter.
    QTest::newRow("24 unchanged")    << QByteArray("24")     << QString("24fps");
    // Bare trailing dot gets chopped.
    QTest::newRow("30. -> 30")       << QByteArray("30.")    << QString("30fps");
}

void TestMediaInfo::video_fpsTrim()
{
    QFETCH(QByteArray, fps);
    QFETCH(QString, expectedFps);

    // No General track → no overall bitrate → codec row is just the fps.
    // Width/Height present, so a Size row is also emitted.
    const QByteArray json = "{\"media\":{\"track\":[{"
                            "\"@type\":\"Video\","
                            "\"Format\":\"H.264\","
                            "\"Width\":\"1920\",\"Height\":\"1080\","
                            "\"FrameRate\":\"" + fps + "\""
                            "}]}}";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0].first, QStringLiteral("H.264 "));
    QCOMPARE(rows[0].second, expectedFps);
    // 1080 pixels tall → "Full HD" label.
    QCOMPARE(rows[1], Row(QStringLiteral("Full HD "), QStringLiteral("1920x1080 (1.78)")));
}

void TestMediaInfo::video_stripVisual()
{
    // "MPEG-4 Visual" has " Visual" (7 chars) chopped from the end.
    // 640x480 → raw aspect 1.333 → rounds to "1.33" (Academy ratio).
    // 480 pixels tall → "SD" label.
    const QByteArray json = R"({"media":{"track":[{
        "@type":"Video","Format":"MPEG-4 Visual",
        "Width":"640","Height":"480","FrameRate":"24.000"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0], Row(QStringLiteral("MPEG-4 "), QStringLiteral("24fps")));
    QCOMPARE(rows[1], Row(QStringLiteral("SD "), QStringLiteral("640x480 (1.33)")));
}

void TestMediaInfo::video_formatUnchanged()
{
    // Format that doesn't end in " Visual" should pass through untouched
    // (modulo the single trailing-space label suffix).
    // 3840x2160 → 1.778 → "1.78" (16:9). 2160 pixels tall → "4K" label.
    const QByteArray json = R"({"media":{"track":[{
        "@type":"Video","Format":"AV1",
        "Width":"3840","Height":"2160","FrameRate":"60.000"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0], Row(QStringLiteral("AV1 "), QStringLiteral("60fps")));
    QCOMPARE(rows[1], Row(QStringLiteral("4K "), QStringLiteral("3840x2160 (1.78)")));
}

void TestMediaInfo::video_fpsPrefersOriginal()
{
    // Telecined film content: container reports the 29.97 NTSC pulldown
    // rate in FrameRate, but the real source rate lives in FrameRate_Original.
    // Parser must prefer the _Original value so the UI shows "23.976fps"
    // (a 24p movie) instead of "29.97fps" (a broadcast-rate stream). Also
    // add DisplayAspectRatio="1.333" so the Size row shows the true NTSC
    // DVD 4:3 aspect rather than the raw 720/480 = 1.50 coded ratio.
    // 480 pixels tall → "SD" label.
    const QByteArray json = R"({"media":{"track":[{
        "@type":"Video","Format":"MPEG-2 Video",
        "Width":"720","Height":"480",
        "FrameRate":"29.970",
        "FrameRate_Original":"23.976",
        "DisplayAspectRatio":"1.333"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0], Row(QStringLiteral("MPEG-2 Video "), QStringLiteral("23.976fps")));
    QCOMPARE(rows[1], Row(QStringLiteral("SD "), QStringLiteral("720x480 (1.33)")));
}

// ----------------------------------------------------------------------
// Codec-row bitrate: pulls from the General track's OverallBitRate_Mode
// and OverallBitRate via the pre-scan.
// ----------------------------------------------------------------------

void TestMediaInfo::video_bitrate_withMode()
{
    // Both mode and rate present → "<fps> @ VBR 5Mbps". The General track
    // has Format + FileSize, so a Matroska format-name row is also emitted
    // (with the size as its value). We assert the full row set to pin
    // order and row count.
    const QByteArray json = R"({"media":{"track":[
        {"@type":"General","Format":"Matroska","FileSize":"500",
         "OverallBitRate_Mode":"VBR","OverallBitRate":"5000000"},
        {"@type":"Video","Format":"AVC",
         "Width":"1920","Height":"1080","FrameRate":"23.976"}
    ]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows[0], Row(QStringLiteral("Matroska "), QStringLiteral("500B")));
    QCOMPARE(rows[1], Row(QStringLiteral("AVC "),      QStringLiteral("23.976fps @ VBR 5Mbps")));
    QCOMPARE(rows[2], Row(QStringLiteral("Full HD "),  QStringLiteral("1920x1080 (1.78)")));
}

void TestMediaInfo::video_bitrate_withoutMode()
{
    // Rate present, mode missing → "<fps> @ 1.5Mbps" (no "VBR "/"CBR "
    // prefix). A 1.5 Mbps value exercises the one-decimal rendering.
    const QByteArray json = R"({"media":{"track":[
        {"@type":"General","Format":"Matroska","FileSize":"500",
         "OverallBitRate":"1500000"},
        {"@type":"Video","Format":"AVC",
         "Width":"1920","Height":"1080","FrameRate":"30.000"}
    ]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows[1].second, QStringLiteral("30fps @ 1.5Mbps"));
}

void TestMediaInfo::video_bitrate_fractional()
{
    // 12345678 bps → 12.345... Mbps → rounds to "12.3Mbps". Pins the
    // one-decimal rounding, which is the tricky case (not .0 and not <1).
    const QByteArray json = R"({"media":{"track":[
        {"@type":"General","Format":"Matroska","FileSize":"500",
         "OverallBitRate_Mode":"CBR","OverallBitRate":"12345678"},
        {"@type":"Video","Format":"AVC",
         "Width":"1920","Height":"1080","FrameRate":"24.000"}
    ]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows[1].second, QStringLiteral("24fps @ CBR 12.3Mbps"));
}

void TestMediaInfo::video_bitrate_missing()
{
    // No General track at all → overallBitRate stays empty → the codec
    // row is just "<fps>" with no "@" suffix.
    const QByteArray json = R"({"media":{"track":[
        {"@type":"Video","Format":"AVC",
         "Width":"1920","Height":"1080","FrameRate":"30.000"}
    ]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0], Row(QStringLiteral("AVC "),     QStringLiteral("30fps")));
    QCOMPARE(rows[1], Row(QStringLiteral("Full HD "), QStringLiteral("1920x1080 (1.78)")));
}

void TestMediaInfo::video_bitrate_preScanWorksRegardlessOfTrackOrder()
{
    // Put the General track *after* the Video track in the JSON. The pre-
    // scan should still find it, so the Video row still gets the bitrate
    // suffix. Guards against "grab from General on first encounter" regressions.
    const QByteArray json = R"({"media":{"track":[
        {"@type":"Video","Format":"AVC",
         "Width":"1920","Height":"1080","FrameRate":"24.000"},
        {"@type":"General","Format":"Matroska","FileSize":"500",
         "OverallBitRate_Mode":"VBR","OverallBitRate":"5000000"}
    ]}})";
    const Rows rows = parseMediaInfo(json);
    // Codec row still gets bitrate. Format-name row order follows track
    // order, so it comes last here, but that's expected behaviour — rows
    // appear in the order their driving track appears in the JSON.
    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows[0], Row(QStringLiteral("AVC "),      QStringLiteral("24fps @ VBR 5Mbps")));
    QCOMPARE(rows[1], Row(QStringLiteral("Full HD "),  QStringLiteral("1920x1080 (1.78)")));
    QCOMPARE(rows[2], Row(QStringLiteral("Matroska "), QStringLiteral("500B")));
}

// ----------------------------------------------------------------------
// Resolution row: label is the height-tier name (SD/HD/Full HD/2K/4K/8K
// or a generic "Resolution" for anything above 8K). Value is
// "WxH (aspect)" with DAR (preferred) or raw pixels as the aspect source.
// ----------------------------------------------------------------------

void TestMediaInfo::video_size_computesFromDimensions()
{
    // No DAR fields at all — row computes aspect from raw Width/Height.
    // 1920x800 → 2.4 → "2.40" (ultrawide cinema). 800 pixels tall falls
    // into the Full HD tier (720 < 800 <= 1080).
    const QByteArray json = R"({"media":{"track":[{
        "@type":"Video","Format":"AVC",
        "Width":"1920","Height":"800","FrameRate":"24.000"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[1], Row(QStringLiteral("Full HD "), QStringLiteral("1920x800 (2.40)")));
}

void TestMediaInfo::video_size_prefersDisplayAspectRatio()
{
    // Anamorphic case: raw 720/480 = 1.5 but DisplayAspectRatio says 1.778
    // (NTSC 16:9 DVD). Parser must use the DAR, not compute from pixels.
    // 480 pixels tall → "SD" label.
    const QByteArray json = R"({"media":{"track":[{
        "@type":"Video","Format":"MPEG-2 Video",
        "Width":"720","Height":"480","FrameRate":"29.970",
        "DisplayAspectRatio":"1.778"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[1], Row(QStringLiteral("SD "), QStringLiteral("720x480 (1.78)")));
}

void TestMediaInfo::video_size_prefersOriginalDar()
{
    // Both DisplayAspectRatio and DisplayAspectRatio_Original present.
    // The _Original variant wins, mirroring the FrameRate_Original and
    // ChannelPositions_Original preference pattern elsewhere in the parser.
    const QByteArray json = R"({"media":{"track":[{
        "@type":"Video","Format":"MPEG-2 Video",
        "Width":"720","Height":"480","FrameRate":"29.970",
        "DisplayAspectRatio":"1.778",
        "DisplayAspectRatio_Original":"1.333"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[1], Row(QStringLiteral("SD "), QStringLiteral("720x480 (1.33)")));
}

void TestMediaInfo::video_size_missingDimensions()
{
    // No Width/Height → no resolution row at all. The codec row is still
    // emitted.
    const QByteArray json = R"({"media":{"track":[{
        "@type":"Video","Format":"AVC","FrameRate":"24.000"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0], Row(QStringLiteral("AVC "), QStringLiteral("24fps")));
}

void TestMediaInfo::video_size_resolutionLabel_data()
{
    QTest::addColumn<QByteArray>("height");
    QTest::addColumn<QString>("expectedLabel");

    // Sweep both sides of every tier boundary to pin the `<=` edges.
    // Each row names the rule it checks so a failure message points
    // straight at the wrong threshold.
    QTest::newRow("240 -> SD")           << QByteArray("240")   << QString("SD ");
    QTest::newRow("480 -> SD (boundary)") << QByteArray("480")  << QString("SD ");
    QTest::newRow("481 -> HD")           << QByteArray("481")   << QString("HD ");
    QTest::newRow("720 -> HD (boundary)") << QByteArray("720")  << QString("HD ");
    QTest::newRow("721 -> Full HD")      << QByteArray("721")   << QString("Full HD ");
    QTest::newRow("1080 -> Full HD (boundary)")
                                         << QByteArray("1080")  << QString("Full HD ");
    QTest::newRow("1081 -> 2K")          << QByteArray("1081")  << QString("2K ");
    QTest::newRow("1440 -> 2K (boundary)") << QByteArray("1440") << QString("2K ");
    QTest::newRow("1441 -> 4K")          << QByteArray("1441")  << QString("4K ");
    QTest::newRow("2160 -> 4K (boundary)") << QByteArray("2160") << QString("4K ");
    QTest::newRow("2161 -> 8K")          << QByteArray("2161")  << QString("8K ");
    QTest::newRow("4320 -> 8K (boundary)") << QByteArray("4320") << QString("8K ");
    // Above 4320p falls back to a generic "Resolution" label.
    QTest::newRow("4321 -> Resolution")  << QByteArray("4321")  << QString("Resolution ");
    QTest::newRow("10000 -> Resolution") << QByteArray("10000") << QString("Resolution ");
    // Degenerate input: height parses to 0 → same "Resolution" fallback
    // as the above-8K case, rather than miscategorising into any tier.
    QTest::newRow("garbage -> Resolution") << QByteArray("not a number") << QString("Resolution ");
}

void TestMediaInfo::video_size_resolutionLabel()
{
    QFETCH(QByteArray, height);
    QFETCH(QString, expectedLabel);

    const QByteArray json = "{\"media\":{\"track\":[{"
                            "\"@type\":\"Video\","
                            "\"Format\":\"AVC\","
                            "\"Width\":\"1920\","
                            "\"Height\":\"" + height + "\","
                            "\"FrameRate\":\"24.000\""
                            "}]}}";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[1].first, expectedLabel);
}

// ----------------------------------------------------------------------
// Audio
// ----------------------------------------------------------------------

void TestMediaInfo::audio_languageOptional()
{
    // With Language: appended in parentheses before the trailing space.
    const QByteArray withLang = R"({"media":{"track":[{
        "@type":"Audio","Format":"FLAC","Language":"English",
        "ChannelPositions":"Front: L C R"
    }]}})";
    Rows rows = parseMediaInfo(withLang);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].first, QStringLiteral("FLAC (English) "));
    QCOMPARE(rows[0].second, QStringLiteral("Front: L C R"));

    // Without Language: just the format name.
    const QByteArray noLang = R"({"media":{"track":[{
        "@type":"Audio","Format":"AAC",
        "ChannelPositions":"Front: L R"
    }]}})";
    rows = parseMediaInfo(noLang);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].first, QStringLiteral("AAC "));
    QCOMPARE(rows[0].second, QStringLiteral("Front: L R"));
}

void TestMediaInfo::audio_channels_useChannelPositions()
{
    // ChannelPositions wins over Channels when both are present.
    const QByteArray json = R"({"media":{"track":[{
        "@type":"Audio","Format":"DTS",
        "ChannelPositions":"Front: L C R, Side: L R, LFE",
        "Channels":"6"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].second, QStringLiteral("Front: L C R, Side: L R, LFE"));
}

void TestMediaInfo::audio_channels_preferOriginalPositions()
{
    // Regression: AC-3 audio track where mediainfo reports the downmix
    // target ("2") in the base Channels field and the real 5.1 layout only
    // in *_Original siblings. Fixture taken verbatim from a real file.
    // The parser must show ChannelPositions_Original (the friendliest form)
    // instead of falling for the misleading Channels:"2".
    const QByteArray json = R"({"media":{"track":[{
        "@type":"Audio",
        "StreamOrder":"1",
        "ID":"1",
        "Format":"AC-3",
        "Format_Commercial_IfAny":"Dolby Digital",
        "Format_Settings_Endianness":"Big",
        "CodecID":"2000",
        "Duration":"6165.536",
        "BitRate_Mode":"CBR",
        "BitRate":"448000",
        "Channels":"2",
        "Channels_Original":"6",
        "ChannelPositions_Original":"Front: L C R, Side: L R, LFE",
        "ChannelLayout_Original":"L R C LFE Ls Rs",
        "SamplesPerFrame":"1536",
        "SamplingRate":"48000"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].first, QStringLiteral("AC-3 "));
    QCOMPARE(rows[0].second, QStringLiteral("Front: L C R, Side: L R, LFE"));
}

void TestMediaInfo::audio_channels_useChannelLayout()
{
    // No Positions fields at all — fall through to ChannelLayout, which is
    // the terser "L R C LFE Ls Rs" form some containers emit.
    const QByteArray json = R"({"media":{"track":[{
        "@type":"Audio","Format":"Opus",
        "ChannelLayout":"L R C LFE Ls Rs",
        "Channels":"6"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].second, QStringLiteral("L R C LFE Ls Rs"));
}

void TestMediaInfo::audio_channels_preferOriginalLayout()
{
    // No Positions fields, but both ChannelLayout (base) and the _Original
    // sibling are present. The _Original wins within the Layout tier for
    // the same reason as Positions: it describes the real audio content.
    const QByteArray json = R"({"media":{"track":[{
        "@type":"Audio","Format":"AC-3",
        "ChannelLayout":"L R",
        "ChannelLayout_Original":"L R C LFE Ls Rs",
        "Channels":"2",
        "Channels_Original":"6"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].second, QStringLiteral("L R C LFE Ls Rs"));
}

void TestMediaInfo::audio_channels_preferOriginalCount()
{
    // No Positions or Layout info at all — only raw channel counts. In that
    // case Channels_Original trumps the potentially-misleading base Channels,
    // so an AC-3 5.1 stream shows "6" instead of "2" even without layout info.
    const QByteArray json = R"({"media":{"track":[{
        "@type":"Audio","Format":"AC-3",
        "Channels":"2",
        "Channels_Original":"6"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].second, QStringLiteral("6"));
}

void TestMediaInfo::audio_channels_fallbackToChannels()
{
    // No ChannelPositions → fall back to Channels.
    const QByteArray json = R"({"media":{"track":[{
        "@type":"Audio","Format":"AC-3","Channels":"6"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].second, QStringLiteral("6"));
}

void TestMediaInfo::audio_channels_bareTwoRendersAsStereoGroup()
{
    // Regression: when the only info we have is a bare Channels:"2", render
    // it as "Front: L R" so the row reads consistently with multichannel
    // rows that come in as position groups. Also verify the same rewrite
    // when the count comes from Channels_Original instead of the base.
    const QByteArray base = R"({"media":{"track":[{
        "@type":"Audio","Format":"AAC","Channels":"2"
    }]}})";
    Rows rows = parseMediaInfo(base);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].first, QStringLiteral("AAC "));
    QCOMPARE(rows[0].second, QStringLiteral("Front: L R"));

    const QByteArray orig = R"({"media":{"track":[{
        "@type":"Audio","Format":"AC-3","Channels_Original":"2"
    }]}})";
    rows = parseMediaInfo(orig);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].second, QStringLiteral("Front: L R"));
}

void TestMediaInfo::audio_channels_fallbackToStereo()
{
    // Neither ChannelPositions nor Channels → assume stereo, rendered in
    // the same position-group form as other stereo rows and flagged with
    // "(presumed)" so it's distinguishable from a file that actually
    // reported 2 channels via mediainfo.
    const QByteArray json = R"({"media":{"track":[{
        "@type":"Audio","Format":"Opus","Language":"Spanish"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].first, QStringLiteral("Opus (Spanish) "));
    QCOMPARE(rows[0].second, QStringLiteral("Front: L R (presumed)"));
}

// ----------------------------------------------------------------------
// Subtitles (@type == "Text")
// ----------------------------------------------------------------------

void TestMediaInfo::subtitles_languageAndTitle()
{
    const QByteArray json = R"({"media":{"track":[{
        "@type":"Text","Language":"English","Title":"SDH"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].first, QStringLiteral("Subtitles "));
    QCOMPARE(rows[0].second, QStringLiteral("English (SDH)"));
}

void TestMediaInfo::subtitles_languageOnly()
{
    const QByteArray json = R"({"media":{"track":[{
        "@type":"Text","Language":"French"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].second, QStringLiteral("French"));
}

void TestMediaInfo::subtitles_unspecifiedLanguage()
{
    // No Language field at all → literal "unspecified".
    const QByteArray json = R"({"media":{"track":[{"@type":"Text"}]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].second, QStringLiteral("unspecified"));
}

void TestMediaInfo::subtitles_unspecifiedWithTitle()
{
    const QByteArray json = R"({"media":{"track":[{
        "@type":"Text","Title":"Commentary"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].second, QStringLiteral("unspecified (Commentary)"));
}

// ----------------------------------------------------------------------
// Dedup: when the same (label, value) pair would be emitted more than
// once (e.g. two identically-configured audio tracks, or two English
// subtitle tracks that share everything parseMediaInfo inspects), the
// second and subsequent duplicates are dropped. Order of first
// appearance is preserved.
// ----------------------------------------------------------------------

void TestMediaInfo::dedup_duplicateRowsCollapsed()
{
    const QByteArray json = R"({"media":{"track":[
        {"@type":"Video","Format":"AVC","Width":"1920","Height":"1080","FrameRate":"23.976"},
        {"@type":"Audio","Format":"FLAC","Language":"English","ChannelPositions":"Front: L C R, Side: L R, LFE"},
        {"@type":"Audio","Format":"FLAC","Language":"English","ChannelPositions":"Front: L C R, Side: L R, LFE"},
        {"@type":"Audio","Format":"AC-3","Language":"English","ChannelPositions":"Front: L R"},
        {"@type":"Text","Language":"English","Title":"SDH"},
        {"@type":"Text","Language":"English","Title":"SDH"},
        {"@type":"Text","Language":"French"}
    ]}})";
    const Rows rows = parseMediaInfo(json);
    // Expected: 1 AVC + 1 Size + 1 FLAC (duplicate skipped) + 1 AC-3 +
    // 1 merged Subtitles row ("English (SDH), French" — duplicate SDH
    // entry collapsed) = 5 rows.
    QCOMPARE(rows.size(), 5);
    QCOMPARE(rows[0].first, QStringLiteral("AVC "));
    QCOMPARE(rows[1].first, QStringLiteral("Full HD "));
    QCOMPARE(rows[2], Row(QStringLiteral("FLAC (English) "),
                          QStringLiteral("Front: L C R, Side: L R, LFE")));
    QCOMPARE(rows[3], Row(QStringLiteral("AC-3 (English) "),
                          QStringLiteral("Front: L R")));
    QCOMPARE(rows[4], Row(QStringLiteral("Subtitles "),
                          QStringLiteral("English (SDH), French")));
}

// ----------------------------------------------------------------------
// Ratings sidecar: parseRatings(). Rows are ordered RT → IMDb →
// Metacritic → Letterboxd, only present when the value is non-empty.
// ----------------------------------------------------------------------

void TestMediaInfo::ratings_allPresent()
{
    // Default order: rt, imdb, letterboxd, metacritic.
    const QStringList order = {"rt", "imdb", "letterboxd", "metacritic"};
    const QByteArray json = R"({
        "title":"The Matrix","year":"1999",
        "rt":"88%","imdb":"8.7/10","metacritic":"73/100","letterboxd":""
    })";
    const Rows rows = parseRatings(json, order);
    // Letterboxd is empty → skipped, so only 3 rows.
    // IMDb "8.7/10" is converted to "87%" (whole-number percentage).
    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows[0], Row(QStringLiteral("rt"),         QStringLiteral("88%")));
    QCOMPARE(rows[1], Row(QStringLiteral("imdb"),       QStringLiteral("87%")));
    QCOMPARE(rows[2], Row(QStringLiteral("metacritic"), QStringLiteral("73%")));
}

void TestMediaInfo::ratings_partialFields()
{
    const QStringList order = {"rt", "imdb", "letterboxd", "metacritic"};
    // Only RT present — the other rows are simply absent, not empty or "N/A".
    const QByteArray json = R"({
        "title":"Niche Film","year":"2020",
        "rt":"95%","imdb":"","metacritic":"","letterboxd":""
    })";
    const Rows rows = parseRatings(json, order);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0], Row(QStringLiteral("rt"), QStringLiteral("95%")));
}

void TestMediaInfo::ratings_emptyValues()
{
    const QStringList order = {"rt", "imdb", "letterboxd", "metacritic"};
    // All ratings are empty strings — no rows emitted at all. This
    // matches the thumbnailer's behaviour of not writing the .ratings
    // file when all lookups returned nothing, but covers the edge case
    // where the file was written with blanks.
    const QByteArray json = R"({
        "title":"Unknown","year":"2000",
        "rt":"","imdb":"","metacritic":"","letterboxd":""
    })";
    QCOMPARE(parseRatings(json, order), Rows());
}

void TestMediaInfo::ratings_emptyJson()
{
    const QStringList order = {"rt", "imdb", "letterboxd", "metacritic"};
    // Empty/absent input — same no-rows result, no crash.
    QCOMPARE(parseRatings(QByteArray(), order), Rows());
    QCOMPARE(parseRatings(QByteArray(""), order), Rows());
    QCOMPARE(parseRatings(QByteArray("{}"), order), Rows());
}

void TestMediaInfo::ratings_malformedJson()
{
    const QStringList order = {"rt", "imdb", "letterboxd", "metacritic"};
    QCOMPARE(parseRatings(QByteArray("not json"), order), Rows());
    QCOMPARE(parseRatings(QByteArray("{broken"), order), Rows());
}

void TestMediaInfo::ratings_futureLetterboxd()
{
    // Default order puts Letterboxd before Metacritic. When both are
    // populated, they appear in that order. IMDb is converted to a
    // percentage ("87%"), Letterboxd gets a star suffix ("4.2★").
    const QStringList order = {"rt", "imdb", "letterboxd", "metacritic"};
    const QByteArray json = R"({
        "title":"The Matrix","year":"1999",
        "rt":"88%","imdb":"8.7/10","metacritic":"73/100","letterboxd":"4.2/5"
    })";
    const Rows rows = parseRatings(json, order);
    QCOMPARE(rows.size(), 4);
    QCOMPARE(rows[0], Row(QStringLiteral("rt"),         QStringLiteral("88%")));
    QCOMPARE(rows[1], Row(QStringLiteral("imdb"),       QStringLiteral("87%")));
    QCOMPARE(rows[2], Row(QStringLiteral("letterboxd"), QStringLiteral("4.2\u2605")));
    QCOMPARE(rows[3], Row(QStringLiteral("metacritic"), QStringLiteral("73%")));
}

void TestMediaInfo::ratings_customOrder()
{
    // The serviceKeys list controls display order. Metacritic first, then
    // IMDb, then RT — reversed from the default. Letterboxd not in the
    // list at all, so even though it has a value it's excluded.
    // IMDb "7.5/10" → "75%".
    const QStringList order = {"metacritic", "imdb", "rt"};
    const QByteArray json = R"({
        "title":"Test","year":"2020",
        "rt":"90%","imdb":"7.5/10","metacritic":"80/100","letterboxd":"4.0/5"
    })";
    const Rows rows = parseRatings(json, order);
    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows[0], Row(QStringLiteral("metacritic"), QStringLiteral("80%")));
    QCOMPARE(rows[1], Row(QStringLiteral("imdb"),       QStringLiteral("75%")));
    QCOMPARE(rows[2], Row(QStringLiteral("rt"),         QStringLiteral("90%")));
}

void TestMediaInfo::ratings_filterSources()
{
    // Only "rt" in the service list — the other ratings exist in the JSON
    // but are excluded from the output.
    const QStringList order = {"rt"};
    const QByteArray json = R"({
        "title":"Test","year":"2020",
        "rt":"90%","imdb":"7.5/10","metacritic":"80/100","letterboxd":""
    })";
    const Rows rows = parseRatings(json, order);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0], Row(QStringLiteral("rt"), QStringLiteral("90%")));
}

void TestMediaInfo::ratings_emptyOrder()
{
    // Empty service list → no rows regardless of what's in the JSON.
    // This is what happens when the user sets ratings_display= (empty).
    const QByteArray json = R"({
        "title":"Test","year":"2020",
        "rt":"90%","imdb":"7.5/10","metacritic":"80/100"
    })";
    QCOMPARE(parseRatings(json, QStringList()), Rows());
}

void TestMediaInfo::ratings_imdbConversion()
{
    const QStringList order = {"imdb"};

    // Standard: "6.8/10" → "68%"
    auto parse = [&](const char *val) {
        return parseRatings(QByteArray(R"({"imdb":")") + val + "\"}", order);
    };
    Rows rows = parse("6.8/10");
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].second, QStringLiteral("68%"));

    // Perfect score: "10/10" → "100%"
    rows = parse("10/10");
    QCOMPARE(rows[0].second, QStringLiteral("100%"));

    // Low score with rounding: "6.85/10" → "69%" (qRound(68.5) = 69)
    rows = parse("6.85/10");
    QCOMPARE(rows[0].second, QStringLiteral("69%"));

    // No "/10" suffix (unexpected format) — pass through unchanged.
    rows = parse("8.7");
    QCOMPARE(rows[0].second, QStringLiteral("8.7"));
}

void TestMediaInfo::ratings_letterboxdStar()
{
    const QStringList order = {"letterboxd"};

    auto parse = [&](const char *val) {
        return parseRatings(QByteArray(R"({"letterboxd":")") + val + "\"}", order);
    };

    // Standard: "4.2/5" → "4.2★"
    Rows rows = parse("4.2/5");
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].second, QStringLiteral("4.2\u2605"));

    // Whole number: "4/5" → "4★" (no trailing .0)
    rows = parse("4/5");
    QCOMPARE(rows[0].second, QStringLiteral("4\u2605"));

    // Whole after rounding: "4.0/5" → "4★"
    rows = parse("4.0/5");
    QCOMPARE(rows[0].second, QStringLiteral("4\u2605"));

    // High precision rounded down: "3.83/5" → "3.8★"
    rows = parse("3.83/5");
    QCOMPARE(rows[0].second, QStringLiteral("3.8\u2605"));

    // Rounding up: "3.87/5" → "3.9★"
    rows = parse("3.87/5");
    QCOMPARE(rows[0].second, QStringLiteral("3.9\u2605"));

    // Rounds to whole: "3.96/5" → "4★"
    rows = parse("3.96/5");
    QCOMPARE(rows[0].second, QStringLiteral("4\u2605"));

    // No "/5" suffix (unexpected format) — pass through unchanged.
    rows = parse("3.8");
    QCOMPARE(rows[0].second, QStringLiteral("3.8"));
}

// ----------------------------------------------------------------------
// parsePlot: extracts the OMDb short plot string from a .ratings sidecar.
// ----------------------------------------------------------------------

void TestMediaInfo::plot_present()
{
    // A stored plot blurb is returned verbatim.
    const QByteArray json = R"({
        "title":"Example","year":"2020",
        "plot":"A brief factual summary of the film."
    })";
    QCOMPARE(parsePlot(json), QStringLiteral("A brief factual summary of the film."));
}

void TestMediaInfo::plot_missing()
{
    // No "plot" key at all — returns empty string, does not crash.
    const QByteArray json = R"({"title":"Example","year":"2020","rt":"80%"})";
    QCOMPARE(parsePlot(json), QString());
}

void TestMediaInfo::plot_empty()
{
    // Empty plot string (thumbnailer writes this when OMDb returns "N/A"
    // or the plot field wasn't populated) — returns empty, not "N/A".
    const QByteArray json = R"({"title":"Example","year":"2020","plot":""})";
    QCOMPARE(parsePlot(json), QString());
}

void TestMediaInfo::plot_malformed()
{
    // Garbage input — empty string, no crash.
    QCOMPARE(parsePlot(QByteArray()), QString());
    QCOMPARE(parsePlot(QByteArray("")), QString());
    QCOMPARE(parsePlot(QByteArray("{}")), QString());
    QCOMPARE(parsePlot(QByteArray("not json at all")), QString());
    QCOMPARE(parsePlot(QByteArray("{broken")), QString());
}

// ----------------------------------------------------------------------
// Degenerate / error inputs — must not crash, must return empty rows.
// ----------------------------------------------------------------------

void TestMediaInfo::empty_json()
{
    QCOMPARE(parseMediaInfo(QByteArray()), Rows());
    QCOMPARE(parseMediaInfo(QByteArray("")), Rows());
    QCOMPARE(parseMediaInfo(QByteArray("{}")), Rows());
}

void TestMediaInfo::malformed_json()
{
    QCOMPARE(parseMediaInfo(QByteArray("not json at all")), Rows());
    QCOMPARE(parseMediaInfo(QByteArray("{not valid")), Rows());
}

void TestMediaInfo::missing_media_key()
{
    // Top-level object without "media": root conversion yields an empty
    // object, track[] is empty, no rows produced.
    QCOMPARE(parseMediaInfo(QByteArray(R"({"something":"else"})")), Rows());
}

void TestMediaInfo::missing_track_array()
{
    // "media" exists but no "track": empty array, no rows.
    QCOMPARE(parseMediaInfo(QByteArray(R"({"media":{}})")), Rows());
}

void TestMediaInfo::track_without_type()
{
    // Track entry with no @type: all four branches fall through, no rows.
    const QByteArray json = R"({"media":{"track":[{"Format":"MP4","FileSize":"500"}]}})";
    QCOMPARE(parseMediaInfo(json), Rows());
}

void TestMediaInfo::general_without_format_skipped()
{
    // @type=General but Format is not a string: General branch is skipped
    // entirely (so no size row AND no Duration row, even if Duration is
    // present — the Duration is nested inside the Format-guarded block).
    const QByteArray json = R"({"media":{"track":[{
        "@type":"General","FileSize":"2048","Duration":"60.000"
    }]}})";
    QCOMPARE(parseMediaInfo(json), Rows());
}

// ----------------------------------------------------------------------
// Full realistic fixture: one of each track type, matching the order the
// UI displays them. Catches any future regression where a branch drops
// or reorders rows.
// ----------------------------------------------------------------------

void TestMediaInfo::realistic_fixture()
{
    // Values match a plausible `mediainfo --Output=JSON` dump: a 1.5 MiB
    // H.264 clip in an MP4 container, 23.976 fps 1080p at 5 Mbps VBR, with
    // English FLAC audio (5.1 channels), English SDH subtitles, and a
    // File_Modified_Date_Local of 2010 Dec 11. Duration is 1h23m45s (5025 s).
    // Duration is emitted first. The container format row uses the format
    // name itself as its label ("MPEG-4 ") with "<size> (<date>)" as the
    // value. The Video row is split into a codec row (fps + bitrate) and
    // a Size row (WxH + aspect).
    const QByteArray json = R"({"media":{"track":[
        {"@type":"General","Format":"MPEG-4","FileSize":"1572864","File_Modified_Date_Local":"2010-12-11 23:46:17","Duration":"5025.000","OverallBitRate_Mode":"VBR","OverallBitRate":"5000000"},
        {"@type":"Video","Format":"AVC","Width":"1920","Height":"1080","FrameRate":"23.976"},
        {"@type":"Audio","Format":"FLAC","Language":"English","ChannelPositions":"Front: L C R, Side: L R, LFE"},
        {"@type":"Text","Language":"English","Title":"SDH"}
    ]}})";

    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 6);

    QCOMPARE(rows[0], Row(QStringLiteral("Duration "), QStringLiteral("1h23m45s")));
    QCOMPARE(rows[1], Row(QStringLiteral("MPEG-4 "),   QStringLiteral("1.5MiB (2010 Dec 11)")));
    QCOMPARE(rows[2], Row(QStringLiteral("AVC "),      QStringLiteral("23.976fps @ VBR 5Mbps")));
    QCOMPARE(rows[3], Row(QStringLiteral("Full HD "),  QStringLiteral("1920x1080 (1.78)")));
    QCOMPARE(rows[4], Row(QStringLiteral("FLAC (English) "), QStringLiteral("Front: L C R, Side: L R, LFE")));
    QCOMPARE(rows[5], Row(QStringLiteral("Subtitles "), QStringLiteral("English (SDH)")));
}

QTEST_APPLESS_MAIN(TestMediaInfo)
#include "tst_mediainfo.moc"
