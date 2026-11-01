#include "aboutdialogcontroller.h"

#include "app/launcherenv.h"
#include "client/gameclient.h"
#include "gamewindow/messagedialogcontroller.h"
#include "input/gameaction.h"
#include "input/gameactionhandlers.h"

#include <QCoreApplication>
#include <QtGlobal>

namespace tibia::gamewindow {

TAboutDialogController::TAboutDialogController(QObject* parent)
    : TDialogController(QStringLiteral("AboutDialog.qml"), parent)
    , m_licensesMessage(new TMessageDialogController(this))
{
    m_licensesMessage->setCloseOnButtonClicked(true);
}

QString TAboutDialogController::clientType() const
{
    return QCoreApplication::applicationName();
}

QString TAboutDialogController::operatingSystemName() const
{
#if defined(Q_OS_WIN)
    return qtTrId("aboutdialog_windows");
#elif defined(Q_OS_MACOS)
    return qtTrId("aboutdialog_macosx");
#else
    return qtTrId("aboutdialog_linux");
#endif
}

QString TAboutDialogController::tibiaVersion() const
{
    constexpr int version = client::TGameClient::ClientVersion;
    QString text = QStringLiteral("%1.%2").arg(version / 100).arg(version % 100, 2, 10, QLatin1Char('0'));
    const QString revision = app::LauncherEnv::load(QString()).revision;
    if (!revision.isEmpty())
        text += QLatin1Char('.') + revision;
    return text;
}

QString TAboutDialogController::copyrightPeriod() const
{
    return QStringLiteral("%1-%2").arg(2002).arg(2026);
}

QString TAboutDialogController::websiteHost() const
{
    if (!m_portalBase.isEmpty() && !m_portalBase.host().isEmpty())
        return m_portalBase.host() + (m_portalBase.port() > 0 ? QStringLiteral(":%1").arg(m_portalBase.port()) : QString());
    return QCoreApplication::applicationName();
}

void TAboutDialogController::setPortalBase(const QUrl& loginBase)
{
    m_portalBase = loginBase;
}

void TAboutDialogController::showDialog()
{
    dialog()->show();
}

void TAboutDialogController::requestClose()
{
    dialog()->close();
}

void TAboutDialogController::requestOpenWebsite()
{
    input::publish(input::makeAction(input::EGameAction::OPEN_URL_TIBIA_PAGE));
}

void TAboutDialogController::requestShowLicensesDialog()
{
    // CIP 0x100c4b120: message caption aboutdialog_licenses; body from
    // *(this+0x40)->vslot[0x1a8/8] which concatenates
    // Resources/3rdpartylicences/*.txt (qt.txt, qt-3rdparty.txt, protobuf, …;
    // includes BattlEye EULA — out of scope). Product lists our linked libs.
    m_licensesMessage->setButtons({ new TMessageDialogButton(qtTrId("ok"), TMessageDialogController::Button::OK, QKeySequence(QStringLiteral("Return"))) });
    m_licensesMessage->showMessage(qtTrId("aboutdialog_licenses"),
                                   QStringLiteral("Qt %1 (LGPLv3), zlib, miniaudio (public domain), protobuf (BSD)")
                                       .arg(QLatin1StringView(qVersion())));
}

}
