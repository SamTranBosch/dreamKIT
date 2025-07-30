#pragma once
// datamanager.hpp   (only “business / HTTP” parts remain)

#include <QString>
#include <QJsonDocument>
#include <QJsonArray>
#include <QList>
#include <QMutex>
#include "dashboardconfig.hpp"

extern QString DK_VCU_USERNAME;
extern QString DK_ARCH;
extern QString DK_DOCKER_HUB_NAMESPACE;
extern QString DK_CONTAINER_ROOT;

struct AppInfo {
    QString id, name, author, iconUrl, folderName, packageLink;
    double  rating     = 0;
    int     downloads  = 0;
    bool    isInstalled = false;
    DashboardConfig dashboardConfig;
};

class DataManager
{
public:
    struct FetchOptions {
        QString marketUrl;
        QString loginUrl;
        QString username;
        QString password;
        QString category;
        int     page    = 1;
        int     limit   = 20;
        QString rootFolder;
    };

    QJsonArray load(const QString &target);
    bool save(const QString &target, const QJsonArray &arr);
    static QList<AppInfo> fetchAppList(const FetchOptions &opt);

private:
    // helper utilities now live in Core::JsonStorage / Core::AppSerializer
};
