#include "jsonconfigurationprovider.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>

namespace AppManager {

JsonConfigurationProvider::JsonConfigurationProvider(const QString& configPath, QObject* parent)
    : IConfigurationProvider(parent), m_configPath(configPath)
{
    loadConfiguration();
}

QVariant JsonConfigurationProvider::getValue(const QString& key, const QVariant& defaultValue) const {
    QStringList keyParts = key.split('.');
    QJsonValue value = m_config;

    for (const QString& part : keyParts) {
        if (value.isObject()) {
            value = value.toObject().value(part);
        } else {
            return defaultValue;
        }
    }

    return value.toVariant();
}

void JsonConfigurationProvider::setValue(const QString& key, const QVariant& value) {
    QStringList keyParts = key.split('.');
    QJsonObject* current = &m_config;

    for (int i = 0; i < keyParts.size() - 1; ++i) {
        const QString& part = keyParts[i];
        if (!current->contains(part) || !(*current)[part].isObject()) {
            (*current)[part] = QJsonObject();
        }
        QJsonValueRef ref = (*current)[part];
        if (!ref.isObject()) {
            ref = QJsonObject();
        }
        QJsonObject obj = ref.toObject();
        current = &obj;
    }

    (*current)[keyParts.last()] = QJsonValue::fromVariant(value);

    saveConfiguration();
    emit configurationChanged(key, value);
}

QStringList JsonConfigurationProvider::getCategories() const {
    QJsonArray categories = m_config.value("categories").toArray();
    QStringList result;
    for (const auto& category : categories) {
        result.append(category.toString());
    }
    return result;
}

QString JsonConfigurationProvider::getInstallPath() const {
    return getValue("paths.install", QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/apps").toString();
}

QString JsonConfigurationProvider::getCachePath() const {
    return getValue("paths.cache", QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).toString();
}

void JsonConfigurationProvider::loadConfiguration() {
    QFile file(m_configPath);
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isObject()) {
            m_config = doc.object();
        }
        file.close();
    } else {
        // Create default configuration
        m_config = QJsonObject{
            {"categories", QJsonArray{"vehicle", "vehicle-service"}},
            {"paths", QJsonObject{
                {"install", QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/apps"},
                {"cache", QStandardPaths::writableLocation(QStandardPaths::CacheLocation)}
            }},
            {"repositories", QJsonArray{
                QJsonObject{
                    {"name", "Default Marketplace"},
                    {"url", "https://store-be.digitalauto.tech"},
                    {"loginUrl", ""}
                }
            }}
        };
        saveConfiguration();
    }
}

void JsonConfigurationProvider::saveConfiguration() {
    QFileInfo fileInfo(m_configPath);
    QDir().mkpath(fileInfo.path());

    QFile file(m_configPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(m_config).toJson());
        file.close();
    }
}

}
