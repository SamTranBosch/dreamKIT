#include "interfaces.h"
#include <QJsonDocument>
#include <QJsonObject>

namespace AppManager {

AppMetadata AppMetadata::fromJson(const QJsonObject& json) {
    AppMetadata metadata;
    metadata.id = json.value("_id").toString();
    metadata.name = json.value("name").toString();
    metadata.version = json.value("version").toString();
    metadata.description = json.value("description").toString();
    metadata.iconUrl = json.value("thumbnail").toString();
    metadata.category = json.value("category").toString();
    metadata.rating = json.value("rating").toDouble();
    metadata.downloads = json.value("downloads").toInt();
    
    if (json.contains("storeId") && json.value("storeId").isObject()) {
        metadata.author = json.value("storeId").toObject().value("name").toString();
    }
    
    for (auto it = json.begin(); it != json.end(); ++it) {
        if (!QStringList{"_id", "name", "version", "description", "thumbnail", 
                         "category", "rating", "downloads", "storeId"}.contains(it.key())) {
            metadata.extendedProperties[it.key()] = it.value();
        }
    }
    
    return metadata;
}

QJsonObject AppMetadata::toJson() const {
    QJsonObject json;
    json["_id"] = id;
    json["name"] = name;
    json["version"] = version;
    json["description"] = description;
    json["thumbnail"] = iconUrl;
    json["category"] = category;
    json["rating"] = rating;
    json["downloads"] = downloads;
    
    QJsonObject storeId;
    storeId["name"] = author;
    json["storeId"] = storeId;
    
    for (auto it = extendedProperties.begin(); it != extendedProperties.end(); ++it) {
        json[it.key()] = it.value();
    }
    
    return json;
}

AppInstallInfo AppInstallInfo::fromJson(const QJsonObject& json) {
    AppInstallInfo info;
    info.appId = json.value("appId").toString();
    info.installPath = json.value("installPath").toString();
    info.installedAt = QDateTime::fromString(json.value("installedAt").toString(), Qt::ISODate);
    info.installerVersion = json.value("installerVersion").toString();
    info.installConfig = json.value("installConfig").toObject();
    info.isActive = json.value("isActive").toBool();
    return info;
}

QJsonObject AppInstallInfo::toJson() const {
    QJsonObject json;
    json["appId"] = appId;
    json["installPath"] = installPath;
    json["installedAt"] = installedAt.toString(Qt::ISODate);
    json["installerVersion"] = installerVersion;
    json["installConfig"] = installConfig;
    json["isActive"] = isActive;
    return json;
}

}