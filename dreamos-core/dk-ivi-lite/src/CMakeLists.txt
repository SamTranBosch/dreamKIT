cmake_minimum_required(VERSION 3.16)

project(AppManager VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Find required Qt components
find_package(Qt6 6.2 REQUIRED COMPONENTS 
    Core 
    Quick 
    Network 
    Concurrent
)

# Library target for the abstract framework
add_library(AppManagerFramework STATIC
    # Core interfaces
    core/applicationmanager.h
    core/applicationmanager.cpp
    core/interfaces.h
    core/interfaces.cpp
    
    # Concrete implementations
    implementations/k8sdockerinstaller.h
    implementations/k8sdockerinstaller.cpp
    implementations/k8sdockerruntime.h
    implementations/k8sdockerruntime.cpp
    implementations/httpmarketplacerepository.h
    implementations/httpmarketplacerepository.cpp
    implementations/jsonconfigurationprovider.h
    implementations/jsonconfigurationprovider.cpp
    
    # Factory and plugin system
    factory/appmanagerfactory.h
    factory/appmanagerfactory.cpp
    # plugins/pluginmanager.h
    # plugins/pluginmanager.cpp
    
    # Models for QML
    models/applistmodel.h
    models/applistmodel.cpp
)

target_link_libraries(AppManagerFramework PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::Concurrent
)

target_include_directories(AppManagerFramework PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

# QML module for the framework
qt6_add_qml_module(AppManagerFramework
    URI AppManager
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/AppManager
    SOURCES
        core/applicationmanager.h
        core/applicationmanager.cpp
        models/applistmodel.h
        models/applistmodel.cpp
    QML_FILES
        qml/AppManagerView.qml
        qml/MarketplaceView.qml
        qml/InstalledAppsView.qml
        qml/AppCard.qml
        qml/InstalledAppCard.qml
)

# Example application using the framework
add_executable(dk_ivi
    main/main.cpp
)

target_link_libraries(dk_ivi PUBLIC
    Qt6::Core
    Qt6::Quick
    AppManagerFramework
)



# Platform-specific plugins

# Kubernetes plugin
# add_library(KubernetesPlugin SHARED
#     plugins/kubernetes/kubernetesplugin.h
#     plugins/kubernetes/kubernetesplugin.cpp
#     plugins/kubernetes/k8sinstaller.h
#     plugins/kubernetes/k8sinstaller.cpp
#     plugins/kubernetes/k8sruntime.h
#     plugins/kubernetes/k8sruntime.cpp
# )

# target_link_libraries(KubernetesPlugin
#     Qt6::Core
#     AppManagerFramework
# )

# Docker plugin
# add_library(DockerPlugin SHARED
#     plugins/docker/dockerplugin.h
#     plugins/docker/dockerplugin.cpp
#     plugins/docker/dockerinstaller.h
#     plugins/docker/dockerinstaller.cpp
#     plugins/docker/dockerruntime.h
#     plugins/docker/dockerruntime.cpp
# )

# target_link_libraries(DockerPlugin
#     Qt6::Core
#     AppManagerFramework
# )

# Local filesystem plugin (for development/testing)
# add_library(LocalPlugin SHARED
#     plugins/local/localplugin.h
#     plugins/local/localplugin.cpp
#     plugins/local/localinstaller.h
#     plugins/local/localinstaller.cpp
#     plugins/local/localruntime.h
#     plugins/local/localruntime.cpp
# )

# target_link_libraries(LocalPlugin
#     Qt6::Core
#     AppManagerFramework
# )

# # Install targets
# install(TARGETS AppManagerFramework
#     EXPORT AppManagerFrameworkTargets
#     LIBRARY DESTINATION lib
#     ARCHIVE DESTINATION lib
#     RUNTIME DESTINATION bin
#     INCLUDES DESTINATION include
# )

install(DIRECTORY 
    DESTINATION include/AppManager
    FILES_MATCHING PATTERN "*.h"
)

# install(EXPORT AppManagerFrameworkTargets
#     FILE AppManagerFrameworkTargets.cmake
#     NAMESPACE AppManager::
#     DESTINATION lib/cmake/AppManagerFramework
# )

# Package configuration
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    AppManagerFrameworkConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    AppManagerFrameworkConfig.cmake.in
    AppManagerFrameworkConfig.cmake
    INSTALL_DESTINATION lib/cmake/AppManagerFramework
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/AppManagerFrameworkConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/AppManagerFrameworkConfigVersion.cmake
    DESTINATION lib/cmake/AppManagerFramework
)
