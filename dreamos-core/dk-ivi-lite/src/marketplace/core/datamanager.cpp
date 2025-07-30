#include "datamanager.hpp"
#include "fetching.hpp"

#include "jsonstorage.hpp"
#include "appserializer.hpp"

#include <QDebug>

using Core::JsonStorage;
using Core::AppSerializer;

QJsonArray DataManager::load(const QString &target)
{
    QString folder = DK_CONTAINER_ROOT + "dk_marketplace/";
    QString filePath = (target == QLatin1String("vehicle"))
                        ? folder + "installedapps.json"
                        : folder + "installedservices.json";

    // If file is missing or unreadable, it will be created with 'def' and
    // that default document is returned.
    auto doc = JsonStorage::load(filePath, QJsonValue(QJsonArray()));
    if (doc.isNull()) {
        qWarning() << "DataManager::load: cannot read" << filePath;
        return {};
    }
    if (!doc.isArray()) {
        qWarning() << "DataManager::load: array expected in" << filePath;
        return {};
    }
    qDebug() << "DataManager::load: loaded" << filePath;
    return doc.array();
}

bool DataManager::save(const QString &target, const QJsonArray &arr)
{
    QString folder = DK_CONTAINER_ROOT + "dk_marketplace/";
    QString filePath = (target == QLatin1String("vehicle"))
                        ? folder + "installedapps.json"
                        : folder + "installedservices.json";

    // Create a QJsonDocument from the QJsonArray
    QJsonDocument m_doc(arr);
    auto ret = JsonStorage::save(filePath, m_doc);
    if (!ret) {
        qWarning() << "DataManager::save: cannot write" << filePath;
        return false;
    }
    qDebug() << "DataManager::save: saved" << filePath;
    return true;
}

QList<AppInfo> DataManager::fetchAppList(const FetchOptions &opt)
{
    // 1) optional auth
    QString token;
    if (!opt.loginUrl.isEmpty())
        token = marketplace_login(opt.loginUrl,
                                  opt.username,
                                  opt.password);

    // 2) HTTP request (writes marketplace_data_installcfg.json)
    if (!queryMarketplacePackages(opt.marketUrl, token,
                                  opt.page, opt.limit, opt.category))
    {
        qWarning() << "DataManager::fetchAppList: HTTP failed";
        return {};
    }

    // 3) load JSON that fetcher stored
    const QString listPath = opt.rootFolder + "/marketplace_data_installcfg.json";
    const auto doc = JsonStorage::load(listPath, QJsonValue(QJsonArray()));
    if (!doc.isArray()) {
        qWarning() << "DataManager::fetchAppList: array expected in" << listPath;
        return {};
    }

    // 4) parse
    return AppSerializer::listFromJson(doc.array());
}
