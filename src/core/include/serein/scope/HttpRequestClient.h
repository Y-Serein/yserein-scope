#pragma once

#include "CoreExport.h"

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QPair>
#include <QtCore/QString>
#include <QtCore/QUrl>
#include <QtCore/QtTypes>

#include <memory>

namespace serein::scope {

// Host-owned asynchronous HTTP client. Keeping the QtNetwork objects in the
// core DLL means a component can close and unload without retaining QtNetwork
// callbacks into its own module.
class SEREIN_SCOPE_CORE_EXPORT HttpRequestClient final : public QObject {
    Q_OBJECT

public:
    using RequestId = quint64;
    using Headers = QList<QPair<QByteArray, QByteArray>>;

    explicit HttpRequestClient(QObject *parent = nullptr);
    ~HttpRequestClient() override;

    HttpRequestClient(const HttpRequestClient &) = delete;
    HttpRequestClient &operator=(const HttpRequestClient &) = delete;

    [[nodiscard]] RequestId get(const QUrl &url, const Headers &headers = {});
    [[nodiscard]] RequestId post(const QUrl &url, const QByteArray &body,
                                 const Headers &headers = {});
    [[nodiscard]] RequestId put(const QUrl &url, const QByteArray &body,
                                const Headers &headers = {});
    void cancel(RequestId requestId);
    void cancelAll();
    void clearConnectionCache();

signals:
    void completed(serein::scope::HttpRequestClient::RequestId requestId,
                   int statusCode, const QByteArray &body,
                   const QString &error);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace serein::scope
