#pragma once

#include "../core/interfaces.h"

namespace AppManager {

class JsonConfigurationProvider : public IConfigurationProvider {
    Q_OBJECT

public:
    explicit JsonConfigurationProvider(const QString& configPath, QObject* parent = nullptr);

    QVariant getValue(const QString& key, const QVariant& defaultValue = QVariant()) const override;
    void setValue(const QString& key, const QVariant& value) override;
    QStringList getCategories() const override;
    QString getInstallPath() const override;
    QString getCachePath() const override;

private:
    void loadConfiguration();
    void saveConfiguration();

    QString m_configPath;
    QJsonObject m_config;
};

}
