#pragma once

#include <QAbstractListModel>
#include <QObject>
#include "../core/interfaces.h"

namespace AppManager {

class AppListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        AuthorRole,
        VersionRole,
        DescriptionRole,
        IconUrlRole,
        CategoryRole,
        RatingRole,
        DownloadsRole,
        StatusRole,
        IsInstalledRole,
        IsRunningRole
    };
    Q_ENUM(Roles)

    explicit AppListModel(QObject* parent = nullptr);

    // QAbstractListModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Model management
    void updateApps(const QList<AppMetadata>& apps);
    void updateAppStatus(const QString& appId, AppStatus status);

    Q_INVOKABLE QVariantMap get(int index) const;
    Q_INVOKABLE int findAppIndex(const QString& appId) const;

private:
    struct AppItem {
        AppMetadata metadata;
        AppStatus status = AppStatus::Unknown;
        bool isInstalled = false;
        bool isRunning = false;
    };

    QList<AppItem> m_apps;
};

}
