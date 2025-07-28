#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>

#include "../core/applicationmanager.h"
#include "../factory/appmanagerfactory.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    
    // Register QML types
    qmlRegisterType<AppManager::ApplicationManager>("AppManager", 1, 0, "ApplicationManager");
    qmlRegisterType<AppManager::AppListModel>("AppManager", 1, 0, "AppListModel");
    qmlRegisterUncreatableType<AppManager::AppStatus>("AppManager", 1, 0, "AppStatus", "Enum type");
    
    // Create application manager with auto-detected platform
    auto appManager = AppManager::AppManagerFactory::createManager("auto", "");
    
    QQmlApplicationEngine engine;
    
    // Expose the manager to QML
    engine.rootContext()->setContextProperty("appManagerInstance", appManager.get());
    
    // Load main QML file
    const QUrl url(QStringLiteral("qrc:/AppManager/main/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);
    
    engine.load(url);
    
    return app.exec();
}