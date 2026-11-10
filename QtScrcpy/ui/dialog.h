#ifndef DIALOG_H
#define DIALOG_H

#include <QWidget>
#include <QPointer>
#include <QMessageBox>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QListWidget>
#include <QTimer>
#include <QVector>


#include "adbprocess.h"
#include "../QtScrcpyCore/include/QtScrcpyCore.h"
#include "audio/audiooutput.h"
#include "presetconfigdialog.h"

namespace Ui
{
    class Widget;
}

class QYUVOpenGLWidget;
class QComboBox;
class QLineEdit;
class QCheckBox;
class QGroupBox;
class QPushButton;
class QSpinBox;
class QDialog;
class QLabel;
class Dialog : public QWidget
{
    Q_OBJECT

public:
    explicit Dialog(QWidget *parent = 0);
    ~Dialog();

    void outLog(const QString &log, bool newLine = true);
    bool filterLog(const QString &log);
    void getIPbyIp();
    // show the main window (e.g. when hidden in the tray) and give it focus
    void bringToFront();

private slots:
    void onDeviceConnected(bool success, const QString& serial, const QString& deviceName, const QSize& size);
    void onDeviceDisconnected(QString serial);

    void on_updateDevice_clicked();
    void on_startServerBtn_clicked();
    void on_stopServerBtn_clicked();
    void on_wirelessConnectBtn_clicked();
    void on_startAdbdBtn_clicked();
    void on_getIPBtn_clicked();
    void on_wirelessDisConnectBtn_clicked();
    void on_selectRecordPathBtn_clicked();
    void on_recordPathEdt_textChanged(const QString &arg1);
    void on_adbCommandBtn_clicked();
    void on_stopAdbBtn_clicked();
    void on_clearOut_clicked();
    void on_stopAllServerBtn_clicked();
    void on_refreshGameScriptBtn_clicked();
    void on_applyScriptBtn_clicked();
    void on_editScriptBtn_clicked();
    // VenMirror v1.1.0: el editor guardó el mapa con un nombre nuevo;
    // refrescar la lista de mapas y seleccionar ese archivo.
    void onKeymapSaved(const QString &fileName);
    // WiFi sin USB (VenMirror v1.1.0): depuración inalámbrica Android 11+.
    void on_wifiPairBtn_clicked();
    void on_wifiConnectWifiBtn_clicked();
    void on_wifiScanBtn_clicked();
    void on_wifiQrBtn_clicked();
    void onWifiMdnsTimer();
    void onWifiQrDialogFinished();
    void on_gameBox_currentIndexChanged(const QString &arg1);
    void on_profileBox_currentIndexChanged(const QString &arg1);
    void on_saveProfileBtn_clicked();
    void on_importGameScriptBtn_clicked();
    void on_exportGameScriptBtn_clicked();
    void on_recordScreenCheck_clicked(bool checked);
    void on_usbConnectBtn_clicked();
    void on_wifiConnectBtn_clicked();
    void on_connectedPhoneList_itemDoubleClicked(QListWidgetItem *item);
    void on_updateNameBtn_clicked();
    void on_useSingleModeCheck_clicked();
    void on_serialBox_currentIndexChanged(const QString &arg1);

    void on_startAudioBtn_clicked();

    void on_stopAudioBtn_clicked();

    void on_installSndcpyBtn_clicked();

    void on_autoUpdatecheckBox_toggled(bool checked);

    void on_codecModeBox_currentIndexChanged(int index);
    void on_presetConfigBtn_clicked();

    void on_videoSourceBox_currentIndexChanged(int index);
    void on_refreshCameraBtn_clicked();

    void showIpEditMenu(const QPoint &pos);

private:
    bool checkAdbRun();
    void initUI();
    void updateBootConfig(bool toView = true);
    void execAdbCmd();
    void delayMs(int ms);
    QString getGameScript(const QString &fileName);
    // Ruta del mapa (usuario primero, luego instalación) o vacía.
    QString getGameScriptPath(const QString &fileName);
    // Perfiles por juego (VenMirror Fase 3): profiles.json vive en el
    // directorio de keymaps del usuario, con fallback al de instalación.
    struct GameProfile
    {
        QString name;
        QString keymap;
        QString package;
    };
    void loadProfiles();
    void refreshProfiles();
    bool saveProfiles();
    void updateEditScriptBtnState();
    void slotActivated(QSystemTrayIcon::ActivationReason reason);
    int findDeviceFromeSerialBox(bool wifi);
    quint32 getBitRate();
    const QString &getServerPath();
    void initAdvancedConfigUi(QWidget *parent);
    void loadAdvancedConfig();
    void saveAdvancedConfig();
    void openUserDirectory(const QString &path);
    void refreshApps();
    void updateVideoSourceUi();
    void initAdvancedDisplayUi();
    void updateAdvancedDisplayUi();
    void loadIpHistory();
    void saveIpHistory(const QString &ip);
    void loadPortHistory();
    void savePortHistory(const QString &port);

    void showPortEditMenu(const QPoint &pos);
    void syncPresetLevelToUi();

    // WiFi sin USB (VenMirror v1.1.0): depuración inalámbrica Android 11+.
    // El camino por USB (`adb tcpip 5555`) sigue existiendo tal cual en
    // el grupo "WiFi por USB"; esto añade el camino sin USB.
    void initWifiNoUsbUi();
    void wifiQueueConnect(const QString &addr);
    void wifiConnectNext();
    void wifiParseMdnsServices(const QString &output);
    void wifiStart5555Scan();
    void wifiFinish5555Scan(const QStringList &found);
    QString wifiRandomSecret(int length);
    bool wifiIsValidIp(const QString &ip);
    void wifiShowQrDialog(const QString &payload);

protected:
    void closeEvent(QCloseEvent *event);

private:
    Ui::Widget *ui;
    qsc::AdbProcess m_adb;
    QSystemTrayIcon *m_hideIcon;
    QGroupBox *m_advancedDisplayGroup = nullptr;
    QComboBox *m_configLanguageBox = nullptr;
    QLineEdit *m_configTitleEdit = nullptr;
    QLineEdit *m_configPushPathEdit = nullptr;
    QSpinBox *m_configMaxFpsSpin = nullptr;
    QCheckBox *m_configRenderExpiredCheck = nullptr;
    QComboBox *m_configOpenGlBox = nullptr;
    QLineEdit *m_configServerPathEdit = nullptr;
    QLineEdit *m_configAdbPathEdit = nullptr;
    QLineEdit *m_configCodecOptionsEdit = nullptr;
    QLineEdit *m_configCodecNameEdit = nullptr;
    QComboBox *m_configLogLevelBox = nullptr;
    QComboBox *m_displayModeBox = nullptr;
    QLineEdit *m_displayIdEdit = nullptr;
    QLineEdit *m_newDisplayEdit = nullptr;
    QLineEdit *m_cropEdit = nullptr;
    QCheckBox *m_flexDisplayCheck = nullptr;
    QComboBox *m_displayImePolicyBox = nullptr;
    QCheckBox *m_vdSystemDecorationsCheck = nullptr;
    QCheckBox *m_vdDestroyContentCheck = nullptr;
    QCheckBox *m_keepActiveCheck = nullptr;
    QComboBox *m_startAppBox = nullptr;
    QPushButton *m_refreshAppsBtn = nullptr;
    QMenu *m_menu;
    QAction *m_showWindow;
    QAction *m_quit;
    AudioOutput m_audioOutput;
    QTimer m_autoUpdatetimer;
    quint32 m_prevBitRate = 2000000;
    int m_prevMaxSizeIndex = 0;
    QPointer<PresetConfigDialog> m_presetDialog;
    // Perfiles por juego (VenMirror Fase 3). m_loadingProfiles bloquea
    // los slots de profileBox/gameBox mientras se repueblan, para que no
    // se aplique un mapa por un cambio programático.
    QVector<GameProfile> m_profiles;
    bool m_loadingProfiles = false;

    // WiFi sin USB (VenMirror v1.1.0). El escaneo 5555 corre en un
    // std::thread (nunca bloquea la UI); el resto usa m_adb, que ya es
    // asíncrono, con el temporizador m_wifiMdnsTimer para el sondeo mDNS.
    QGroupBox *m_wifiNoUsbGroup = nullptr;
    QLineEdit *m_wifiIpEdit = nullptr;
    QLineEdit *m_wifiPairPortEdit = nullptr;
    QLineEdit *m_wifiCodeEdit = nullptr;
    QLineEdit *m_wifiConnectPortEdit = nullptr;
    QPushButton *m_wifiPairBtn = nullptr;
    QPushButton *m_wifiConnectWifiBtn = nullptr;
    QPushButton *m_wifiScanBtn = nullptr;
    QPushButton *m_wifiQrBtn = nullptr;
    QStringList m_wifiConnectQueue;
    QTimer m_wifiMdnsTimer;
    QString m_wifiQrService;
    QString m_wifiQrPassword;
    int m_wifiMdnsPollsLeft = 0;
    bool m_wifiQrPairing = false;
    bool m_wifiAfterPair = false;
    bool m_wifiManualScan = false;
    bool m_wifiScanning5555 = false;
    QPointer<QDialog> m_wifiQrDialog;
    QPointer<QLabel> m_wifiQrImageLabel;
};

#endif // DIALOG_H
