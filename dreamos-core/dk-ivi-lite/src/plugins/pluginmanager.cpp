#include "pluginmanager.h"
#include <QCoreApplication>
#include <QDebug>

namespace AppManager {

PluginManager::PluginManager(QObject* parent)
    : QObject(parent)
{
}

PluginManager& PluginManager::instance() {
    static PluginManager instance;
    return instance;
}

void PluginManager::loadPlugins(const QString& pluginDir) {
    QDir dir(pluginDir);
    for (const QString& fileName : dir.entryList(QDir::Files)) {
        QPluginLoader* loader = new QPluginLoader(dir.absoluteFilePath(fileName), this);
        if (loader->load()) {
            QObject* plugin = loader->instance();
            if (plugin) {
                IAppManagerPlugin* appPlugin = qobject_cast<IAppManagerPlugin*>(plugin);
                if (appPlugin) {
                    m_plugins.insert(appPlugin->pluginName(), appPlugin);
                    m_pluginLoaders.insert(appPlugin->pluginName(), loader);
                    qDebug() << "Loaded plugin:" << appPlugin->pluginName();
                } else {
                    qWarning() << "Could not cast plugin to IAppManagerPlugin:" << fileName;
                    loader->unload();
                    delete loader;
                }
            } else {
                qWarning() << "Could not get plugin instance:" << loader->errorString();
                delete loader;
            }
        } else {
            qWarning() << "Could not load plugin:" << loader->fileName() << loader->errorString();
            delete loader;
        }
    }
}

QStringList PluginManager::availablePlugins() const {
    return m_plugins.keys();
}

IAppManagerPlugin* PluginManager::getPlugin(const QString& name) const {
    return m_plugins.value(name);
}

} // namespace AppManager
