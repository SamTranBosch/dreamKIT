#pragma once

#include "../core/interfaces.h"
#include <QNetworkAccessManager>

namespace AppManager {

class HttpMarketplaceRepository : public IAppRepository {
    Q_OBJECT
    
public:
    explicit HttpMarketplaceRepository(const QString& baseUrl, QObject* parent = nullptr);
    
    QFuture<QList<AppMetadata>> searchApps(
        const QString& query = "",
        const QString& category = "",
        int page = 1,
        int limit = 20
    ) override;
    
    QFuture<AppMetadata> getAppDetails(const QString& appId) override;
    QFuture<QByteArray> downloadAppPackage(const QString& appId) override;
    
    QString name() const override { return m_name; }
    bool requiresAuthentication() const override { return !m_loginUrl.isEmpty(); }
    QFuture<bool> authenticate(const QString& username, const QString& password) override;

private:
    QString m_baseUrl;
    QString m_loginUrl;
    QString m_name;
    QString m_authToken;
    QNetworkAccessManager* m_networkManager;
};

} // namespace AppManager