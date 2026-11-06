#include "ShellOpenWith.hpp"

#include <QWidget>

#include <windows.h>
#include <shlobj.h>

namespace ShellOpenWith {

bool openWithDialog(QWidget *owner, const QString &path) {
    if (path.isEmpty()) return false;
    OPENASINFO info{};
    info.pcszFile = reinterpret_cast<LPCWSTR>(path.utf16());
    info.pcszClass = nullptr;
    info.oaifInFlags = OAIF_EXEC;
    const HWND parent = owner != nullptr ? reinterpret_cast<HWND>(owner->winId()) : nullptr;
    return SHOpenWithDialog(parent, &info) == S_OK;
}

}  // namespace ShellOpenWith
