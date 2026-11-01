#pragma once

#include "input/gameaction.h"
#include "launcherenv.h"

#include <QString>
#include <QUrl>

namespace tibia::app {

/// Product portal URLs for CIP OPEN_URL_* / OPEN_NEWS actions (CIP reads
/// tibiaPageUrl and friends from the [URLS] blob). Defaults are CipSoft-shaped
/// extensionless website paths under loginBase; QTCLIENT_URL_* / paymentsBase override.
class PortalUrls
{
public:
    /// Path under the portal origin for one OPEN_URL / OPEN_NEWS action, or empty.
    static QUrl urlFor(const LauncherEnv& env, input::EGameAction action);
    /// Backward-compatible: CIP defaults only (no overrides).
    static QUrl urlFor(const QUrl& loginBase, input::EGameAction action);
    /// CIP clientWebService (`/clientservices/clientservices.php` by default).
    static QUrl clientServicesUrl(const QUrl& loginBase,
                                  const QString& clientServicesPath = QStringLiteral("/clientservices/clientservices.php"));
    static void open(const QUrl& url);
};

}
