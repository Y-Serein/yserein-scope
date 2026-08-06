#include "DelimitedEngine.h"
#include "Tn160Engine.h"
#include "Tn160PortSelection.h"

#include <serein/scope/BufferedDecoder.h>

#include <QtCore/QtEndian>
#include <QtTest/QTest>

#include <memory>

using namespace serein::scope;

class EngineTests final : public QObject {
    Q_OBJECT

private slots:
    void delimitedEngineKeepsPartialLine();
    void invalidTextRemainsVisible();
    void rawTextEngineKeepsPartialUtf8();
    void tn160PortSelectionAvoidsUnrelatedDefault();
    void tn160EngineResynchronizes();
    void tn160EngineDecodesGuiTelemetry();
};

void EngineTests::delimitedEngineKeepsPartialLine()
{
    BufferedDecoder decoder(std::make_unique<serial::DelimitedEngine>());
    QCOMPARE(decoder.feed(QByteArrayView("1.5,2", 5)).size(), 0);
    QCOMPARE(decoder.pendingBytes(), 5);
    const QByteArray tail(".5\n3 4\n");
    const PacketList packets = decoder.feed(QByteArrayView(tail));
    QCOMPARE(packets.size(), 2);
    const auto *first = std::get_if<SamplePacket>(&packets[0]);
    QVERIFY(first);
    QCOMPARE(first->values, QVector<double>({1.5, 2.5}));
    QCOMPARE(decoder.pendingBytes(), 0);
}

void EngineTests::invalidTextRemainsVisible()
{
    BufferedDecoder decoder(std::make_unique<serial::DelimitedEngine>());
    const QByteArray input("device ready\n");
    const PacketList packets = decoder.feed(QByteArrayView(input));
    QCOMPARE(packets.size(), 2);
    QVERIFY(std::holds_alternative<TextPacket>(packets[0]));
    QVERIFY(std::holds_alternative<EventPacket>(packets[1]));
}

void EngineTests::rawTextEngineKeepsPartialUtf8()
{
    BufferedDecoder decoder(std::make_unique<serial::RawTextEngine>());
    const QByteArray first = QByteArray::fromHex("e4b8");
    QCOMPARE(decoder.feed(QByteArrayView(first)).size(), 0);
    QCOMPARE(decoder.pendingBytes(), 2);
    const QByteArray tail = QByteArray::fromHex("ad");
    const PacketList packets = decoder.feed(QByteArrayView(tail));
    QCOMPARE(packets.size(), 1);
    const auto *text = std::get_if<TextPacket>(&packets.front());
    QVERIFY(text);
    QCOMPARE(text->text, QStringLiteral("中"));
    QCOMPARE(decoder.pendingBytes(), 0);
}

void EngineTests::tn160PortSelectionAvoidsUnrelatedDefault()
{
    const QList<SerialPortInfo> ports{
        {QStringLiteral("COM3"), QStringLiteral("USB serial device (COM3)")},
        {QStringLiteral("COM27"), QStringLiteral("USB-Enhanced-SERIAL-B2 CH346 (COM27)")},
        {QStringLiteral("COM28"), QStringLiteral("USB-Enhanced-SERIAL-A2 CH346 (COM28)")},
    };
    QCOMPARE(tn160::preferredTn160PortIndex(ports), 2);

    const QList<SerialPortInfo> unrelated{
        {QStringLiteral("COM3"), QStringLiteral("USB serial device (COM3)")},
        {QStringLiteral("COM7"), QStringLiteral("Debug adapter (COM7)")},
    };
    QCOMPARE(tn160::preferredTn160PortIndex(unrelated), -1);
}

void EngineTests::tn160EngineResynchronizes()
{
    QByteArray frame(tn160::FrameSize, '\0');
    frame[0] = char(0xff);
    const int base = 1 + tn160::FrameWidth * tn160::FrameHeight;
    qToBigEndian<quint16>(8192, reinterpret_cast<uchar *>(frame.data() + base));
    qToBigEndian<qint16>(0x7fff, reinterpret_cast<uchar *>(frame.data() + base + 2));
    qToBigEndian<qint16>(0x7fff, reinterpret_cast<uchar *>(frame.data() + base + 4));
    qToBigEndian<quint16>(8192, reinterpret_cast<uchar *>(frame.data() + base + 44));
    qToBigEndian<qint16>(250, reinterpret_cast<uchar *>(frame.data() + base + 48));
    qToBigEndian<quint16>(7800, reinterpret_cast<uchar *>(frame.data() + base + 52));

    BufferedDecoder decoder(std::make_unique<tn160::Tn160Engine>());
    const QByteArray first = QByteArray("noise") + frame.first(120);
    QCOMPARE(decoder.feed(QByteArrayView(first)).size(), 0);
    const QByteArray tail = frame.sliced(120);
    const PacketList packets = decoder.feed(QByteArrayView(tail));
    QCOMPARE(packets.size(), 2);
    QVERIFY(std::holds_alternative<ImagePacket>(packets[0]));
    const auto *sample = std::get_if<SamplePacket>(&packets[1]);
    QVERIFY(sample);
    QCOMPARE(sample->values[0], 25.0);
    QCOMPARE(decoder.pendingBytes(), 0);
}

void EngineTests::tn160EngineDecodesGuiTelemetry()
{
    QByteArray frame(tn160::FrameSize, '\0');
    frame[0] = char(0xff);
    const int base = 1 + tn160::FrameWidth * tn160::FrameHeight;
    qToBigEndian<quint16>(8192, reinterpret_cast<uchar *>(frame.data() + base));
    qToBigEndian<qint16>(-120, reinterpret_cast<uchar *>(frame.data() + base + 2));
    qToBigEndian<qint16>(860, reinterpret_cast<uchar *>(frame.data() + base + 4));
    qToBigEndian<qint32>(123456, reinterpret_cast<uchar *>(frame.data() + base + 6));
    qToBigEndian<quint16>(4000, reinterpret_cast<uchar *>(frame.data() + base + 10));
    qToBigEndian<quint16>(7000, reinterpret_cast<uchar *>(frame.data() + base + 12));
    qToBigEndian<quint16>(1800, reinterpret_cast<uchar *>(frame.data() + base + 18));
    qToBigEndian<quint16>(1900, reinterpret_cast<uchar *>(frame.data() + base + 20));
    frame[base + 22] = char(2);
    frame[base + 23] = char(7);
    qToBigEndian<qint16>(-3, reinterpret_cast<uchar *>(frame.data() + base + 24));
    qToBigEndian<qint16>(4, reinterpret_cast<uchar *>(frame.data() + base + 26));
    qToBigEndian<qint16>(5, reinterpret_cast<uchar *>(frame.data() + base + 28));
    qToBigEndian<quint16>(2000, reinterpret_cast<uchar *>(frame.data() + base + 44));
    qToBigEndian<qint16>(210, reinterpret_cast<uchar *>(frame.data() + base + 46));
    qToBigEndian<qint16>(250, reinterpret_cast<uchar *>(frame.data() + base + 48));
    qToBigEndian<quint16>(7700, reinterpret_cast<uchar *>(frame.data() + base + 50));
    qToBigEndian<quint16>(7800, reinterpret_cast<uchar *>(frame.data() + base + 52));

    tn160::MonitorTelemetry telemetry;
    QVERIFY(tn160::Tn160Engine::decodeTelemetry(QByteArrayView(frame), telemetry));
    QCOMPARE(telemetry.tLowX10, qint16(-120));
    QCOMPARE(telemetry.tHighX10, qint16(860));
    QCOMPARE(telemetry.nucDecision, quint8(2));
    QCOMPARE(telemetry.nucCount, quint8(7));
    QCOMPARE(telemetry.centerRoiAdu, quint16(7800));

    BufferedDecoder decoder(std::make_unique<tn160::Tn160Engine>());
    const PacketList packets = decoder.feed(QByteArrayView(frame));
    const auto *sample = std::get_if<SamplePacket>(&packets[1]);
    QVERIFY(sample);
    QCOMPARE(sample->values.size(), 26);
    QCOMPARE(sample->values.at(0), 25.0);
    QCOMPARE(sample->values.at(14), 2.0);
    QCOMPARE(sample->values.at(15), 7.0);
    QCOMPARE(sample->labels.at(14), QStringLiteral("NUC decision"));
}

QTEST_APPLESS_MAIN(EngineTests)

#include "EngineTests.moc"
