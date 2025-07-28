#pragma once

#include <QObject>
#include <QHash>
#include <QPluginLoader>
#include <QDir>
#include <memory>

#include "../core/interfaces.h"

namespace AppManager {

class PluginManager : public QObject {
    Q_OBJECT

public:
    static PluginManager& instance();
    
    void loadPlugins(const QString& pluginDir);
    QStringList availablePlugins() const;
    IAppManagerPlugin* getPlugin(const QString& name) const;
    
private:
    PluginManager(QObject* parent = nullptr);
    Q_DISABLE_COPY(PluginManager)
    
    QHash<QString, QPluginLoader*> m_pluginLoaders;
    QHash<QString, IAppManagerPlugin*> m_plugins;
};

} // namespace AppManager