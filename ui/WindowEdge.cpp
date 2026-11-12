/**
 * @file WindowEdge.cpp
 * @brief 窗口最外沿的开关（见 WindowEdge.hpp）。
 */

#include "WindowEdge.hpp"

#include <QOperatingSystemVersion>
#include <QString>
#include <QtGlobal>

namespace WindowEdge {

bool supported() {
#ifdef Q_OS_WIN
    return QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11;
#else
    return false;
#endif
}

bool enabled() { return supported() && qEnvironmentVariable("WTFML_WINDOW_EDGE") != QLatin1String("0"); }

}  // namespace WindowEdge
