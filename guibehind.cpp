/* DUKTO - A simple, fast and multi-platform file transfer tool for LAN users
 * Copyright (C) 2011 Emanuele Colombo
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "guibehind.h"

#include "settings.h"
#include "miniwebserver.h"
#include "duktowindow.h"
#include "platform.h"
#include "updateschecker.h"
#include "systemtray.h"

#ifdef Q_OS_ANDROID
#include "androidutils.h"
#include <QMessageBox>
#endif

#include <QQmlContext>
#include <QTimer>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QClipboard>
#include <QApplication>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QScreen>

#if QT_VERSION >= QT_VERSION_CHECK(5, 10 ,0)
#include <QRandomGenerator>
#else
#include <QDateTime>
#endif

#define NETWORK_PORT 4644 // 6742

GuiBehind::GuiBehind(Settings *settings) :
    QObject(nullptr), mSettings(settings)
{
    // Status variables
    mCurrentTransferProgress = 0;
    mTextSnippetSending = false;
    mShowUpdateBanner = false;

    // Clipboard object
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &GuiBehind::clipboardChanged);
    clipboardChanged();

    // Add "Me" entry
    mBuddiesList.addMeElement();

    // Add "Ip" entry
    mBuddiesList.addIpElement();

    // Mini web server
    mMiniWebServer = new MiniWebServer(NETWORK_PORT + 1);

    // Destination buddy
    mDestBuddy = new DestinationBuddy(this);

    // Set destination folder
    mDuktoProtocol.setDestDir(mSettings->destPath());

    // Set current theme color
    mTheme.setThemeColor(mSettings->themeColor());

    // Register protocol signals
    connect(&mDuktoProtocol, &DuktoProtocol::peerListAdded, this, &GuiBehind::peerListAdded);
    connect(&mDuktoProtocol, &DuktoProtocol::peerListRemoved, this, &GuiBehind::peerListRemoved);
    connect(&mDuktoProtocol, &DuktoProtocol::transferStatusUpdate, this, &GuiBehind::transferStatusUpdate);
    connect(&mDuktoProtocol, &DuktoProtocol::receiveStarted, this, &GuiBehind::receiveFileStart);
    connect(&mDuktoProtocol, &DuktoProtocol::receiveAborted, this, &GuiBehind::receiveFileCancelled);
    connect(&mDuktoProtocol, &DuktoProtocol::receiveCompleted, this, &GuiBehind::receiveComplete);
    connect(&mDuktoProtocol, &DuktoProtocol::receiveFileCompleted, this, &GuiBehind::receiveFileComplete);
    connect(&mDuktoProtocol, &DuktoProtocol::receiveDirCompleted, this, &GuiBehind::receiveDirComplete);
    connect(&mDuktoProtocol, &DuktoProtocol::receiveTextCompleted, this, &GuiBehind::receiveTextComplete);
    connect(&mDuktoProtocol, &DuktoProtocol::sendFileComplete, this, &GuiBehind::sendFileComplete);
    connect(&mDuktoProtocol, &DuktoProtocol::sendFileError, this, &GuiBehind::sendFileError);
    connect(&mDuktoProtocol, &DuktoProtocol::sendFileAborted, this, &GuiBehind::sendFileAborted);

    // Register other signals
    connect(this, &GuiBehind::remoteDestinationAddressChanged, this, &GuiBehind::remoteDestinationAddressHandler);

    // Periodic "hello" timer
    mPeriodicHelloTimer = new QTimer(this);
    connect(mPeriodicHelloTimer, &QTimer::timeout, this, &GuiBehind::discoveryNeighbors);

    // Setup protocol
    initialize();

    // Start random rotate
    mShowBackTimer = new QTimer(this);
    connect(mShowBackTimer, &QTimer::timeout, this, &GuiBehind::showRandomBack);
#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
    qsrand(QDateTime::currentDateTimeUtc().toTime_t());
#endif
    mShowBackTimer->start(10000);

#ifdef UPDATER
    // Enqueue check for updates
    mUpdatesChecker = new UpdatesChecker();
    connect(mUpdatesChecker, &UpdatesChecker::updatesAvailable, this, &GuiBehind::showUpdatesMessage);
    QTimer::singleShot(2000, [this]()->void {
        mUpdatesChecker->start();
    });
#endif
}

GuiBehind::~GuiBehind()
{
#ifdef UPDATER
    if (mUpdatesChecker) mUpdatesChecker->deleteLater();
#endif
    if (mMiniWebServer) mMiniWebServer->deleteLater();
    if (mShowBackTimer) mShowBackTimer->deleteLater();
    if (mPeriodicHelloTimer) mPeriodicHelloTimer->deleteLater();
    if (mDestBuddy) mDestBuddy->deleteLater();
}

void GuiBehind::setViewer(DuktoWindow *view, SystemTray *tray) {
    mView = view;

    // Init buddy list
    view->rootContext()->setContextProperty("buddiesListData", &mBuddiesList);
    view->rootContext()->setContextProperty("recentListData", &mRecentList);
    view->rootContext()->setContextProperty("ipAddressesData", &mIpAddresses);
    view->rootContext()->setContextProperty("guiBehind", this);
    view->rootContext()->setContextProperty("destinationBuddy", mDestBuddy);
    view->rootContext()->setContextProperty("theme", &mTheme);

    // Load GUI
    view->setSource(QUrl("qrc:/qml/dukto/Dukto.qml"));
    view->restoreGeometry(mSettings->windowGeometry());

    connect(&mDuktoProtocol, &DuktoProtocol::receiveTextCompleted, tray, &SystemTray::received_text);
    connect(&mDuktoProtocol, &DuktoProtocol::receiveFileCompleted, tray, &SystemTray::received_file);
    connect(&mDuktoProtocol, &DuktoProtocol::receiveDirCompleted, tray, &SystemTray::received_folder);
}

// Add the new buddy to the buddy list
void GuiBehind::peerListAdded(const Peer &peer) {
    mBuddiesList.addBuddy(peer);
}

// Remove the buddy from the buddy list
void GuiBehind::peerListRemoved(const Peer &peer) {

    // Check if currently is shown the "send" page for that buddy
    if (((overlayState() == "send")
         || ((overlayState() == "showtext") && textSnippetSending()))
            && (mDestBuddy->ip() == peer.address.toString()))
        emit hideAllOverlays();

    // Check if currently is shown the "transfer complete" message box
    // for the removed user as destination
    if ((overlayState() == "message") && (messagePageBackState() == "send")
            && (mDestBuddy->ip() == peer.address.toString()))
        setMessagePageBackState("");

    // Remove from the list
    mBuddiesList.removeBuddy(peer.address.toString());
}

void GuiBehind::showRandomBack()
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    // Look for a random element
    int i = QRandomGenerator::global()->bounded(mBuddiesList.rowCount());

    // Show back
    mBuddiesList.showSingleBack(i);
#else
    // Look for a random element
    int i = (qrand() * 1.0 / RAND_MAX) * (mBuddiesList.rowCount() + 1);

    // Show back
    if (i < mBuddiesList.rowCount()) mBuddiesList.showSingleBack(i);
#endif
}

void GuiBehind::clipboardChanged()
{
    mClipboardTextAvailable = !(QApplication::clipboard()->text().isEmpty());
    emit clipboardTextAvailableChanged();
}

void GuiBehind::receiveFileStart(const QString &senderIp)
{
    // Look for the sender in the buddy list
    QString sender = mBuddiesList.buddyNameByIp(senderIp);
    if (sender.isEmpty())
        setCurrentTransferBuddy("remote sender");
    else
        setCurrentTransferBuddy(sender);

    // Update user interface
    setCurrentTransferSending(false);
#ifdef Q_OS_WIN
    mView->showTaskbarProgress(0);
#endif

    emit transferStart();
}

void GuiBehind::transferStatusUpdate(qint64 total, qint64 partial)
{
    // Stats formatting
    if (total < 1024)
        setCurrentTransferStats(QString::number(partial) + " B of " + QString::number(total) + " B");
    else if (total < 1048576)
        setCurrentTransferStats(QString::number(partial * 1.0 / 1024, 'f', 1) + " KB of " + QString::number(total * 1.0 / 1024, 'f', 1) + " KB");
    else
        setCurrentTransferStats(QString::number(partial * 1.0 / 1048576, 'f', 1) + " MB of " + QString::number(total * 1.0 / 1048576, 'f', 1) + " MB");

    double percent = partial * 1.0 / total * 100;
    setCurrentTransferProgress(percent);

#ifdef Q_OS_WIN
    mView->showTaskbarProgress(percent);
#endif
}

void GuiBehind::receiveFileComplete(const QString &name, const QString &path, qint64 size) {
    // Add an entry to recent activities
    mRecentList.addRecent(name, path, "file", mCurrentTransferBuddy, size);
}

void GuiBehind::receiveDirComplete(const QString &name, const QString &path) {
    // Add an entry to recent activities
    mRecentList.addRecent(name, path, "dir", mCurrentTransferBuddy, -1);
}

void GuiBehind::receiveTextComplete(const QString &text) {
    // Add an entry to recent activities
    mRecentList.addRecent("Text snippet", text, "text", mCurrentTransferBuddy, text.size());
}

void GuiBehind::receiveComplete() {
    // Update GUI
#ifdef Q_OS_WIN
    mView->hideTaskbarProgress();
#endif
    QApplication::alert(mView, 5000);
    emit receiveCompleted();
}

void GuiBehind::showTextSnippet(const QString &text, const QString &sender)
{
    setTextSnippet(text);
    setTextSnippetBuddy(sender);
    setTextSnippetSending(false);
    emit gotoTextSnippet();
}

void GuiBehind::openFile(const QString &path)
{
#ifdef Q_OS_ANDROID
    QJniObject uri = AndroidStorage::parseUri(path);
    AndroidStorage::grantUriPermission(uri);
    QDesktopServices::openUrl(QUrl(path));
    AndroidStorage::revokeUriPermission(uri);
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
#endif
}

void GuiBehind::openDestinationFolder()
{
    QDesktopServices::openUrl(mSettings->destPath());
}

void GuiBehind::changeDestinationFolder()
{
    // Show system dialog for folder selection
    QString dirname;
#ifdef Q_OS_ANDROID
    // Qt use QUrl::toString(QUrl::PrettyDecoded) to convert QUrl to QString in
    // QFileDialog::getExistingDirectory's internal implementation. Some characters
    // like spaces will be wrongly decoded
    QUrl url = QFileDialog::getExistingDirectoryUrl(mView, "Change folder", QUrl(mSettings->destPath()), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (url.isEmpty()) {
        return;
    }
    dirname = url.toString(QUrl::FullyEncoded);
#else
    dirname = QFileDialog::getExistingDirectory(mView, "Change folder", mSettings->destPath(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dirname.isEmpty()) {
        return;
    }
#endif

    // Set the new folder as destination
    mDuktoProtocol.setDestDir(dirname);

    // Save the new setting
    setDestPath(dirname);
}

void GuiBehind::refreshIpList()
{
    mIpAddresses.refreshIpList();
}

void GuiBehind::showSendPage(const QString &ip)
{
    // Check for a buddy with the provided IP address
    QStandardItem *buddy = mBuddiesList.buddyByIp(ip);
    if (buddy == nullptr) return;

    // Update exposed data for the selected user
    mDestBuddy->fillFromItem(buddy);

    // Preventive update of destination buddy
    if (mDestBuddy->ip() == "IP")
        setCurrentTransferBuddy(remoteDestinationAddress());
    else
        setCurrentTransferBuddy(mDestBuddy->username());

    // Preventive update of text send page
    setTextSnippetBuddy(mDestBuddy->username());
    setTextSnippetSending(true);
    setTextSnippet("");

    // Show send UI
    emit gotoSendPage();
}

void GuiBehind::sendDroppedFiles(QStringList *files)
{
    if(files->isEmpty()) return;

    // Check if there's no selected buddy
    // (but there must be only one buddy in the buddy list)
    if (overlayState().isEmpty())
    {
        if (mBuddiesList.rowCount() != 3) return;
        showSendPage(mBuddiesList.fistBuddyIp());
    }

    // Send files
    QStringList toSend = *files;
    startTransfer(toSend);
}


void GuiBehind::sendSomeFiles()
{
    // Show file selection dialog
    QStringList files;
#ifdef Q_OS_ANDROID
    // Qt use QUrl::toString(QUrl::PrettyDecoded) to convert QUrl to QString in
    // QFileDialog::getOpenFileNames' internal implementation. Some characters
    // like spaces will be wrongly decoded
    const QList<QUrl> urls = QFileDialog::getOpenFileUrls(mView, "Send some files");
    if (urls.isEmpty()) {
        return;
    }
    for (const QUrl &url: urls) {
        files << url.toString(QUrl::FullyEncoded);
    }
#else
    files = QFileDialog::getOpenFileNames(mView, "Send some files");
    if (files.isEmpty()) {
        return;
    }
#endif
    // Send files
    startTransfer(files);
}

void GuiBehind::sendFolder()
{
    // Show folder selection dialog
    QString dirname;
#ifdef Q_OS_ANDROID
    // Qt use QUrl::toString(QUrl::PrettyDecoded) to convert QUrl to QString in
    // QFileDialog::getExistingDirectory's internal implementation. Some characters
    // like spaces will be wrongly decoded
    QUrl url = QFileDialog::getExistingDirectoryUrl(mView, "Send a folder", QUrl(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (url.isEmpty()) {
        return;
    }
    dirname = url.toString(QUrl::FullyEncoded);
#else
    dirname = QFileDialog::getExistingDirectory(mView, "Send a folder", QString(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dirname.isEmpty()) {
        return;
    }
#endif

    // Send files
    QStringList toSend;
    toSend.append(dirname);
    startTransfer(toSend);
}

void GuiBehind::sendClipboardText()
{
    // Get text to send
    QString text = QApplication::clipboard()->text();
    if (text.isEmpty()) return;

    // Send text
    startTransfer(text);
}

void GuiBehind::sendText()
{
    // Get text to send
    QString text = textSnippet();
    if (text.isEmpty()) return;

    // Send text
    startTransfer(text);
}

void GuiBehind::sendScreen()
{
    // Minimize window
    mView->setWindowState(Qt::WindowMinimized);

    QTimer::singleShot(500, this, &GuiBehind::sendScreenStage2);
}

void GuiBehind::sendScreenStage2() {

    // Screenshot
    QPixmap screen = qApp->primaryScreen()->grabWindow(0);

    // Restore window
    mView->setWindowState(Qt::WindowActive);

    // Salvataggio screenshot in file
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(false);
    tempFile.open();
    mScreenTempPath = tempFile.fileName();
    tempFile.close();
    screen.save(mScreenTempPath, "JPG", 95);

    // Prepare file transfer
    QString ip;
    qint16 port;
    if (!prepareStartTransfer(&ip, &port)) return;

    // Start screen transfer
    mDuktoProtocol.sendScreen(ip, port, mScreenTempPath);
}

void GuiBehind::startTransfer(const QStringList &files)
{
    // Prepare file transfer
    QString ip;
    qint16 port;
    if (!prepareStartTransfer(&ip, &port)) return;

    // Start files transfer
    mDuktoProtocol.sendFile(ip, port, files);
}

void GuiBehind::startTransfer(const QString &text)
{
    // Prepare file transfer
    QString ip;
    qint16 port;
    if (!prepareStartTransfer(&ip, &port)) return;

    // Start files transfer
    mDuktoProtocol.sendText(ip, port, text);
}

bool GuiBehind::prepareStartTransfer(QString *ip, qint16 *port)
{
    // Check if it's a remote file transfer
    if (mDestBuddy->ip() == "IP") {

        // Remote transfer
        QString dest = remoteDestinationAddress();

        // Check if port is specified
        if (dest.contains(":")) {

            // Port is specified or destination is malformed...
            static const QRegularExpression rx("^(.*):([0-9]+)$");
            QRegularExpressionMatch match = rx.match(dest);
            if (match.hasMatch() == false) {

                // Malformed destination
                setMessagePageTitle("Send");
                setMessagePageText("Hey, take a look at your destination, it appears to be malformed!");
                setMessagePageBackState("send");
                emit gotoMessagePage();
                return false;
            }

            // Get IP (or hostname) and port
            *ip = match.captured(1);
            *port = match.captured(2).toInt();
        }
        else {

            // Port not specified, using default
            *ip = dest;
            *port = 0;
        }
        setCurrentTransferBuddy(*ip);
    }
    else {

        // Local transfer
        *ip = mDestBuddy->ip();
        *port = mDestBuddy->port();
        setCurrentTransferBuddy(mDestBuddy->username());
    }

    // Update GUI for file transfer
    setCurrentTransferSending(true);
    setCurrentTransferStats("Connecting...");
    setCurrentTransferProgress(0);
#ifdef Q_OS_WIN
    mView->showTaskbarProgress(0);
#endif

    emit transferStart();
    return true;
}

void GuiBehind::sendFileComplete()
{
    // Show completed message
    setMessagePageTitle("Send");
#ifdef MOBILE_APP
    setMessagePageText("Your data has been sent to your buddy!");
#else
    setMessagePageText("Your data has been sent to your buddy!\n\nDo you want to send other files to your buddy? Just drag and drop them here!");
#endif
    setMessagePageBackState("send");

#ifdef Q_OS_WIN
    mView->hideTaskbarProgress();
#endif

    // Check for temporary file to delete
    if (!mScreenTempPath.isEmpty()) {

        QFile(mScreenTempPath).remove();
        mScreenTempPath.clear();
    }

    emit gotoMessagePage();
}

void GuiBehind::remoteDestinationAddressHandler()
{
    // Update GUI status
    setCurrentTransferBuddy(remoteDestinationAddress());
    setTextSnippetBuddy(remoteDestinationAddress());
}

// Returns true if the application is ready to accept
// drag and drop for files to send
bool GuiBehind::canAcceptDrop()
{
    QString state = overlayState();
    if (state == "termspage" || state == "initerr") {
        return false;
    }
    // There must be the send page shown and,
    // if it's a remote destination, it must have an IP
    if (state == "send")
        return !((mDestBuddy->ip() == "IP") && (remoteDestinationAddress().isEmpty()));

    // Or there could be a "send complete" or "send error" message relative to a
    // determinate buddy
    else if ((state == "message") && (messagePageBackState() == "send"))
        return true;

    // Or there could be just one buddy in the list
    else if (mBuddiesList.rowCount() == 3)
        return true;

    return false;
}

// Handles send error
void GuiBehind::sendFileError(const QString &error)
{
    setMessagePageTitle("Error");
    setMessagePageText("Sorry, an error has occurred while sending your data...\n\n" + error);
    setMessagePageBackState("send");
#ifdef Q_OS_WIN
    mView->stopTaskbarProgress();
#endif

    // Check for temporary file to delete
    if (!mScreenTempPath.isEmpty()) {

        QFile file(mScreenTempPath);
        file.remove();
        mScreenTempPath.clear();
    }

    emit gotoMessagePage();
}

// Handles receive error
void GuiBehind::receiveFileCancelled(const QString &error)
{
    if (error.isEmpty() == false) {
        setMessagePageTitle("Error");
        setMessagePageText("An error has occurred during the transfer... The data you received could be incomplete or broken.\n\n" + error);
        setMessagePageBackState("");
        emit gotoMessagePage();
    } else {
        // no reason, cancelled by user
        emit receiveCompleted();
    }
#ifdef Q_OS_WIN
    mView->stopTaskbarProgress();
#endif
}

// Event handler to catch the "application activate" event
bool GuiBehind::eventFilter(QObject *, QEvent *event)
{
    // On application activatio, I send a broadcast hello
    if (event->type() == QEvent::ApplicationActivate)
        discoveryNeighbors();

    return false;
}

// Changes the current theme color
void GuiBehind::changeThemeColor(const QString &color)
{
    mTheme.setThemeColor(color);
    mSettings->saveThemeColor(color);
}

// Called on application closing event
void GuiBehind::close()
{
    mDuktoProtocol.closeServers();
}

// Reset taskbar progress status
void GuiBehind::resetProgressStatus()
{
#ifdef Q_OS_WIN
    mView->hideTaskbarProgress();
#endif
}

// Broadcast hello
void GuiBehind::discoveryNeighbors()
{
    mDuktoProtocol.greeting();
}

// Show updates message
void GuiBehind::showUpdatesMessage()
{
    setShowUpdateBanner(true);
}

// Abort current transfer while sending data
void GuiBehind::abortTransfer()
{
    mDuktoProtocol.abortCurrentTransfer();
}

// Protocol confirms that abort has been done
void GuiBehind::sendFileAborted()
{
    resetProgressStatus();
    emit gotoSendPage();
}

// ------------------------------------------------------------
// Property setter and getter

QString GuiBehind::currentTransferBuddy()
{
    return mCurrentTransferBuddy;
}

void GuiBehind::setCurrentTransferBuddy(const QString &buddy)
{
    if (buddy == mCurrentTransferBuddy) return;
    mCurrentTransferBuddy = buddy;
    emit currentTransferBuddyChanged();
}

int GuiBehind::currentTransferProgress()
{
    return mCurrentTransferProgress;
}

void GuiBehind::setCurrentTransferProgress(int value)
{
    if (value == mCurrentTransferProgress) return;
    mCurrentTransferProgress = value;
    emit currentTransferProgressChanged();
}

QString GuiBehind::currentTransferStats()
{
    return mCurrentTransferStats;
}

void GuiBehind::setCurrentTransferStats(const QString &stats)
{
    if (stats == mCurrentTransferStats) return;
    mCurrentTransferStats = stats;
    emit currentTransferStatsChanged();
}

QString GuiBehind::textSnippetBuddy()
{
    return mTextSnippetBuddy;
}

void GuiBehind::setTextSnippetBuddy(const QString &buddy)
{
    if (buddy == mTextSnippetBuddy) return;
    mTextSnippetBuddy = buddy;
    emit textSnippetBuddyChanged();
}

QString GuiBehind::textSnippet()
{
    return mTextSnippet;
}

void GuiBehind::setTextSnippet(const QString &text)
{
    if (text == mTextSnippet) return;
    mTextSnippet = text;
    emit textSnippetChanged();
}

bool GuiBehind::textSnippetSending()
{
    return mTextSnippetSending;
}

void GuiBehind::setTextSnippetSending(bool sending)
{
    if (sending == mTextSnippetSending) return;
    mTextSnippetSending = sending;
    emit textSnippetSendingChanged();
}

QString GuiBehind::destPath()
{
    return mSettings->destPath();
}

void GuiBehind::setDestPath(const QString &path)
{
    mSettings->saveDestPath(path);
    emit destPathChanged();
}

bool GuiBehind::currentTransferSending()
{
    return mCurrentTransferSending;
}

void GuiBehind::setCurrentTransferSending(bool sending)
{
    if (sending == mCurrentTransferSending) return;
    mCurrentTransferSending = sending;
    emit currentTransferSendingChanged();
}

bool GuiBehind::clipboardTextAvailable()
{
    return mClipboardTextAvailable;
}

QString GuiBehind::remoteDestinationAddress()
{
    return mRemoteDestinationAddress;
}

void GuiBehind::setRemoteDestinationAddress(const QString &address)
{
    if (address == mRemoteDestinationAddress) return;
    mRemoteDestinationAddress = address;
    emit remoteDestinationAddressChanged();
}

QString GuiBehind::overlayState()
{
    return mOverlayState;
}

void GuiBehind::setOverlayState(const QString &state)
{
    if (state == mOverlayState) return;
    mOverlayState = state;
    emit overlayStateChanged();
}

QString GuiBehind::messagePageText()
{
    return mMessagePageText;
}

void GuiBehind::setMessagePageText(const QString &message)
{
    if (message == mMessagePageText) return;
    mMessagePageText = message;
    emit messagePageTextChanged();
}

QString GuiBehind::messagePageTitle()
{
    return mMessagePageTitle;
}

void GuiBehind::setMessagePageTitle(const QString &title)
{
    if (title == mMessagePageTitle) return;
    mMessagePageTitle = title;
    emit messagePageTitleChanged();
}

QString GuiBehind::messagePageBackState()
{
    return mMessagePageBackState;
}

void GuiBehind::setMessagePageBackState(const QString &state)
{
    if (state == mMessagePageBackState) return;
    mMessagePageBackState = state;
    emit messagePageBackStateChanged();
}

bool GuiBehind::showTermsOnStart()
{
    return mSettings->showTermsOnStart();
}

void GuiBehind::setShowTermsOnStart(bool show)
{
    mSettings->saveShowTermsOnStart(show);
    emit showTermsOnStartChanged();
}

bool GuiBehind::showUpdateBanner()
{
    return mShowUpdateBanner;
}

void GuiBehind::setShowUpdateBanner(bool show)
{
    mShowUpdateBanner = show;
    emit showUpdateBannerChanged();
}

void GuiBehind::setBuddyName(const QString &name)
{
    mSettings->saveBuddyName(QString(name).replace(' ', ""));
    mDuktoProtocol.updateBuddyName();
    mBuddiesList.updateMeElement();
    emit buddyNameChanged();
}

QString GuiBehind::buddyName()
{
    return mSettings->buddyName();
}

void GuiBehind::setShowNotification(bool show) {
    mSettings->saveNotificationEnabled(show);
    emit showNotificationChanged();
}

bool GuiBehind::showNotification() {
    return mSettings->notificationEnabled();
}

void GuiBehind::setCloseToTray(bool enabled) {
    mSettings->saveCloseToTrayEnabled(enabled);
    emit closeToTrayChanged();
}

bool GuiBehind::closeToTray() {
    return mSettings->closeToTrayEnabled();
}

void GuiBehind::setInitError(const QString &error, const QString &action) {
    if (error != mInitError) {
        mInitError = error;
        emit initErrorChanged();
    }
    if (action != mInitErrorAction) {
        mInitErrorAction = action;
        emit initErrorActionChanged();
    }
}

QString GuiBehind::initError() {
    return mInitError;
}

QString GuiBehind::initErrorAction() {
    return mInitErrorAction;
}

bool GuiBehind::isDesktopApp() {
#ifdef MOBILE_APP
    return false;
#else
    return true;
#endif
}

void GuiBehind::initialize() {
#ifdef Q_OS_ANDROID
    if (mSettings->destPath().isEmpty()) {
        setInitError(QStringLiteral("The directory for received files hasn't been specified."), QStringLiteral("Choose a directory"));
        return;
    }
#endif
    if (mDuktoProtocol.setupUdpServer(NETWORK_PORT) == false) {
        mDuktoProtocol.closeServers();
        setInitError(QStringLiteral("The UDP port %1 has been used by another application. Please quit that application and try again.").arg(QString::number(NETWORK_PORT)));
        return;
    }
    if (mDuktoProtocol.setupTcpServer(NETWORK_PORT) == false) {
        mDuktoProtocol.closeServers();
        setInitError(QStringLiteral("The TCP port %1 has been used by another application. Please quit that application and try again.").arg(QString::number(NETWORK_PORT)));
        return;
    }
    setInitError(QString(""));
    // Say "hello"
    discoveryNeighbors();
    mPeriodicHelloTimer->start(60000);
}

void GuiBehind::reinitialize(const QString &action) {
    if (action == QStringLiteral("Choose a directory")) {
        changeDestinationFolder();
    }
    initialize();
}


void GuiBehind::refreshNeighbors() {
    mBuddiesList.clearBuddies();
    discoveryNeighbors();
}

void GuiBehind::pasteDestinationIp() {
    QString text = QApplication::clipboard()->text();
    if (text.isEmpty() == false) {
        setRemoteDestinationAddress(text);
    }
}
