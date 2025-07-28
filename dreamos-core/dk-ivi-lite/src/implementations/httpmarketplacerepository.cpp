#include "httpmarketplacerepository.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QEventLoop>
#include <QtConcurrent>

namespace AppManager {

HttpMarketplaceRepository::HttpMarketplaceRepository(const QString& baseUrl, QObject* parent)
    : IAppRepository(parent)
    , m_baseUrl(baseUrl)
    , m_networkManager(new QNetworkAccessManager(this))
{
    // Extract name from URL
    QUrl url(baseUrl);
    m_name = url.host();
}

QFuture<QList<AppMetadata>> HttpMarketplaceRepository::searchApps(
    const QString& query, const QString& category, int page, int limit) {

    return QtConcurrent::run([this, query, category, page, limit]() -> QList<AppMetadata> {
        QUrl url(m_baseUrl + "/package");
        QUrlQuery urlQuery;
        urlQuery.addQueryItem("page", QString::number(page));
        urlQuery.addQueryItem("limit", QString::number(limit));
        if (!category.isEmpty()) {
            urlQuery.addQueryItem("category", category);
        }
        if (!query.isEmpty()) {
            urlQuery.addQueryItem("search", query);
        }
        url.setQuery(urlQuery);

        QNetworkRequest request(url);
        if (!m_authToken.isEmpty()) {
            request.setRawHeader("Authorization", ("Bearer " + m_authToken).toUtf8());
        }

        QEventLoop loop;
        QNetworkReply* reply = m_networkManager->get(request);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        QList<AppMetadata> apps;

        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonObject response = doc.object();
            QJsonArray data = response.value("data").toArray();

            for (const auto& item : data) {
                if (item.isObject()) {
                    apps.append(AppMetadata::fromJson(item.toObject()));
                }
            }
        } else {
            emit error("Network error: " + reply->errorString());
        }

        reply->deleteLater();
        return apps;
    });
}

QFuture<AppMetadata> HttpMarketplaceRepository::getAppDetails(const QString& appId) {
    return QtConcurrent::run([this, appId]() -> AppMetadata {
        QUrl url(m_baseUrl + "/package/" + appId);
        QNetworkRequest request(url);
        if (!m_authToken.isEmpty()) {
            request.setRawHeader("Authorization", ("Bearer " + m_authToken).toUtf8());
        }

        QEventLoop loop;
        QNetworkReply* reply = m_networkManager->get(request);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        AppMetadata metadata;

        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject()) {
                metadata = AppMetadata::fromJson(doc.object());
            }
        } else {
            emit error("Failed to get app details: " + reply->errorString());
        }

        reply->deleteLater();
        return metadata;
    });
}

QFuture<QByteArray> HttpMarketplaceRepository::downloadAppPackage(const QString& appId) {
    return QtConcurrent::run([this, appId]() -> QByteArray {
        QUrl url(m_baseUrl + "/package/" + appId + "/download");
        QNetworkRequest request(url);
        if (!m_authToken.isEmpty()) {
            request.setRawHeader("Authorization", ("Bearer " + m_authToken).toUtf8());
        }

        QEventLoop loop;
        QNetworkReply* reply = m_networkManager->get(request);

        // Connect progress signal
        QObject::connect(reply, &QNetworkReply::downloadProgress,
                         [this, appId](qint64 received, qint64 total) {
            if (total > 0) {
                int percentage = (received * 100) / total;
                emit downloadProgress(appId, percentage);
            }
        });

        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        QByteArray data;

        if (reply->error() == QNetworkReply::NoError) {
            data = reply->readAll();
        } else {
            emit error("Download failed: " + reply->errorString());
        }

        reply->deleteLater();
        return data;
    });
}

QFuture<bool> HttpMarketplaceRepository::authenticate(const QString& username, const QString& password) {
    return QtConcurrent::run([this, username, password]() -> bool {
        if (m_loginUrl.isEmpty()) {
            return true; // No authentication required
        }

        QUrl url(m_loginUrl);
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QJsonObject loginData;
        loginData["email"] = username;
        loginData["password"] = password;

        QEventLoop loop;
        QNetworkReply* reply = m_networkManager->post(request, QJsonDocument(loginData).toJson());
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        bool success = false;

        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonObject response = doc.object();
            m_authToken = response.value("token").toString();
            success = !m_authToken.isEmpty();
        } else {
            emit error("Authentication failed: " + reply->errorString());
        }

        reply->deleteLater();
        return success;
    });
}

}
