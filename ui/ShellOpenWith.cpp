#include "ShellOpenWith.hpp"

#include <QWidget>

#include <windows.h>
#include <shlobj.h>

namespace ShellOpenWith {

QWidget *ownerWindow(QWidget *owner) { return owner != nullptr ? owner->window() : nullptr; }

bool openWithDialog(QWidget *owner, const QString &path) {
    if (path.isEmpty()) return false;
    OPENASINFO info{};
    info.pcszFile = reinterpret_cast<LPCWSTR>(path.utf16());
    info.pcszClass = nullptr;
    info.oaifInFlags = OAIF_EXEC;
    QWidget *top = ownerWindow(owner);
    const HWND parent = top != nullptr ? reinterpret_cast<HWND>(top->winId()) : nullptr;
    return SHOpenWithDialog(parent, &info) == S_OK;
}

}  // namespace ShellOpenWith
