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

    // --- Video: fps trailing-zero strip + " Visual" chop + WxH details ---
    void video_fpsTrim_data();
    void video_fpsTrim();
    void video_stripVisual();
    void video_formatUnchanged();

    // --- Audio: language, channel-position fallback chain ---
    void audio_languageOptional();
    void audio_channels_useChannelPositions();
    void audio_channels_fallbackToChannels();
    void audio_channels_fallbackToStereo();

    // --- Subtitles: language + optional title ---
    void subtitles_languageAndTitle();
    void subtitles_languageOnly();
    void subtitles_unspecifiedLanguage();
    void subtitles_unspecifiedWithTitle();

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
    QTest::addColumn<QString>("expectedFormat");

    // Sub-KiB: raw byte count, no decimal (divider=1, divider>10 is false).
    QTest::newRow("500 bytes")      << QByteArray("500")      << QString("MPEG-4 (500B)");
    // Edge: 1024 is NOT greater than 1024, so still bytes.
    QTest::newRow("1024 bytes")     << QByteArray("1024")     << QString("MPEG-4 (1024B)");
    // Just above 1024 promotes to KiB.
    QTest::newRow("1025 -> 1KiB")   << QByteArray("1025")     << QString("MPEG-4 (1KiB)");
    // Whole-KiB value with no decimal emitted (tenths == 0).
    QTest::newRow("2048 -> 2KiB")   << QByteArray("2048")     << QString("MPEG-4 (2KiB)");
    // 2.5 KiB: 2560 / 102 = 25, tenths = 5, whole = 2 => "2.5KiB".
    QTest::newRow("2560 -> 2.5KiB") << QByteArray("2560")     << QString("MPEG-4 (2.5KiB)");
    // MiB boundary.
    QTest::newRow("1048577 -> 1MiB")  << QByteArray("1048577") << QString("MPEG-4 (1MiB)");
    // 1.5 MiB.
    QTest::newRow("1572864 -> 1.5MiB") << QByteArray("1572864") << QString("MPEG-4 (1.5MiB)");
    // KNOWN QUIRK — pinning current behaviour: 1073741825 is *not* exactly
    // representable in IEEE-754 single precision, and FileSize is parsed via
    // `toFloat()`. The value rounds down to 1073741824 on the way in, so the
    // `size > 1073741824` test in the GiB branch is false and formatting
    // stays in MiB, producing "1024MiB" instead of "1GiB". Switching the
    // parse to `toLongLong()` would fix this; this test pins the current
    // wrong-but-stable output so any intentional fix is visible as a diff.
    QTest::newRow("1073741825 bytes (float-lossy -> 1024MiB)")
        << QByteArray("1073741825") << QString("MPEG-4 (1024MiB)");
    // 2.5 GiB expressed as 2684354560 bytes. This value IS exactly
    // representable as a float (two bits set in the significand), so the
    // GiB branch actually fires and the decimal computation produces "2.5".
    // Exercises the GiB code path cleanly.
    QTest::newRow("2684354560 -> 2.5GiB")
        << QByteArray("2684354560") << QString("MPEG-4 (2.5GiB)");
}

void TestMediaInfo::formatSize()
{
    QFETCH(QByteArray, fileSize);
    QFETCH(QString, expectedFormat);

    const Rows rows = parseMediaInfo(generalJson("MPEG-4", fileSize));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].first, QStringLiteral("Format "));
    QCOMPARE(rows[0].second, expectedFormat);
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

    const Rows rows = parseMediaInfo(generalJson("MP4", "1024", durationSeconds));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[1].first, QStringLiteral("Duration "));
    QCOMPARE(rows[1].second, expectedDuration);
}

// ----------------------------------------------------------------------
// Video
// ----------------------------------------------------------------------

void TestMediaInfo::video_fpsTrim_data()
{
    QTest::addColumn<QByteArray>("fps");
    QTest::addColumn<QString>("expectedDetails");

    // "30.000" → loop chops trailing 0s, then the trailing ".", then stops.
    QTest::newRow("30.000 -> 30")    << QByteArray("30.000") << QString("1920x1080 @ 30fps");
    // "29.970" → one trailing 0 chopped, then stops (no more trailing 0/.).
    QTest::newRow("29.970 -> 29.97") << QByteArray("29.970") << QString("1920x1080 @ 29.97fps");
    // "23.976" → nothing chopped.
    QTest::newRow("23.976 unchanged") << QByteArray("23.976") << QString("1920x1080 @ 23.976fps");
    // Integer-looking string with no dot: loop doesn't enter.
    QTest::newRow("24 unchanged")    << QByteArray("24")     << QString("1920x1080 @ 24fps");
    // Bare trailing dot gets chopped.
    QTest::newRow("30. -> 30")       << QByteArray("30.")    << QString("1920x1080 @ 30fps");
}

void TestMediaInfo::video_fpsTrim()
{
    QFETCH(QByteArray, fps);
    QFETCH(QString, expectedDetails);

    const QByteArray json = "{\"media\":{\"track\":[{"
                            "\"@type\":\"Video\","
                            "\"Format\":\"H.264\","
                            "\"Width\":\"1920\",\"Height\":\"1080\","
                            "\"FrameRate\":\"" + fps + "\""
                            "}]}}";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].first, QStringLiteral("H.264 "));
    QCOMPARE(rows[0].second, expectedDetails);
}

void TestMediaInfo::video_stripVisual()
{
    // "MPEG-4 Visual" has " Visual" (7 chars) chopped from the end.
    const QByteArray json = R"({"media":{"track":[{
        "@type":"Video","Format":"MPEG-4 Visual",
        "Width":"640","Height":"480","FrameRate":"24.000"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].first, QStringLiteral("MPEG-4 "));
    QCOMPARE(rows[0].second, QStringLiteral("640x480 @ 24fps"));
}

void TestMediaInfo::video_formatUnchanged()
{
    // Format that doesn't end in " Visual" should pass through untouched
    // (modulo the single trailing-space label suffix).
    const QByteArray json = R"({"media":{"track":[{
        "@type":"Video","Format":"AV1",
        "Width":"3840","Height":"2160","FrameRate":"60.000"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].first, QStringLiteral("AV1 "));
    QCOMPARE(rows[0].second, QStringLiteral("3840x2160 @ 60fps"));
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

void TestMediaInfo::audio_channels_fallbackToStereo()
{
    // Neither ChannelPositions nor Channels → assume stereo ("2").
    const QByteArray json = R"({"media":{"track":[{
        "@type":"Audio","Format":"Opus","Language":"Spanish"
    }]}})";
    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].first, QStringLiteral("Opus (Spanish) "));
    QCOMPARE(rows[0].second, QStringLiteral("2"));
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
    // H.264 clip in an MP4 container, 23.976 fps, 1080p, with English
    // FLAC audio (5.1 channels) and English SDH subtitles. Duration is
    // 1h23m45s (5025 s).
    const QByteArray json = R"({"media":{"track":[
        {"@type":"General","Format":"MPEG-4","FileSize":"1572864","Duration":"5025.000"},
        {"@type":"Video","Format":"AVC","Width":"1920","Height":"1080","FrameRate":"23.976"},
        {"@type":"Audio","Format":"FLAC","Language":"English","ChannelPositions":"Front: L C R, Side: L R, LFE"},
        {"@type":"Text","Language":"English","Title":"SDH"}
    ]}})";

    const Rows rows = parseMediaInfo(json);
    QCOMPARE(rows.size(), 5);

    QCOMPARE(rows[0], Row(QStringLiteral("Format "),    QStringLiteral("MPEG-4 (1.5MiB)")));
    QCOMPARE(rows[1], Row(QStringLiteral("Duration "),  QStringLiteral("1h23m45s")));
    QCOMPARE(rows[2], Row(QStringLiteral("AVC "),       QStringLiteral("1920x1080 @ 23.976fps")));
    QCOMPARE(rows[3], Row(QStringLiteral("FLAC (English) "), QStringLiteral("Front: L C R, Side: L R, LFE")));
    QCOMPARE(rows[4], Row(QStringLiteral("Subtitles "), QStringLiteral("English (SDH)")));
}

QTEST_APPLESS_MAIN(TestMediaInfo)
#include "tst_mediainfo.moc"
