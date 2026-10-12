#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtWidgets/QWidget>
#include <functional>

class QLabel;
class QPushButton;
class QNetworkReply;

namespace serein::scope::t384 {
// OEM spatial K workflow. Does not fit/upload the host temperature model.
class T384OemKPanel final : public QWidget {
    Q_OBJECT
public:
    explicit T384OemKPanel(QWidget *parent = nullptr);
    ~T384OemKPanel() override { shutdown(); }
    void setEndpoint(const QString &endpoint);
    void shutdown();
    bool busy() const { return m_busy || m_shutdown; }
    bool holdsCalibration() const;
signals:
    void prepareCommand();
    void commandSubmitted();
    void operationFinished(bool resumeStream);
    void availabilityChanged();
    void record(const QString &message);
private:
    void begin(int action);
    void waitForStream();
    void readStatus(bool beforeCommand);
    void sendAction();
    void finish(const QString &message, bool resume);
    void updateButtons();
    void request(const QString &path, const QByteArray &body,
                 std::function<void(int, const QByteArray &)> complete);
    bool acceptStatus(const QJsonObject &status);
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_poll;
    QTimer m_deadline;
    QElapsedTimer m_elapsed;
    QUrl m_endpoint;
    QString m_identity;
    QJsonObject m_status;
    QLabel *m_feedback = nullptr;
    QPushButton *m_refresh = nullptr;
    QList<QPushButton *> m_actions;
    quint32 m_expectedRevision = 0;
    int m_action = -1;
    bool m_busy = false;
    bool m_shutdown = false;
    bool m_uncertain = false;
    bool m_postSent = false;
};
}
