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

#include "platform.h"

#include <QString>
#include <QHostInfo>
#include <QFile>
#include <QDir>
#include <QRegularExpression>

#if defined(Q_OS_MAC)
#include <QTemporaryFile>
#include <CoreServices/CoreServices.h>
#endif

#if defined(Q_OS_WIN)
#include <windows.h>
#include <lmaccess.h>
#endif

// Returns the system username
QString Platform::getSystemUsername()
{
    // Save in a static variable so that It's always ready
    static QString username;
    if (!username.isEmpty()) return username;

    // Looking for the username
    QString uname(getenv("USERNAME"));
    if (uname.isEmpty())
    {
        uname = getenv("USER");
        if (uname.isEmpty())
            uname = "Unknown";
    }
    username = uname.left(1).toUpper() + uname.mid(1);

    return username;
}

// Returns the hostname
QString Platform::getHostname()
{
    // Save in a static variable so that It's always ready
    static QString hostname;
    if (!hostname.isEmpty()) return hostname;

    // Get the hostname
    // (replace ".local" for MacOSX)
    hostname = QHostInfo::localHostName().replace(".local", "");

    return hostname;
}

// Returns the platform name
QString Platform::getPlatformName()
{
#if defined(Q_OS_WIN)
    return "Windows";
#elif defined(Q_OS_MAC)
    return "Macintosh";
#elif defined(Q_OS_LINUX)
    return "Linux";
#else
    return "Unknown";
#endif
}

// Returns the platform avatar path
QString Platform::getAvatarPath()
{
#if defined(Q_OS_WIN)
    QString username = getSystemUsername().replace("\\", "+");
    QString path = QString(getenv("LOCALAPPDATA")) + "\\Temp\\" + username + ".bmp";
    if (!QFile::exists(path))
        path = getWinTempAvatarPath();
    if (!QFile::exists(path))
        path = QString(getenv("PROGRAMDATA")) + "\\Microsoft\\User Account Pictures\\Guest.bmp";
    if (!QFile::exists(path))
        path = QString(getenv("ALLUSERSPROFILE")) + "\\" + QDir(getenv("APPDATA")).dirName() + "\\Microsoft\\User Account Pictures\\" + username + ".bmp";
    if (!QFile::exists(path))
        path = QString(getenv("ALLUSERSPROFILE")) + "\\" + QDir(getenv("APPDATA")).dirName() + "\\Microsoft\\User Account Pictures\\Guest.bmp";
    return path;
#elif defined(Q_OS_MAC)
    return getMacTempAvatarPath();
#elif defined(Q_OS_LINUX)
    return getLinuxAvatarPath();
#else
    return "";
#endif
}

// Returns the platform default output path
QString Platform::getDefaultPath()
{
    // For Windows it's the Desktop folder
#if defined(Q_OS_WIN)
    return QString(getenv("USERPROFILE")) + "\\Desktop";
#elif defined(Q_OS_MAC)
    return QString(getenv("HOME")) + "/Desktop";
#elif defined(Q_OS_UNIX)
    return QString(getenv("HOME"));
#else
    #error "Unknown default path for this platform"
#endif

}

#if defined(Q_OS_LINUX)
// Special function for Linux
QString Platform::getLinuxAvatarPath()
{
    QString path;

    // Gnome2 check
    path = QString(getenv("HOME")) + "/.face";
    if (QFile::exists(path)) return path;

    // Gnome3 check
    QFile f("/var/lib/AccountsService/users/" + QString(getenv("USER")));
    if (!f.open(QFile::ReadOnly)) return "";
    QTextStream ts(&f);
    QString line;
    bool found = false;
    while (true) {
        line = ts.readLine();
        if (line.isNull()) break;
        if (line.startsWith("Icon=")) {
            static const QRegularExpression re("^Icon=(.*)$");
            QRegularExpressionMatch match = re.match(line);
            if (match.hasMatch() == false) continue;
            path = match.captured(1);
            found = true;
            break;
        }
    }
    f.close();
    if (found && QFile::exists(path)) return path;

    // Not found
    return "";
}
#endif

#if defined(Q_OS_MAC)
static QTemporaryFile macAvatar;

// Special function for OSX
QString Platform::getMacTempAvatarPath()
{
    // Get image data from system
    QByteArray qdata;
    CSIdentityQueryRef query = CSIdentityQueryCreateForCurrentUser(kCFAllocatorSystemDefault);
    CFErrorRef error;
	if (CSIdentityQueryExecute(query, kCSIdentityQueryGenerateUpdateEvents, &error)) {
        CFArrayRef foundIds = CSIdentityQueryCopyResults(query);
        if (CFArrayGetCount(foundIds) == 1) {
            CSIdentityRef userId = (CSIdentityRef) CFArrayGetValueAtIndex(foundIds, 0);
            CFDataRef data = CSIdentityGetImageData(userId);
            qDebug() << CFDataGetLength(data);
            qdata.resize(CFDataGetLength(data));
            CFDataGetBytes(data, CFRangeMake(0, CFDataGetLength(data)), (uint8*)qdata.data());
        }
    }
    CFRelease(query);

    // Save it to a temporary file
    macAvatar.open();
    macAvatar.write(qdata);
    macAvatar.close();
    return macAvatar.fileName();
}
#endif

#if defined(Q_OS_WIN)

#include <objbase.h>

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) \
  ((sizeof(a) / sizeof(*(a))) / \
  static_cast<size_t>(!(sizeof(a) % sizeof(*(a)))))
#endif

typedef HRESULT (WINAPI*pfnSHGetUserPicturePathEx)(
    LPCWSTR pwszUserOrPicName,
    DWORD sguppFlags,
    LPCWSTR pwszDesiredSrcExt,
    LPWSTR pwszPicPath,
    UINT picPathLen,
    LPWSTR pwszSrcPath,
    UINT srcLen
);

// Special function for Windows 8
QString Platform::getWinTempAvatarPath()
{
    // Get file path
    CoInitialize(nullptr);
    HMODULE hMod = LoadLibrary(L"shell32.dll");
    pfnSHGetUserPicturePathEx picPathFn = (pfnSHGetUserPicturePathEx)GetProcAddress(hMod, (LPCSTR)810);
    WCHAR picPath[500] = {0}, srcPath[500] = {0};
    HRESULT ret = picPathFn(nullptr, 0, nullptr, picPath, ARRAYSIZE(picPath), srcPath, ARRAYSIZE(srcPath));
    FreeLibrary(hMod);
    CoUninitialize();
    if (ret != S_OK) return "C:\\missing.bmp";
    QString result = QString::fromWCharArray(picPath, -1);
    return result;
}

#endif
