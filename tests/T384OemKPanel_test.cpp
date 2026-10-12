#include "T384OemKPanel.h"
#include <QtCore/QJsonDocument>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>

using namespace serein::scope::t384;

class OemKPanelTest final : public QObject {
    Q_OBJECT
    QTcpServer server;
    QJsonObject status;
    QList<QByteArray> writes;
    bool losePost = false;
    bool unsupported = false;
    bool uncertain = false;
    bool wrongIdentity = false;
    void reply(QTcpSocket *socket, const QByteArray &body, int code = 200) {
        socket->write("HTTP/1.1 " + QByteArray::number(code) + " Result\r\nConnection: close\r\nContent-Length: "
            + QByteArray::number(body.size()) + "\r\n\r\n" + body);
        socket->disconnectFromHost();
    }
    QPushButton *button(T384OemKPanel &panel, const char *name) {
        return panel.findChild<QPushButton *>(QStringLiteral("t384OemK_") + QString::fromLatin1(name));
    }
    void action(T384OemKPanel &panel, const char *name) {
        QTimer::singleShot(0, &panel, [] {
            auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (dialog) dialog->button(QMessageBox::Yes)->click();
        });
        button(panel, name)->click();
    }
private slots:
    void init() {
        status = {{"schema",1},{"revision",0},{"step",0},{"action",0},
                  {"result",0},{"error",0},{"uart_status",255},{"locked",false}};
        writes.clear(); losePost = unsupported = uncertain = wrongIdentity = false;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (auto *socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    QByteArray bytes = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", bytes);
                    const auto end = bytes.indexOf("\r\n\r\n");
                    if (end < 0) return;
                    if (bytes.startsWith("GET /diag ")) {
                        reply(socket, QByteArray("stream.active=0\nmini2.pn=TIFSC640\nmini2.sn=")
                            + (wrongIdentity ? "other" : "fixture") + "\nmini2.firmware_version=01.00.01.03\n");
                    } else if (bytes.startsWith("GET /api/v1/oem-k/status ")) {
                        reply(socket, QJsonDocument(status).toJson(QJsonDocument::Compact), unsupported ? 404 : 200);
                    } else if (bytes.startsWith("POST /api/v1/oem-k/action ")) {
                        int length = -1;
                        for (const auto &line : bytes.left(end).split('\n'))
                            if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                        if (length < 0 || bytes.size() < end + 4 + length) return;
                        const QByteArray body = bytes.mid(end + 4, length);
                        writes.append(body);
                        const auto parts = body.split(' ');
                        const QList<QByteArray> names = {"low","high","compute","save","cancel"};
                        const int i = int(names.indexOf(parts[0]));
                        status["revision"] = status["revision"].toInt()+1;
                        status["action"] = i;
                        status["result"] = uncertain ? 4 : 2;
                        status["locked"] = uncertain;
                        status["uart_status"] = uncertain ? 255 : 0;
                        if (!uncertain) status["step"] = i+1;
                        if (losePost) { socket->abort(); return; }
                        reply(socket, QJsonDocument(status).toJson(QJsonDocument::Compact));
                    }
                });
            }
        });
    }
    void cleanup() { server.close(); disconnect(&server, nullptr, this, nullptr); }
    void manualSaveAndCancel() {
        T384OemKPanel panel;
        panel.setEndpoint(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
        QVERIFY(!button(panel,"low")->isEnabled());
        button(panel,"refresh")->click();
        QTRY_VERIFY(button(panel,"low")->isEnabled());
        action(panel,"low"); QTRY_VERIFY(button(panel,"high")->isEnabled());
        QVERIFY(!button(panel,"save")->isEnabled());
        action(panel,"high"); QTRY_VERIFY(button(panel,"compute")->isEnabled());
        action(panel,"compute"); QTRY_VERIFY(button(panel,"save")->isEnabled());
        QCOMPARE(writes, QList<QByteArray>({"low 0","high 1","compute 2"}));
        action(panel,"save"); QTRY_VERIFY(button(panel,"low")->isEnabled());
        QCOMPARE(writes.last(), QByteArray("save 3"));
        action(panel,"low"); QTRY_VERIFY(button(panel,"cancel")->isEnabled());
        action(panel,"cancel"); QTRY_VERIFY(button(panel,"low")->isEnabled());
        QCOMPARE(writes.last(), QByteArray("cancel 5"));
        QVERIFY(!panel.holdsCalibration());
    }
    void lostPostIsNotRepeated() {
        T384OemKPanel panel;
        panel.setEndpoint(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
        button(panel,"refresh")->click(); QTRY_VERIFY(button(panel,"low")->isEnabled());
        losePost = true;
        action(panel,"low"); QTRY_VERIFY(button(panel,"high")->isEnabled());
        QCOMPARE(writes.size(), 1);
        panel.shutdown();
        QCOMPARE(writes.size(), 1); // No implicit cancel on close.
    }
    void uncertainBlocksWrites() {
        T384OemKPanel panel;
        panel.setEndpoint(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
        button(panel,"refresh")->click(); QTRY_VERIFY(button(panel,"low")->isEnabled());
        uncertain = true;
        action(panel,"low"); QTRY_VERIFY(!panel.busy());
        QVERIFY(!button(panel,"low")->isEnabled());
        QVERIFY(!button(panel,"cancel")->isEnabled());
        QVERIFY(panel.holdsCalibration());
        QCOMPARE(writes.size(), 1);
    }
    void oldFirmwareCannotWrite() {
        T384OemKPanel panel;
        panel.setEndpoint(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
        unsupported = true;
        button(panel,"refresh")->click(); QTRY_VERIFY(!panel.busy());
        QVERIFY(!button(panel,"low")->isEnabled());
        QVERIFY(writes.isEmpty());
    }
    void deviceSwapStopsWorkflow() {
        T384OemKPanel panel;
        panel.setEndpoint(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
        button(panel,"refresh")->click(); QTRY_VERIFY(button(panel,"low")->isEnabled());
        action(panel,"low"); QTRY_VERIFY(button(panel,"high")->isEnabled());
        wrongIdentity = true;
        action(panel,"high"); QTRY_VERIFY(!panel.busy());
        QCOMPARE(writes.size(), 1);
    }
};
QTEST_MAIN(OemKPanelTest)
#include "T384OemKPanel_test.moc"
