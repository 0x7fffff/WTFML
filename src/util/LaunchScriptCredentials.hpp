#pragma once
#include <QString>

namespace LaunchScriptCredentials {
inline QString placeholder() { return QStringLiteral("__WTFML_RUNTIME_MC_TOKEN_7E41__"); }
inline QString preamble() {
    return QStringLiteral(
        "setlocal DisableDelayedExpansion\r\n"
        "rem No login token is stored here. Prefer launching online games through WTFML.\r\n"
        "if not defined WTFML_MC_ACCESS_TOKEN (\r\n"
        "  echo Missing WTFML_MC_ACCESS_TOKEN. Supply a valid token in this process environment.\r\n"
        "  exit /b 1\r\n"
        ")\r\n"
        "powershell.exe -NoProfile -NonInteractive -Command \"if ($env:WTFML_MC_ACCESS_TOKEN -notmatch '^[A-Za-z0-9._-]+$') { exit 1 }\"\r\n"
        "if errorlevel 1 (\r\n"
        "  echo Invalid token format or unable to validate token.\r\n"
        "  exit /b 1\r\n"
        ")\r\n");
}
// Run after batch escaping, so the intentional environment reference is not doubled.
inline QString argument(QString quoted) {
    if (!quoted.contains(placeholder())) return quoted;
    quoted.replace(placeholder(), QStringLiteral("%WTFML_MC_ACCESS_TOKEN%"));
    if (!quoted.startsWith(QLatin1Char('"'))) quoted = QLatin1Char('"') + quoted + QLatin1Char('"');
    return quoted;
}
} // namespace LaunchScriptCredentials
