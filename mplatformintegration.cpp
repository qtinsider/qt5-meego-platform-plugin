/*
 * Copyright (C) 2020 Chukwudi Nwutobo <nwutobo@outlook.com>
 * Copyright (C) 2018 The Qt Company Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mplatformintegration.h"
#include "xcbconnection.h"
#include "mplatformscreen.h"
#include "mplatformwindow.h"
#include "mplatformbackingstore.h"
#include "mplatformnativeinterface.h"
#include "mplatformclipboard.h"
#include "xcbeventqueue.h"
#include "meventdispatcher.h"

#include "mplatformsessionmanager.h"

#include <xcb/xcb.h>

#include <QtFontDatabaseSupport/private/qgenericunixfontdatabase_p.h>
#include <QtServiceSupport/private/qgenericunixservices_p.h>

#include <stdio.h>

#include <QtGui/private/qguiapplication_p.h>

#define register        /* C++17 deprecated register */
#include <X11/Xlib.h>
#undef register

#include <qpa/qplatforminputcontextfactory_p.h>
#include <private/qgenericunixthemes_p.h>
#include <qpa/qplatforminputcontext.h>

#include <QtGui/QOpenGLContext>
#include <QtGui/QScreen>
#include <QtGui/QOffscreenSurface>

#include <QtCore/QFileInfo>

// Find out if our parent process is gdb by looking at the 'exe' symlink under /proc,.
// or, for older Linuxes, read out 'cmdline'.
static bool runningUnderDebugger()
{
#if defined(QT_DEBUG) && defined(Q_OS_LINUX)
    const QString parentProc = QLatin1String("/proc/") + QString::number(getppid());
    const QFileInfo parentProcExe(parentProc + QLatin1String("/exe"));
    if (parentProcExe.isSymLink())
        return parentProcExe.symLinkTarget().endsWith(QLatin1String("/gdb"));
    QFile f(parentProc + QLatin1String("/cmdline"));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QByteArray s;
    char c;
    while (f.getChar(&c) && c) {
        if (c == '/')
            s.clear();
        else
            s += c;
    }
    return s == "gdb";
#else
    return false;
#endif
}

MPlatformIntegration *MPlatformIntegration::m_instance = nullptr;

MPlatformIntegration::MPlatformIntegration(const QStringList &parameters, int &argc, char **argv)
    : m_services(new QGenericUnixServices)
    , m_instanceName(nullptr)
    , m_canGrab(true)
    , m_defaultVisualId(UINT_MAX)
{
    m_instance = this;
    qApp->setAttribute(Qt::AA_CompressHighFrequencyEvents, true);

    QWindowSystemInterface::setPlatformFiltersEvents(true);

    qRegisterMetaType<MPlatformWindow*>();
    XInitThreads();
    m_nativeInterface.reset(new MPlatformNativeInterface);

    // Parse arguments
    const char *displayName = nullptr;
    bool noGrabArg = false;
    bool doGrabArg = false;
    if (argc) {
        int j = 1;
        for (int i = 1; i < argc; i++) {
            QByteArray arg(argv[i]);
            if (arg.startsWith("--"))
                arg.remove(0, 1);
            if (arg == "-display" && i < argc - 1)
                displayName = argv[++i];
            else if (arg == "-name" && i < argc - 1)
                m_instanceName = argv[++i];
            else if (arg == "-nograb")
                noGrabArg = true;
            else if (arg == "-dograb")
                doGrabArg = true;
            else if (arg == "-visual" && i < argc - 1) {
                bool ok = false;
                m_defaultVisualId = QByteArray(argv[++i]).toUInt(&ok, 0);
                if (!ok)
                    m_defaultVisualId = UINT_MAX;
            }
            else
                argv[j++] = argv[i];
        }
        argc = j;
    } // argc

    bool underDebugger = runningUnderDebugger();
    if (noGrabArg && doGrabArg && underDebugger) {
        qWarning("Both -nograb and -dograb command line arguments specified. Please pick one. -nograb takes prcedence");
        doGrabArg = false;
    }

#if defined(QT_DEBUG)
    if (!noGrabArg && !doGrabArg && underDebugger) {
        qCDebug(lcQpaMeego, "Qt: gdb: -nograb added to command-line options.\n"
                "\t Use the -dograb option to enforce grabbing.");
    }
#endif
    m_canGrab = (!underDebugger && !noGrabArg) || (underDebugger && doGrabArg);

    static bool canNotGrabEnv = qEnvironmentVariableIsSet("QT_XCB_NO_GRAB_SERVER");
    if (canNotGrabEnv)
        m_canGrab = false;

    const int numParameters = parameters.size();
    m_connections.reserve(1 + numParameters / 2);

    auto conn = new XcbConnection(m_nativeInterface.data(), m_canGrab, m_defaultVisualId, displayName);
    if (!conn->isConnected()) {
        delete conn;
        return;
    }
    m_connections << conn;

    // ### Qt 6 (QTBUG-52408) remove this multi-connection code path
    for (int i = 0; i < numParameters - 1; i += 2) {
        qCDebug(lcQpaXcb) << "connecting to additional display: " << parameters.at(i) << parameters.at(i+1);
        QString display = parameters.at(i) + QLatin1Char(':') + parameters.at(i+1);
        conn = new XcbConnection(m_nativeInterface.data(), m_canGrab, m_defaultVisualId, display.toLatin1().constData());
        if (conn->isConnected())
            m_connections << conn;
        else
            delete conn;
    }

    m_fontDatabase.reset(new QGenericUnixFontDatabase());
}

MPlatformIntegration::~MPlatformIntegration()
{
    qDeleteAll(m_connections);
    m_instance = nullptr;
}

QPlatformPixmap *MPlatformIntegration::createPlatformPixmap(QPlatformPixmap::PixelType type) const
{
    return QPlatformIntegration::createPlatformPixmap(type);
}

QPlatformWindow *MPlatformIntegration::createPlatformWindow(QWindow *window) const
{
    MPlatformWindow *xcbWindow = new MPlatformWindow(window);
    xcbWindow->create();
    return xcbWindow;
}

QPlatformOpenGLContext *MPlatformIntegration::createPlatformOpenGLContext(QOpenGLContext *context) const
{
    return nullptr;
}

QPlatformBackingStore *MPlatformIntegration::createPlatformBackingStore(QWindow *window) const
{
    return new MPlatformBackingStore(window);
}

QPlatformOffscreenSurface *MPlatformIntegration::createPlatformOffscreenSurface(QOffscreenSurface *surface) const
{
    return nullptr;
}

bool MPlatformIntegration::hasCapability(QPlatformIntegration::Capability cap) const
{
    switch (cap) {
    case OpenGL:
    case ThreadedOpenGL:
    case RasterGLSurface:
        return false;
    case ThreadedPixmaps:
    case WindowMasks:
    case MultipleWindows:
    case SyncState:
    case SwitchableWidgetComposition:
        return true;

    default: return QPlatformIntegration::hasCapability(cap);
    }
}

QAbstractEventDispatcher *MPlatformIntegration::createEventDispatcher() const
{
    return MEventDispatcher::createEventDispatcher(defaultConnection());
}

void MPlatformIntegration::initialize()
{
    const QLatin1String defaultInputContext("compose");
    // Perform everything that may potentially need the event dispatcher (timers, socket
    // notifiers) here instead of the constructor.
    QString icStr = QPlatformInputContextFactory::requested();
    if (icStr.isNull())
        icStr = defaultInputContext;
    m_inputContext.reset(QPlatformInputContextFactory::create(icStr));
    if (!m_inputContext && icStr != defaultInputContext && icStr != QLatin1String("none"))
        m_inputContext.reset(QPlatformInputContextFactory::create(defaultInputContext));

}

QPlatformFontDatabase *MPlatformIntegration::fontDatabase() const
{
    return m_fontDatabase.data();
}

QPlatformNativeInterface * MPlatformIntegration::nativeInterface() const
{
    return m_nativeInterface.data();
}

QPlatformClipboard *MPlatformIntegration::clipboard() const
{
    return m_connections.at(0)->clipboard();
}

QPlatformInputContext *MPlatformIntegration::inputContext() const
{
    return m_inputContext.data();
}

QPlatformServices *MPlatformIntegration::services() const
{
    return m_services.data();
}

QStringList MPlatformIntegration::themeNames() const
{
    return QGenericUnixTheme::themeNames();
}

QPlatformTheme *MPlatformIntegration::createPlatformTheme(const QString &name) const
{
    return QGenericUnixTheme::createUnixTheme(name);
}

QVariant MPlatformIntegration::styleHint(QPlatformIntegration::StyleHint hint) const
{
    switch (hint) {
    case QPlatformIntegration::CursorFlashTime:
    case QPlatformIntegration::KeyboardInputInterval:
    case QPlatformIntegration::MouseDoubleClickInterval:
    case QPlatformIntegration::StartDragTime:
    case QPlatformIntegration::KeyboardAutoRepeatRate:
    case QPlatformIntegration::PasswordMaskDelay:
    case QPlatformIntegration::StartDragVelocity:
    case QPlatformIntegration::UseRtlExtensions:
    case QPlatformIntegration::PasswordMaskCharacter:
        // TODO using various xcb, gnome or KDE settings
        break; // Not implemented, use defaults
    case QPlatformIntegration::StartDragDistance: {
        return false;
    }
    case QPlatformIntegration::ReplayMousePressOutsidePopup:
        return false;
    default:
        break;
    }
    return QPlatformIntegration::styleHint(hint);
}

static QString argv0BaseName()
{
    QString result;
    const QStringList arguments = QCoreApplication::arguments();
    if (!arguments.isEmpty() && !arguments.front().isEmpty()) {
        result = arguments.front();
        const int lastSlashPos = result.lastIndexOf(QLatin1Char('/'));
        if (lastSlashPos != -1)
            result.remove(0, lastSlashPos + 1);
    }
    return result;
}

static const char resourceNameVar[] = "RESOURCE_NAME";

QByteArray MPlatformIntegration::wmClass() const
{
    if (m_wmClass.isEmpty()) {
        // Instance name according to ICCCM 4.1.2.5
        QString name;
        if (m_instanceName)
            name = QString::fromLocal8Bit(m_instanceName);
        if (name.isEmpty() && qEnvironmentVariableIsSet(resourceNameVar))
            name = QString::fromLocal8Bit(qgetenv(resourceNameVar));
        if (name.isEmpty())
            name = argv0BaseName();

        // Note: QCoreApplication::applicationName() cannot be called from the QGuiApplication constructor,
        // hence this delayed initialization.
        QString className = QCoreApplication::applicationName();
        if (className.isEmpty()) {
            className = argv0BaseName();
            if (!className.isEmpty() && className.at(0).isLower())
                className[0] = className.at(0).toUpper();
        }

        if (!name.isEmpty() && !className.isEmpty())
            m_wmClass = std::move(name).toLocal8Bit() + '\0' + std::move(className).toLocal8Bit() + '\0';
    }
    return m_wmClass;
}

QPlatformSessionManager *MPlatformIntegration::createPlatformSessionManager(const QString &id, const QString &key) const
{
    return new MPlatformSessionManager(id, key);
}

void MPlatformIntegration::sync()
{
    for (int i = 0; i < m_connections.size(); i++) {
        m_connections.at(i)->sync();
    }
}

// For QApplication::beep()
void MPlatformIntegration::beep() const
{
    QScreen *priScreen = QGuiApplication::primaryScreen();
    if (!priScreen)
        return;
    QPlatformScreen *screen = priScreen->handle();
    if (!screen)
        return;
    xcb_connection_t *connection = static_cast<MPlatformScreen *>(screen)->xcb_connection();
    xcb_bell(connection, 0);
    xcb_flush(connection);
}
