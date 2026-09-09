#include <serein/scope/HttpRequestClient.h>

#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QSignalBlocker>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <utility>

namespace serein::scope {

class HttpRequestClient::Impl final {
public:
    explicit Impl(HttpRequestClient *owner)
        : owner(owner)
    {
    }

    [[nodiscard]] RequestId start(QNetworkRequest request,
                                  const QByteArray *body = nullptr)
    {
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        const RequestId requestId = nextRequestId++;
        QNetworkReply *reply = body ? network.post(request, *body)
                                    : network.get(request);
        replies.insert(requestId, reply);
        QObject::connect(reply, &QNetworkReply::finished, owner,
                         [this, requestId, reply] { finish(requestId, reply); });
        return requestId;
    }

    [[nodiscard]] RequestId startPut(QNetworkRequest request,
                                     const QByteArray &body)
    {
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        const RequestId requestId = nextRequestId++;
        QNetworkReply *reply = network.put(request, body);
        replies.insert(requestId, reply);
        QObject::connect(reply, &QNetworkReply::finished, owner,
                         [this, requestId, reply] { finish(requestId, reply); });
        return requestId;
    }

    void cancel(const RequestId requestId)
    {
        QPointer<QNetworkReply> reply = replies.take(requestId);
        if (!reply) return;
        const QSignalBlocker blocker(reply);
        reply->abort();
        reply->deleteLater();
    }

    void cancelAll()
    {
        const QHash<RequestId, QPointer<QNetworkReply>> pending =
            std::exchange(replies, {});
        for (const QPointer<QNetworkReply> &reply : pending) {
            if (!reply) continue;
            const QSignalBlocker blocker(reply);
            reply->abort();
            reply->deleteLater();
        }
    }

    void clearConnectionCache()
    {
        network.clearConnectionCache();
    }

private:
    void finish(const RequestId requestId, QNetworkReply *reply)
    {
        const auto it = replies.constFind(requestId);
        if (it == replies.cend() || it.value() != reply) {
            if (reply) reply->deleteLater();
            return;
        }
        replies.erase(it);
        if (!reply) return;
        const QByteArray body = reply->readAll();
        const int statusCode =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString error = reply->error() == QNetworkReply::NoError
                                  ? QString{}
                                  : reply->errorString();
        reply->deleteLater();
        emit owner->completed(requestId, statusCode, body, error);
    }

    HttpRequestClient *owner = nullptr;
    QNetworkAccessManager network;
    QHash<RequestId, QPointer<QNetworkReply>> replies;
    RequestId nextRequestId = 1;
};

HttpRequestClient::HttpRequestClient(QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(this))
{
}

HttpRequestClient::~HttpRequestClient()
{
    cancelAll();
}

HttpRequestClient::RequestId HttpRequestClient::get(const QUrl &url,
                                                     const Headers &headers)
{
    QNetworkRequest request(url);
    for (const auto &[name, value] : headers) request.setRawHeader(name, value);
    return m_impl->start(std::move(request));
}

HttpRequestClient::RequestId HttpRequestClient::post(const QUrl &url,
                                                      const QByteArray &body,
                                                      const Headers &headers)
{
    QNetworkRequest request(url);
    for (const auto &[name, value] : headers) request.setRawHeader(name, value);
    return m_impl->start(std::move(request), &body);
}

HttpRequestClient::RequestId HttpRequestClient::put(const QUrl &url,
                                                     const QByteArray &body,
                                                     const Headers &headers)
{
    QNetworkRequest request(url);
    for (const auto &[name, value] : headers) request.setRawHeader(name, value);
    return m_impl->startPut(std::move(request), body);
}

void HttpRequestClient::cancel(const RequestId requestId)
{
    if (requestId != 0) m_impl->cancel(requestId);
}

void HttpRequestClient::cancelAll()
{
    m_impl->cancelAll();
}

void HttpRequestClient::clearConnectionCache()
{
    m_impl->clearConnectionCache();
}

} // namespace serein::scope
