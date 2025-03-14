project(dukto)
cmake_minimum_required(VERSION 3.1.0)

#FIXME: updater no longer works
#OPTION(USE_UPDATER "Add updater for application" OFF)
OPTION(USE_SINGLE_APP "Allow only one instance" OFF)
OPTION(USE_NOTIFY_LIBNOTIFY "Use libnotify for notifications (Linux only)" OFF)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(CMAKE_VERSION VERSION_LESS "3.7.0" AND CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(ANDROID 1)
endif()

if(USE_UPDATER)
    add_definitions("-DUPDATER")
endif()
if(USE_SINGLE_APP AND NOT ANDROID)
    add_definitions("-DSINGLE_APP")
endif()
if(USE_NOTIFY_LIBNOTIFY AND UNIX AND NOT APPLE AND NOT ANDROID)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBNOTIFY REQUIRED libnotify)
    add_definitions("-DNOTIFY_LIBNOTIFY")
    include_directories(${LIBNOTIFY_INCLUDE_DIRS})
    list(APPEND DUKTO_LIBS ${LIBNOTIFY_LIBRARIES})
endif()

if(NOT QT_DEFAULT_MAJOR_VERSION)
    find_package(Qt6 QUIET COMPONENTS Core)
    if (NOT Qt6_FOUND)
        find_package(Qt5 COMPONENTS Core REQUIRED)
        set(QT_DEFAULT_MAJOR_VERSION 5)
    else()
        set(QT_DEFAULT_MAJOR_VERSION 6)
    endif(NOT Qt6_FOUND)
endif(NOT QT_DEFAULT_MAJOR_VERSION)

set(QT_COMPONENTS Gui Network Qml QuickWidgets)
if(UNIX AND NOT APPLE AND NOT ANDROID)
    list(APPEND QT_COMPONENTS DBus)
endif()
set(QT_LIBRARIES "")
foreach(temp ${QT_COMPONENTS})
    list(APPEND QT_LIBRARIES "Qt${QT_DEFAULT_MAJOR_VERSION}::${temp}")
endforeach()

find_package(Qt${QT_DEFAULT_MAJOR_VERSION} COMPONENTS ${QT_COMPONENTS} REQUIRED)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_definitions("-DQT_NO_DEBUG_OUTPUT")
endif()

add_definitions("-DUNICODE")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(DUKTO_HDR
    buddylistitemmodel.h
    destinationbuddy.h
    duktoprotocol.h
    duktowindow.h
    guibehind.h
    ipaddressitemmodel.h
    miniwebserver.h
    network/buddymessage.h
    network/filedata.h
    network/messenger.h
    network/receiver.h
    network/sender.h
    peer.h
    platform.h
    recentlistitemmodel.h
    settings.h
    systemtray.h
    theme.h
)

set(DUKTO_SRC
    buddylistitemmodel.cpp
    destinationbuddy.cpp
    duktoprotocol.cpp
    duktowindow.cpp
    guibehind.cpp
    ipaddressitemmodel.cpp
    main.cpp
    miniwebserver.cpp
    network/buddymessage.cpp
    network/filedata.cpp
    network/messenger.cpp
    network/receiver.cpp
    network/sender.cpp
    platform.cpp
    recentlistitemmodel.cpp
    settings.cpp
    systemtray.cpp
    theme.cpp
)

set(DUKTO_RESOURCES
    qml.qrc
    qml/common/common.qrc
)
if(Qt6_FOUND OR Qt5Core_VERSION VERSION_GREATER "5.14.0" OR Qt5Core_VERSION VERSION_EQUAL "5.14.0")
    list(APPEND DUKTO_RESOURCES "qml/new/main.qrc")
else()
    list(APPEND DUKTO_RESOURCES "qml/old/main.qrc")
endif()

if(USE_UPDATER)
    list(APPEND DUKTO_SRC updateschecker.cpp)
    list(APPEND DUKTO_HDR updateschecker.h)
endif()

if(WIN32)
    list(APPEND DUKTO_SRC ecwin7.cpp)
    list(APPEND DUKTO_HDR ecwin7.h)
    list(APPEND DUKTO_RESOURCES dukto.rc)
endif()

if(USE_SINGLE_APP AND NOT ANDROID)
    set(QAPPLICATION_CLASS "QApplication")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/modules/SingleApplication)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/modules/SingleApplication)
endif()

add_executable(${PROJECT_NAME}
                ${DUKTO_HDR}
                ${DUKTO_SRC}
                ${DUKTO_RESOURCES})

if(USE_SINGLE_APP AND NOT ANDROID)
    add_dependencies(${PROJECT_NAME} SingleApplication)
    link_directories(${CMAKE_CURRENT_BINARY_DIR}/modules/SingleApplication)
    target_link_libraries(${PROJECT_NAME} SingleApplication)
endif()

target_link_libraries(${PROJECT_NAME} ${DUKTO_LIBS} ${QT_LIBRARIES})

if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE True)
    target_link_libraries(${PROJECT_NAME} Ws2_32 ole32 user32)
endif(WIN32)

If (APPLE)
    SET( MACOSX_BUNDLE_ICON_FILE dukto.icns )
    SET_SOURCE_FILES_PROPERTIES( "dukto.icns" PROPERTIES MACOSX_PACKAGE_LOCATION Resources )
    target_sources(${PROJECT_NAME} PRIVATE ${MACOSX_BUNDLE_ICON_FILE})
    set_property(TARGET ${PROJECT_NAME} PROPERTY MACOSX_BUNDLE 1)
    find_library(CORE_SERVICES CoreServices REQUIRED)
    target_link_options(${PROJECT_NAME} PRIVATE "${CORE_SERVICES}/CoreServices.tbd")
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID)
    install(TARGETS ${PROJECT_NAME}
            DESTINATION bin)
    install(FILES dukto.png
            DESTINATION share/pixmaps/)
    install(FILES dukto.desktop
            DESTINATION share/applications/)
endif()
