#include "applistmodel.h"

namespace AppManager {

AppListModel::AppListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int AppListModel::rowCount(const QModelIndex& parent) const {
    Q_UNUSED(parent)
    return m_apps.size();
}

QVariant AppListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_apps.size()) {
        return QVariant();
    }

    const AppItem& item = m_apps.at(index.row());

    switch (role) {
    case IdRole: return item.metadata.id;
    case NameRole: return item.metadata.name;
    case AuthorRole: return item.metadata.author;
    case VersionRole: return item.metadata.version;
    case DescriptionRole: return item.metadata.description;
    case IconUrlRole: return item.metadata.iconUrl;
    case CategoryRole: return item.metadata.category;
    case RatingRole: return item.metadata.rating;
    case DownloadsRole: return item.metadata.downloads;
    case StatusRole: return static_cast<int>(item.status);
    case IsInstalledRole: return item.isInstalled;
    case IsRunningRole: return item.isRunning;
    default: return QVariant();
    }
}

QHash<int, QByteArray> AppListModel::roleNames() const {
    static QHash<int, QByteArray> roles = {
        {IdRole, "id"},
        {NameRole, "name"},
        {AuthorRole, "author"},
        {VersionRole, "version"},
        {DescriptionRole, "description"},
        {IconUrlRole, "iconUrl"},
        {CategoryRole, "category"},
        {RatingRole, "rating"},
        {DownloadsRole, "downloads"},
        {StatusRole, "status"},
        {IsInstalledRole, "isInstalled"},
        {IsRunningRole, "isRunning"}
    };
    return roles;
}

void AppListModel::updateApps(const QList<AppMetadata>& apps) {
    beginResetModel();
    m_apps.clear();
    for (const auto& metadata : apps) {
        AppItem item;
        item.metadata = metadata;
        item.status = AppStatus::Available;
        m_apps.append(item);
    }
    endResetModel();
}

void AppListModel::updateAppStatus(const QString& appId, AppStatus status) {
    for (int i = 0; i < m_apps.size(); ++i) {
        if (m_apps[i].metadata.id == appId) {
            m_apps[i].status = status;
            m_apps[i].isInstalled = (status == AppStatus::Installed || status == AppStatus::Running);
            m_apps[i].isRunning = (status == AppStatus::Running);

            QModelIndex index = this->index(i);
            emit dataChanged(index, index);
            break;
        }
    }
}

QVariantMap AppListModel::get(int index) const {
    QVariantMap map;
    if (index >= 0 && index < m_apps.size()) {
        const AppItem& item = m_apps.at(index);
        map["id"] = item.metadata.id;
        map["name"] = item.metadata.name;
        map["author"] = item.metadata.author;
        map["version"] = item.metadata.version;
        map["description"] = item.metadata.description;
        map["iconUrl"] = item.metadata.iconUrl;
        map["category"] = item.metadata.category;
        map["rating"] = item.metadata.rating;
        map["downloads"] = item.metadata.downloads;
        map["status"] = static_cast<int>(item.status);
        map["isInstalled"] = item.isInstalled;
        map["isRunning"] = item.isRunning;
    }
    return map;
}

int AppListModel::findAppIndex(const QString& appId) const {
    for (int i = 0; i < m_apps.size(); ++i) {
        if (m_apps[i].metadata.id == appId) {
            return i;
        }
    }
    return -1;
}

}
