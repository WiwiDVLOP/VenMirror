#include <QDebug>
#include <QAbstractItemView>
#include <QDesktopServices>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QGuiApplication>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QNetworkInterface>
#include <QPainter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QScreen>
#include <QScrollArea>
#include <QSet>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QSpinBox>
#include <QTabWidget>
#include <QTcpSocket>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <QUrl>

#include <thread>

#include "config.h"
#include "dialog.h"
#include "ui_dialog.h"
#include "videoform.h"
#include "venmirror_qr.h"
#include "../groupcontroller/groupcontroller.h"

#ifdef Q_OS_WIN32
#include "../util/winutils.h"
#endif

QString s_keyMapPath = "";
QString s_defaultKeyMapPath = "";

namespace {
class ComboBoxItemDelegate final : public QStyledItemDelegate
{
public:
    explicit ComboBoxItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize hint = QStyledItemDelegate::sizeHint(option, index);
        hint.setHeight(qMax(hint.height(), 30));
        return hint;
    }
};

// VenMirror v1.1.0: renderiza la matriz QR en una QImage con zona
// silenciosa de 4 módulos y `scale` píxeles por módulo.
QImage qrCodeImage(const venmirror::QrMatrix &qr, int scale)
{
    const int quiet = 4;
    const int modules = qr.size + 2 * quiet;
    QImage image(modules * scale, modules * scale, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    for (int y = 0; y < qr.size; y++) {
        for (int x = 0; x < qr.size; x++) {
            if (qr.modules[static_cast<size_t>(y)][static_cast<size_t>(x)]) {
                painter.fillRect((x + quiet) * scale, (y + quiet) * scale, scale, scale, Qt::black);
            }
        }
    }
    return image;
}
} // namespace

const QString &getKeyMapPath()
{
    if (s_keyMapPath.isEmpty()) {
        s_keyMapPath = QString::fromLocal8Bit(qgetenv("QTSCRCPY_KEYMAP_PATH"));
        if (s_keyMapPath.isEmpty()) {
            s_keyMapPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/keymap";
        }
        if (!QDir().mkpath(s_keyMapPath)) {
            qWarning() << "Failed to create keymap directory:" << s_keyMapPath;
        }
    }
    return s_keyMapPath;
}

const QString &getDefaultKeyMapPath()
{
    if (s_defaultKeyMapPath.isEmpty()) {
        s_defaultKeyMapPath = QString::fromLocal8Bit(qgetenv("QTSCRCPY_DEFAULT_KEYMAP_PATH"));
        if (s_defaultKeyMapPath.isEmpty()) {
            s_defaultKeyMapPath = QCoreApplication::applicationDirPath() + "/keymap";
        }
    }
    return s_defaultKeyMapPath;
}

void initializeKeyMapDirectory()
{
    const QDir defaultDir(getDefaultKeyMapPath());
    const QDir userDir(getKeyMapPath());
    if (!defaultDir.exists() || !userDir.exists()) {
        return;
    }

    const QFileInfoList defaults = defaultDir.entryInfoList(QStringList() << "*.json", QDir::Files);
    for (const QFileInfo &fileInfo : defaults) {
        const QString userFile = userDir.filePath(fileInfo.fileName());
        if (!QFileInfo::exists(userFile) && !QFile::copy(fileInfo.filePath(), userFile)) {
            qWarning() << "Failed to copy default keymap to:" << userFile;
        }
    }
}

Dialog::Dialog(QWidget *parent) : QWidget(parent), ui(new Ui::Widget)
{
    ui->setupUi(this);
    initializeKeyMapDirectory();
    initUI();

    updateBootConfig(true);

    // Intel Mac 上如果配置文件遗留 decodeMode=1，强制重置为 0
    // （VideoToolbox 选项在 initUI() 中已被移除）
#if defined(Q_OS_MACOS) && !defined(__arm64__)
    if (ui->decodeModeBox->currentIndex() != 0) {
        ui->decodeModeBox->setCurrentIndex(0);
    }
#endif

    on_useSingleModeCheck_clicked();
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect availableGeometry = screen->availableGeometry();
        move(availableGeometry.x() + (availableGeometry.width() - width()) / 2,
             availableGeometry.y() + (availableGeometry.height() - height()) / 2);
    }
    on_updateDevice_clicked();

    connect(&m_autoUpdatetimer, &QTimer::timeout, this, &Dialog::on_updateDevice_clicked);
    if (ui->autoUpdatecheckBox->isChecked()) {
        m_autoUpdatetimer.start(5000);
    }

    connect(&m_adb, &qsc::AdbProcess::adbProcessResult, this, [this](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
        QString log = "";
        bool newLine = true;
        QStringList args = m_adb.arguments();

        switch (processResult) {
        case qsc::AdbProcess::AER_ERROR_START:
            break;
        case qsc::AdbProcess::AER_SUCCESS_START:
            log = "adb run";
            newLine = false;
            break;
        case qsc::AdbProcess::AER_ERROR_EXEC:
            //log = m_adb.getErrorOut();
            if (args.contains("ifconfig") && args.contains("wlan0")) {
                getIPbyIp();
            } else if (args.contains("pair")) {
                // VenMirror v1.1.0: falló `adb pair` (código o puerto
                // incorrectos, o el teléfono cerró el emparejamiento).
                QString err = m_adb.getErrorOut().trimmed();
                log = err.isEmpty()
                          ? "No se pudo emparejar. Revisa el código de 6 dígitos y el puerto, y vuelve a intentarlo."
                          : err;
                m_wifiMdnsTimer.stop();
                m_wifiQrPairing = false;
                m_wifiAfterPair = false;
            }
            break;
        case qsc::AdbProcess::AER_ERROR_MISSING_BINARY:
            log = "adb not found";
            break;
        case qsc::AdbProcess::AER_SUCCESS_EXEC:
            //log = m_adb.getStdOut();
            if (args.contains("devices")) {
                QStringList devices = m_adb.getDevicesSerialFromStdOut();
                ui->serialBox->clear();
                ui->connectedPhoneList->clear();
                for (auto &item : devices) {
                    ui->serialBox->addItem(item);
                    ui->connectedPhoneList->addItem(Config::getInstance().getNickName(item) + "-" + item);
                }
            } else if (args.contains("show") && args.contains("wlan0")) {
                QString ip = m_adb.getDeviceIPFromStdOut();
                if (ip.isEmpty()) {
                    log = "ip not find, connect to wifi?";
                    break;
                }
                ui->deviceIpEdt->setEditText(ip);
            } else if (args.contains("ifconfig") && args.contains("wlan0")) {
                QString ip = m_adb.getDeviceIPFromStdOut();
                if (ip.isEmpty()) {
                    log = "ip not find, connect to wifi?";
                    break;
                }
                ui->deviceIpEdt->setEditText(ip);
            } else if (args.contains("ip -o a")) {
                QString ip = m_adb.getDeviceIPByIpFromStdOut();
                if (ip.isEmpty()) {
                    log = "ip not find, connect to wifi?";
                    break;
                }
                ui->deviceIpEdt->setEditText(ip);
            } else if (args.contains("mdns") && args.contains("services")) {
                // VenMirror v1.1.0: descubrimiento mDNS (depuración
                // inalámbrica) y sondeo del emparejamiento por QR.
                wifiParseMdnsServices(m_adb.getStdOut());
            } else if (args.contains("pair")) {
                // VenMirror v1.1.0: `adb pair` aceptado. El teléfono ya
                // confía en esta PC; ahora hay que encontrar su puerto
                // de conexión (_adb-tls-connect) con un sondeo mDNS.
                QString out = m_adb.getStdOut().trimmed();
                log = out.isEmpty() ? "Emparejado correctamente. Buscando el puerto de conexión..." : out;
                if (m_wifiQrDialog) {
                    m_wifiQrDialog->close();
                }
                // El puerto de conexión (_adb-tls-connect) puede tardar
                // unos segundos en publicarse tras el emparejamiento:
                // sondear mDNS hasta 5 veces (10 s) antes de rendirse.
                m_wifiAfterPair = true;
                m_wifiMdnsPollsLeft = 5;
                m_wifiMdnsTimer.start();
            }
            break;
        }
        if (!log.isEmpty()) {
            outLog(log, newLine);
        }
        // VenMirror v1.1.0: encadenar la cola de `adb connect` del WiFi
        // sin USB (éxito o error: un connect fallido no debe atascar la
        // cola). Con la cola vacía no se toca nada: el botón inalámbrico
        // clásico sigue comportándose como antes.
        if (!args.isEmpty() && args.first() == "connect" && !m_wifiConnectQueue.isEmpty()) {
            QTimer::singleShot(0, this, &Dialog::wifiConnectNext);
        }
    });

    m_hideIcon = new QSystemTrayIcon(this);
    m_hideIcon->setIcon(QIcon(":/image/tray/logo.png"));
    m_menu = new QMenu(this);
    m_quit = new QAction(this);
    m_showWindow = new QAction(this);
    m_showWindow->setText(tr("show"));
    m_quit->setText(tr("quit"));
    m_menu->addAction(m_showWindow);
    m_menu->addAction(m_quit);
    m_hideIcon->setContextMenu(m_menu);
    m_hideIcon->show();
    connect(m_showWindow, &QAction::triggered, this, &Dialog::show);
    connect(m_quit, &QAction::triggered, this, [this]() {
        m_hideIcon->hide();
        qApp->quit();
    });
    connect(m_hideIcon, &QSystemTrayIcon::activated, this, &Dialog::slotActivated);

    connect(&qsc::IDeviceManage::getInstance(), &qsc::IDeviceManage::deviceConnected, this, &Dialog::onDeviceConnected);
    connect(&qsc::IDeviceManage::getInstance(), &qsc::IDeviceManage::deviceDisconnected, this, &Dialog::onDeviceDisconnected);
}

Dialog::~Dialog()
{
    qDebug() << "~Dialog()";
    updateBootConfig(false);
    qsc::IDeviceManage::getInstance().disconnectAllDevice();
    delete ui;
}

void Dialog::initUI()
{
    setAttribute(Qt::WA_DeleteOnClose);
    //setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint | Qt::CustomizeWindowHint);

    setWindowTitle(Config::getInstance().getTitle());
#ifdef Q_OS_LINUX
    // Set window icon (inherits from application icon set in main.cpp)
    // If application icon was set, this will use it automatically
    if (!qApp->windowIcon().isNull()) {
        setWindowIcon(qApp->windowIcon());
    }
#endif

#ifdef Q_OS_WIN32
    WinUtils::setDarkBorderToWindow((HWND)this->winId(), true);
#endif

    ui->bitRateEdit->setValidator(new QIntValidator(1, 99999, this));

    ui->presetConfigBtn->setVisible(false);

    // Populate the codec-mode combo. Index 0 is the static "Default (Auto)"
    // item from the .ui; every registered encoder preset is appended below it.
    for (const EncoderPreset &preset : EncoderPresetRegistry::all()) {
        ui->codecModeBox->addItem(preset.displayName);
    }

    ui->maxSizeBox->addItem("640");
    ui->maxSizeBox->addItem("720");
    ui->maxSizeBox->addItem("1080");
    ui->maxSizeBox->addItem("1280");
    ui->maxSizeBox->addItem("1920");
    ui->maxSizeBox->addItem(tr("original"));

    ui->videoSourceBox->addItem(tr("display"));
    ui->videoSourceBox->addItem(tr("camera"));
    ui->cameraFacingBox->addItem(tr("back"));
    ui->cameraFacingBox->addItem(tr("front"));

    ui->formatBox->addItem("mp4");
    ui->formatBox->addItem("mkv");

    ui->lockOrientationBox->addItem(tr("no lock"));
    ui->lockOrientationBox->addItem("0");
    ui->lockOrientationBox->addItem("90");
    ui->lockOrientationBox->addItem("180");
    ui->lockOrientationBox->addItem("270");
    ui->lockOrientationBox->setCurrentIndex(0);

    ui->decodeModeBox->addItem(tr("FFmpeg + OpenGL (Universal Default)"));
    ui->decodeModeBox->addItem(tr("VideoToolbox + Metal (Apple Silicon Only)"));
    ui->decodeModeBox->setCurrentIndex(0);

#ifndef Q_OS_MACOS
    // 非 macOS：隐藏整个解码模式控件行
    ui->decodeModeLabel->hide();
    ui->decodeModeBox->hide();
#elif !defined(__arm64__)
    // Intel Mac：移除 VideoToolbox 选项，用户只看到 FFmpeg
    ui->decodeModeBox->removeItem(1);
#endif

    // 加载IP历史记录
    loadIpHistory();

    // 加载端口历史记录
    loadPortHistory();

    // 为deviceIpEdt添加右键菜单
    if (ui->deviceIpEdt->lineEdit()) {
        ui->deviceIpEdt->lineEdit()->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(ui->deviceIpEdt->lineEdit(), &QWidget::customContextMenuRequested,
                this, &Dialog::showIpEditMenu);
    }
    
    // 为devicePortEdt添加右键菜单
    if (ui->devicePortEdt->lineEdit()) {
        ui->devicePortEdt->lineEdit()->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(ui->devicePortEdt->lineEdit(), &QWidget::customContextMenuRequested,
                this, &Dialog::showPortEditMenu);
    }
    initAdvancedDisplayUi();

    auto *configTabs = new QTabWidget(ui->rightWidget);
    auto *startConfigPage = new QWidget(configTabs);
    auto *advancedDisplayPage = new QWidget(configTabs);
    auto *advancedConfigPage = new QWidget(configTabs);
    auto *startConfigLayout = new QVBoxLayout(startConfigPage);
    auto *advancedDisplayLayout = new QVBoxLayout(advancedDisplayPage);
    startConfigLayout->setContentsMargins(0, 0, 0, 0);
    advancedDisplayLayout->setContentsMargins(0, 0, 0, 0);

    ui->verticalLayout_5->removeWidget(ui->adbGroupBox);
    ui->verticalLayout_5->removeWidget(ui->outEdit);
    ui->verticalLayout_6->removeWidget(ui->configGroupBox);
    ui->verticalLayout_6->removeWidget(ui->usbGroupBox);
    ui->verticalLayout_6->removeWidget(ui->wirelessGroupBox);
    ui->verticalLayout_6->removeWidget(m_advancedDisplayGroup);
    delete ui->verticalLayout_6->takeAt(0);

    ui->verticalLayout_5->addWidget(ui->usbGroupBox);
    ui->verticalLayout_5->addWidget(ui->wirelessGroupBox);
    // VenMirror v1.1.0: panel "WiFi sin USB" debajo del grupo por USB.
    initWifiNoUsbUi();
    startConfigLayout->addWidget(ui->configGroupBox);
    startConfigLayout->addWidget(ui->adbGroupBox);
    startConfigLayout->addWidget(ui->outEdit, 1);
    advancedDisplayLayout->addWidget(m_advancedDisplayGroup);
    advancedDisplayLayout->addStretch();
    initAdvancedConfigUi(advancedConfigPage);
    configTabs->addTab(startConfigPage, tr("Start Config"));
    configTabs->addTab(advancedDisplayPage, tr("Advanced Display"));
    configTabs->addTab(advancedConfigPage, tr("Advanced Config"));
    configTabs->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    ui->verticalLayout_6->addWidget(configTabs);

    ui->leftWidget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    ui->leftWidget->setMinimumWidth(0);
    ui->horizontalLayout_11->setStretch(0, 1);
    ui->horizontalLayout_11->setStretch(1, 2);
    ui->horizontalLayout->setStretch(0, 1);
    ui->horizontalLayout->setStretch(2, 0);

    for (QComboBox *comboBox : findChildren<QComboBox *>()) {
        comboBox->view()->setItemDelegate(new ComboBoxItemDelegate(comboBox->view()));
    }

    QSizePolicy simpleModePolicy = ui->simpleGroupBox->sizePolicy();
    simpleModePolicy.setVerticalPolicy(QSizePolicy::Expanding);
    ui->simpleGroupBox->setSizePolicy(simpleModePolicy);

    QSizePolicy logPolicy = ui->outEdit->sizePolicy();
    logPolicy.setVerticalPolicy(QSizePolicy::Expanding);
    ui->outEdit->setSizePolicy(logPolicy);
    ui->verticalLayout_5->setStretch(0, 0);
    ui->verticalLayout_5->setStretch(1, 1);
    ui->verticalLayout_5->setStretch(2, 0);
    ui->verticalLayout_5->setStretch(3, 0);
    ui->verticalLayout_4->setStretch(2, 1);
    ui->verticalLayout_6->setStretch(0, 1);

    // Cargar la lista de mapas y perfiles al arrancar, para que el
    // selector de perfil ya ofrezca "Monster Hunter Outlanders" en una
    // instalación nueva (antes la lista quedaba vacía hasta Actualizar).
    on_refreshGameScriptBtn_clicked();

    updateEditScriptBtnState();
}

void Dialog::initAdvancedDisplayUi()
{
    m_advancedDisplayGroup = new QGroupBox(tr("Advanced display"), this);
    m_advancedDisplayGroup->setCheckable(true);
    m_advancedDisplayGroup->setChecked(false);
    auto *layout = new QFormLayout(m_advancedDisplayGroup);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_displayModeBox = new QComboBox(m_advancedDisplayGroup);
    m_displayModeBox->addItem(tr("Primary display"));
    m_displayModeBox->addItem(tr("Existing display ID"));
    m_displayModeBox->addItem(tr("New virtual display"));
    layout->addRow(tr("Display mode"), m_displayModeBox);

    m_displayIdEdit = new QLineEdit(m_advancedDisplayGroup);
    m_displayIdEdit->setPlaceholderText("1");
    layout->addRow(tr("Display ID"), m_displayIdEdit);

    m_newDisplayEdit = new QLineEdit(m_advancedDisplayGroup);
    m_newDisplayEdit->setPlaceholderText("1920x1080/240");
    layout->addRow(tr("Virtual size / DPI"), m_newDisplayEdit);

    m_cropEdit = new QLineEdit(m_advancedDisplayGroup);
    m_cropEdit->setPlaceholderText("width:height:x:y");
    layout->addRow(tr("Crop"), m_cropEdit);

    m_flexDisplayCheck = new QCheckBox(tr("Resize virtual display with window"), m_advancedDisplayGroup);
    layout->addRow(m_flexDisplayCheck);
    m_displayImePolicyBox = new QComboBox(m_advancedDisplayGroup);
    m_displayImePolicyBox->addItem(tr("Server default"), "");
    m_displayImePolicyBox->addItem("local", "local");
    m_displayImePolicyBox->addItem("fallback", "fallback");
    m_displayImePolicyBox->addItem("hide", "hide");
    layout->addRow(tr("IME policy"), m_displayImePolicyBox);
    m_vdSystemDecorationsCheck = new QCheckBox(tr("Show system decorations"), m_advancedDisplayGroup);
    m_vdSystemDecorationsCheck->setChecked(true);
    layout->addRow(m_vdSystemDecorationsCheck);
    m_vdDestroyContentCheck = new QCheckBox(tr("Destroy content on close"), m_advancedDisplayGroup);
    m_vdDestroyContentCheck->setChecked(true);
    layout->addRow(m_vdDestroyContentCheck);
    m_keepActiveCheck = new QCheckBox(tr("Keep device active"), m_advancedDisplayGroup);
    layout->addRow(m_keepActiveCheck);
    auto *startAppWidget = new QWidget(m_advancedDisplayGroup);
    auto *startAppLayout = new QHBoxLayout(startAppWidget);
    startAppLayout->setContentsMargins(0, 0, 0, 0);
    m_startAppBox = new QComboBox(startAppWidget);
    m_startAppBox->setEditable(true);
    m_startAppBox->setInsertPolicy(QComboBox::NoInsert);
    m_startAppBox->setPlaceholderText("com.android.settings");
    m_startAppBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_refreshAppsBtn = new QPushButton(tr("refresh"), startAppWidget);
    m_refreshAppsBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_refreshAppsBtn->setFixedWidth(m_refreshAppsBtn->sizeHint().width());
    startAppLayout->addWidget(m_startAppBox);
    startAppLayout->addWidget(m_refreshAppsBtn);
    layout->addRow(tr("Start app"), startAppWidget);

    connect(m_displayModeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &Dialog::updateAdvancedDisplayUi);
    connect(m_flexDisplayCheck, &QCheckBox::toggled, this, &Dialog::updateAdvancedDisplayUi);
    connect(m_refreshAppsBtn, &QPushButton::clicked, this, &Dialog::refreshApps);
    updateAdvancedDisplayUi();
}

void Dialog::initAdvancedConfigUi(QWidget *parent)
{
    auto *pageLayout = new QVBoxLayout(parent);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    auto *scrollArea = new QScrollArea(parent);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scrollArea);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    auto *group = new QGroupBox(tr("Application configuration"), content);
    auto *layout = new QFormLayout(group);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_configLanguageBox = new QComboBox(group);
    m_configLanguageBox->addItem(tr("Automatic"), "Auto");
    m_configLanguageBox->addItem("简体中文", "zh_CN");
    m_configLanguageBox->addItem("English", "en_US");
    m_configLanguageBox->addItem("日本語", "ja_JP");
    m_configLanguageBox->addItem("한국어", "ko_KR");
    layout->addRow(tr("Language"), m_configLanguageBox);

    m_configTitleEdit = new QLineEdit(group);
    layout->addRow(tr("Window title"), m_configTitleEdit);

    m_configPushPathEdit = new QLineEdit(group);
    m_configPushPathEdit->setPlaceholderText("/sdcard/");
    layout->addRow(tr("Push file path"), m_configPushPathEdit);

    m_configMaxFpsSpin = new QSpinBox(group);
    m_configMaxFpsSpin->setRange(0, 1000);
    m_configMaxFpsSpin->setSpecialValueText(tr("Unlimited"));
    layout->addRow(tr("Maximum FPS"), m_configMaxFpsSpin);

    m_configRenderExpiredCheck = new QCheckBox(tr("Render expired video frames"), group);
    layout->addRow(m_configRenderExpiredCheck);

    m_configOpenGlBox = new QComboBox(group);
    m_configOpenGlBox->addItem(tr("Automatic"), -1);
    m_configOpenGlBox->addItem(tr("Software OpenGL"), 0);
    m_configOpenGlBox->addItem(tr("OpenGL ES"), 1);
    m_configOpenGlBox->addItem(tr("Desktop OpenGL"), 2);
    m_configOpenGlBox->setToolTip(tr("Takes effect after restarting VenMirror."));
    layout->addRow(tr("OpenGL backend"), m_configOpenGlBox);

    m_configServerPathEdit = new QLineEdit(group);
    m_configServerPathEdit->setPlaceholderText("/data/local/tmp/scrcpy-server.jar");
    layout->addRow(tr("Device server path"), m_configServerPathEdit);

    m_configAdbPathEdit = new QLineEdit(group);
    m_configAdbPathEdit->setPlaceholderText(tr("Use bundled ADB"));
    layout->addRow(tr("ADB executable"), m_configAdbPathEdit);

    m_configCodecOptionsEdit = new QLineEdit(group);
    m_configCodecOptionsEdit->setPlaceholderText("profile=1,level=2");
    layout->addRow(tr("Codec options"), m_configCodecOptionsEdit);

    m_configCodecNameEdit = new QLineEdit(group);
    m_configCodecNameEdit->setPlaceholderText("OMX.qcom.video.encoder.avc");
    layout->addRow(tr("Codec name"), m_configCodecNameEdit);

    m_configLogLevelBox = new QComboBox(group);
    m_configLogLevelBox->addItem("verbose", "verbose");
    m_configLogLevelBox->addItem("debug", "debug");
    m_configLogLevelBox->addItem("info", "info");
    m_configLogLevelBox->addItem("warn", "warn");
    m_configLogLevelBox->addItem("error", "error");
    layout->addRow(tr("Log level"), m_configLogLevelBox);

    auto *buttons = new QHBoxLayout();
    auto *openConfigButton = new QPushButton(tr("Open Config Directory"), group);
    auto *openKeymapButton = new QPushButton(tr("Open Keymap Directory"), group);
    auto *saveButton = new QPushButton(tr("Save"), group);
    buttons->addWidget(openConfigButton);
    buttons->addWidget(openKeymapButton);
    buttons->addStretch();
    buttons->addWidget(saveButton);
    layout->addRow(buttons);

    contentLayout->addWidget(group);
    contentLayout->addStretch();
    scrollArea->setWidget(content);
    pageLayout->addWidget(scrollArea);

    connect(openConfigButton, &QPushButton::clicked, this, [this] {
        openUserDirectory(Config::getInstance().getConfigDirectory());
    });
    connect(openKeymapButton, &QPushButton::clicked, this, [this] {
        openUserDirectory(getKeyMapPath());
    });
    connect(saveButton, &QPushButton::clicked, this, &Dialog::saveAdvancedConfig);
    loadAdvancedConfig();
}

void Dialog::loadAdvancedConfig()
{
    Config &config = Config::getInstance();
    m_configLanguageBox->setCurrentIndex(qMax(0, m_configLanguageBox->findData(config.getLanguage())));
    m_configTitleEdit->setText(config.getTitle());
    m_configPushPathEdit->setText(config.getPushFilePath());
    m_configMaxFpsSpin->setValue(qBound(0, config.getMaxFps(), m_configMaxFpsSpin->maximum()));
    m_configRenderExpiredCheck->setChecked(config.getRenderExpiredFrames() != 0);
    m_configOpenGlBox->setCurrentIndex(qMax(0, m_configOpenGlBox->findData(config.getDesktopOpenGL())));
    m_configServerPathEdit->setText(config.getServerPath());
    m_configAdbPathEdit->setText(config.getAdbPath());
    m_configCodecOptionsEdit->setText(config.getCodecOptions());
    m_configCodecNameEdit->setText(config.getCodecName());
    m_configLogLevelBox->setCurrentIndex(qMax(0, m_configLogLevelBox->findData(config.getLogLevel())));
}

void Dialog::saveAdvancedConfig()
{
    QString pushPath = m_configPushPathEdit->text().trimmed();
    if (!pushPath.isEmpty() && !pushPath.endsWith('/')) {
        pushPath += '/';
    }

    QMap<QString, QVariant> values;
    values.insert("Language", m_configLanguageBox->currentData());
    values.insert("WindowTitle", m_configTitleEdit->text().trimmed());
    values.insert("PushFilePath", pushPath);
    values.insert("MaxFps", m_configMaxFpsSpin->value());
    values.insert("RenderExpiredFrames", m_configRenderExpiredCheck->isChecked() ? 1 : 0);
    values.insert("UseDesktopOpenGL", m_configOpenGlBox->currentData());
    values.insert("ServerPath", m_configServerPathEdit->text().trimmed());
    values.insert("AdbPath", m_configAdbPathEdit->text().trimmed());
    values.insert("CodecOptions", m_configCodecOptionsEdit->text().trimmed());
    values.insert("CodecName", m_configCodecNameEdit->text().trimmed());
    values.insert("LogLevel", m_configLogLevelBox->currentData());

    if (!Config::getInstance().updateCommonConfig(values)) {
        QMessageBox::warning(this, tr("Save failed"), tr("Unable to save config.ini."));
        return;
    }
    m_configPushPathEdit->setText(pushPath);
    QMessageBox::information(this, tr("Configuration saved"),
                             tr("The settings were saved to config.ini. Restart VenMirror for all changes to take effect."));
}

void Dialog::openUserDirectory(const QString &path)
{
    if (!QDir().mkpath(path) || !QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        QMessageBox::warning(this, tr("Open directory failed"), path);
    }
}

void Dialog::updateAdvancedDisplayUi()
{
    if (!m_displayModeBox) {
        return;
    }
    const bool existing = m_displayModeBox->currentIndex() == 1;
    const bool virtualDisplay = m_displayModeBox->currentIndex() == 2;
    m_displayIdEdit->setEnabled(existing);
    m_newDisplayEdit->setEnabled(virtualDisplay);
    m_flexDisplayCheck->setEnabled(virtualDisplay);
    if (!virtualDisplay) {
        m_flexDisplayCheck->setChecked(false);
    }
    m_displayImePolicyBox->setEnabled(virtualDisplay);
    m_vdSystemDecorationsCheck->setEnabled(virtualDisplay);
    m_vdDestroyContentCheck->setEnabled(virtualDisplay);
    m_cropEdit->setEnabled(!m_flexDisplayCheck->isChecked());
}

void Dialog::updateBootConfig(bool toView)
{
    if (toView) {
        UserBootConfig config = Config::getInstance().getUserBootConfig();

        if (config.bitRate == 0) {
            ui->bitRateBox->setCurrentText("Mbps");
        } else if (config.bitRate % 1000000 == 0) {
            ui->bitRateEdit->setText(QString::number(config.bitRate / 1000000));
            ui->bitRateBox->setCurrentText("Mbps");
        } else {
            ui->bitRateEdit->setText(QString::number(config.bitRate / 1000));
            ui->bitRateBox->setCurrentText("Kbps");
        }

        ui->maxSizeBox->setCurrentIndex(config.maxSizeIndex);
        ui->formatBox->setCurrentIndex(config.recordFormatIndex);
        ui->recordPathEdt->setText(config.recordPath);
        ui->lockOrientationBox->setCurrentIndex(config.lockOrientationIndex);
        ui->framelessCheck->setChecked(config.framelessWindow);
        ui->recordScreenCheck->setChecked(config.recordScreen);
        ui->notDisplayCheck->setChecked(config.recordBackground);
        ui->useReverseCheck->setChecked(config.reverseConnect);
        ui->fpsCheck->setChecked(config.showFPS);
        ui->alwaysTopCheck->setChecked(config.windowOnTop);
        ui->closeScreenCheck->setChecked(config.autoOffScreen);
        ui->stayAwakeCheck->setChecked(config.keepAlive);
        ui->useSingleModeCheck->setChecked(config.simpleMode);
        ui->autoUpdatecheckBox->setChecked(config.autoUpdateDevice);
        ui->showToolbar->setChecked(config.showToolbar);
        ui->quitOnCloseCheck->setChecked(config.quitOnClose);
        ui->decodeModeBox->setCurrentIndex(config.decodeMode);
        ui->codecModeBox->setCurrentIndex(config.codecModeIndex);

        // Restore encoder preset dialog state (if dialog was previously opened)
        if (m_presetDialog) {
            m_presetDialog->setLevel(config.presetLevel);
        }
        ui->videoSourceBox->setCurrentIndex(config.videoSource);
        ui->cameraFacingBox->setCurrentIndex(qBound(0, config.cameraFacing, 1));
        if (m_advancedDisplayGroup) {
            m_advancedDisplayGroup->setChecked(config.advancedDisplay);
            m_displayModeBox->setCurrentIndex(qBound(0, config.displayMode, 2));
            m_displayIdEdit->setText(config.displayId);
            m_newDisplayEdit->setText(config.newDisplay);
            m_cropEdit->setText(config.crop);
            m_flexDisplayCheck->setChecked(config.flexDisplay);
            m_displayImePolicyBox->setCurrentIndex(qMax(0, m_displayImePolicyBox->findData(config.displayImePolicy)));
            m_vdSystemDecorationsCheck->setChecked(config.vdSystemDecorations);
            m_vdDestroyContentCheck->setChecked(config.vdDestroyContent);
            m_keepActiveCheck->setChecked(config.keepActive);
            m_startAppBox->setEditText(config.startApp);
            updateAdvancedDisplayUi();
        }
        updateVideoSourceUi();
    } else {
        UserBootConfig config = Config::getInstance().getUserBootConfig();

        config.bitRate = getBitRate();
        config.maxSizeIndex = ui->maxSizeBox->currentIndex();
        config.recordFormatIndex = ui->formatBox->currentIndex();
        config.recordPath = ui->recordPathEdt->text();
        config.lockOrientationIndex = ui->lockOrientationBox->currentIndex();
        config.recordScreen = ui->recordScreenCheck->isChecked();
        config.recordBackground = ui->notDisplayCheck->isChecked();
        config.reverseConnect = ui->useReverseCheck->isChecked();
        config.showFPS = ui->fpsCheck->isChecked();
        config.windowOnTop = ui->alwaysTopCheck->isChecked();
        config.autoOffScreen = ui->closeScreenCheck->isChecked();
        config.framelessWindow = ui->framelessCheck->isChecked();
        config.keepAlive = ui->stayAwakeCheck->isChecked();
        config.simpleMode = ui->useSingleModeCheck->isChecked();
        config.autoUpdateDevice = ui->autoUpdatecheckBox->isChecked();
        config.showToolbar = ui->showToolbar->isChecked();
        config.quitOnClose = ui->quitOnCloseCheck->isChecked();
        config.decodeMode = ui->decodeModeBox->currentIndex();
        config.codecModeIndex = ui->codecModeBox->currentIndex();

        // Save encoder preset dialog state (if opened)
        if (m_presetDialog) {
            config.presetLevel = m_presetDialog->selectedLevel();
        }

        config.videoSource = ui->videoSourceBox->currentIndex();
        config.cameraFacing = qMin(ui->cameraFacingBox->currentIndex(), 1);
        if (m_advancedDisplayGroup) {
            config.advancedDisplay = m_advancedDisplayGroup->isChecked();
            config.displayMode = m_displayModeBox->currentIndex();
            config.displayId = m_displayIdEdit->text().trimmed();
            config.newDisplay = m_newDisplayEdit->text().trimmed();
            config.crop = m_cropEdit->text().trimmed();
            config.flexDisplay = m_flexDisplayCheck->isChecked();
            config.displayImePolicy = m_displayImePolicyBox->currentData().toString();
            config.vdSystemDecorations = m_vdSystemDecorationsCheck->isChecked();
            config.vdDestroyContent = m_vdDestroyContentCheck->isChecked();
            config.keepActive = m_keepActiveCheck->isChecked();
            config.startApp = m_startAppBox->currentData().toString();
            if (config.startApp.isEmpty()) {
                config.startApp = m_startAppBox->currentText().trimmed();
            }
        }

        // 保存当前IP到历史记录
        QString currentIp = ui->deviceIpEdt->currentText().trimmed();
        if (!currentIp.isEmpty()) {
            saveIpHistory(currentIp);
        }

        Config::getInstance().setUserBootConfig(config);
    }
}

void Dialog::execAdbCmd()
{
    if (checkAdbRun()) {
        return;
    }
    QString cmd = ui->adbCommandEdt->text().trimmed();
    outLog("adb " + cmd, false);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    m_adb.execute(ui->serialBox->currentText().trimmed(), cmd.split(" ", Qt::SkipEmptyParts));
#else
    m_adb.execute(ui->serialBox->currentText().trimmed(), cmd.split(" ", QString::SkipEmptyParts));
#endif
}

void Dialog::delayMs(int ms)
{
    QTime dieTime = QTime::currentTime().addMSecs(ms);

    while (QTime::currentTime() < dieTime) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }
}

QString Dialog::getGameScript(const QString &fileName)
{
    if (fileName.isEmpty()) {
        return "";
    }

    QFile loadFile(getKeyMapPath() + "/" + fileName);
    if (!loadFile.exists()) {
        loadFile.setFileName(getDefaultKeyMapPath() + "/" + fileName);
    }
    if (!loadFile.open(QIODevice::ReadOnly)) {
        outLog("open file failed:" + fileName, true);
        return "";
    }

    QString ret = loadFile.readAll();
    loadFile.close();
    return ret;
}

QString Dialog::getGameScriptPath(const QString &fileName)
{
    if (fileName.isEmpty()) {
        return QString();
    }
    const QString userPath = getKeyMapPath() + "/" + fileName;
    if (QFileInfo::exists(userPath)) {
        return userPath;
    }
    const QString defaultPath = getDefaultKeyMapPath() + "/" + fileName;
    if (QFileInfo::exists(defaultPath)) {
        return defaultPath;
    }
    return QString();
}

void Dialog::loadProfiles()
{
    m_profiles.clear();
    const QString path = getGameScriptPath("profiles.json");
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qWarning() << "profiles.json no válido:" << path;
        return;
    }
    const QJsonArray profiles = doc.array();
    for (const QJsonValue &value : profiles) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        GameProfile profile;
        profile.name = obj.value("name").toString();
        profile.keymap = obj.value("keymap").toString();
        profile.package = obj.value("package").toString();
        if (profile.name.isEmpty() || profile.keymap.isEmpty()) {
            continue;
        }
        m_profiles.append(profile);
    }
}

void Dialog::refreshProfiles()
{
    loadProfiles();
    const QString previous = ui->profileBox->currentText();
    m_loadingProfiles = true;
    ui->profileBox->clear();
    if (!m_profiles.isEmpty()) {
        for (const GameProfile &profile : m_profiles) {
            ui->profileBox->addItem(profile.name, profile.keymap);
        }
    } else {
        // Sin profiles.json en ningún directorio: el selector muestra
        // los mapas mismos como fallback (no se crea el archivo solo).
        for (int i = 0; i < ui->gameBox->count(); ++i) {
            ui->profileBox->addItem(ui->gameBox->itemText(i));
        }
    }
    const int restore = ui->profileBox->findText(previous);
    if (restore >= 0) {
        ui->profileBox->setCurrentIndex(restore);
    }
    m_loadingProfiles = false;
}

bool Dialog::saveProfiles()
{
    QJsonArray profiles;
    for (const GameProfile &profile : m_profiles) {
        QJsonObject obj;
        obj.insert("name", profile.name);
        obj.insert("keymap", profile.keymap);
        obj.insert("package", profile.package);
        profiles.append(obj);
    }
    QJsonDocument doc(profiles);
    QFile file(getKeyMapPath() + "/profiles.json");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return file.error() == QFile::NoError;
}

void Dialog::slotActivated(QSystemTrayIcon::ActivationReason reason)
{
    switch (reason) {
    case QSystemTrayIcon::Trigger:
#ifdef Q_OS_WIN32
        this->show();
#endif
        break;
    default:
        break;
    }
}

void Dialog::bringToFront()
{
    show();
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();
}

void Dialog::closeEvent(QCloseEvent *event)
{
    if (ui->quitOnCloseCheck->isChecked()) {
        // The user opted out of the system tray: closing the window quits the app.
        // Ignore the event (instead of accepting it) so WA_DeleteOnClose does not
        // delete this dialog here; main() deletes it after the event loop returns.
        m_hideIcon->hide();
        event->ignore();
        qApp->quit();
        return;
    }

    this->hide();
    if (!Config::getInstance().getTrayMessageShown()) {
        Config::getInstance().setTrayMessageShown(true);
        m_hideIcon->showMessage(tr("Notice"),
                                tr("Hidden here!"),
                                QSystemTrayIcon::Information,
                                3000);
    }
    event->ignore();
}

void Dialog::on_updateDevice_clicked()
{
    if (checkAdbRun()) {
        return;
    }
    outLog("update devices...", false);
    m_adb.execute("", QStringList() << "devices");
}

void Dialog::updateVideoSourceUi()
{
    const bool camera = ui->videoSourceBox->currentIndex() == qsc::VIDEO_SOURCE_CAMERA;
    ui->cameraFacingLabel->setEnabled(camera);
    ui->cameraFacingBox->setEnabled(camera);
    ui->refreshCameraBtn->setEnabled(camera);
    ui->closeScreenCheck->setEnabled(!camera);
    ui->stayAwakeCheck->setEnabled(!camera);
    ui->gameBox->setEnabled(!camera);
    ui->refreshGameScriptBtn->setEnabled(!camera);
    ui->applyScriptBtn->setEnabled(!camera);
    ui->profileBox->setEnabled(!camera);
    ui->saveProfileBtn->setEnabled(!camera);
    ui->importGameScriptBtn->setEnabled(!camera);
    ui->exportGameScriptBtn->setEnabled(!camera);
    updateEditScriptBtnState();
    ui->installSndcpyBtn->setEnabled(!camera);
    ui->startAudioBtn->setEnabled(!camera);
    if (m_advancedDisplayGroup) {
        m_advancedDisplayGroup->setEnabled(!camera);
    }
}

void Dialog::on_videoSourceBox_currentIndexChanged(int)
{
    updateVideoSourceUi();
}

void Dialog::on_startServerBtn_clicked()
{
    outLog("start server...", false);

    // this is ok that "original" toUshort is 0
    quint16 videoSize = ui->maxSizeBox->currentText().trimmed().toUShort();
    qsc::DeviceParams params;
    params.serial = ui->serialBox->currentText().trimmed();
    params.maxSize = videoSize;
    params.bitRate = getBitRate();
    // on devices with Android >= 10, the capture frame rate can be limited
    params.maxFps = static_cast<quint32>(Config::getInstance().getMaxFps());
    params.videoSource = static_cast<qsc::VideoSource>(ui->videoSourceBox->currentIndex());
    params.cameraFacing = ui->cameraFacingBox->currentIndex() == 1
            ? qsc::CAMERA_FACING_FRONT
            : qsc::CAMERA_FACING_BACK;
    params.cameraId = ui->cameraFacingBox->currentData().toString();
    const bool camera = params.videoSource == qsc::VIDEO_SOURCE_CAMERA;
    params.closeScreen = !camera && ui->closeScreenCheck->isChecked();
    params.useReverse = ui->useReverseCheck->isChecked();
    params.display = !ui->notDisplayCheck->isChecked();
    params.renderExpiredFrames = Config::getInstance().getRenderExpiredFrames();
    if (ui->lockOrientationBox->currentIndex() > 0) {
        params.captureOrientationLock = 1;
        params.captureOrientation = (ui->lockOrientationBox->currentIndex() - 1) * 90;
    }
    // Camera sensors expose their video stream in landscape by default. Rotate
    // the default camera preview to portrait, while preserving an explicit
    // orientation selected by the user.
    if (camera && params.captureOrientationLock == 0) {
        params.captureOrientation = 90;
    }
    params.stayAwake = !camera && ui->stayAwakeCheck->isChecked();
    params.recordFile = ui->recordScreenCheck->isChecked();
    params.recordPath = ui->recordPathEdt->text().trimmed();
    params.recordFileFormat = ui->formatBox->currentText().trimmed();
    params.serverLocalPath = getServerPath();
    params.serverRemotePath = Config::getInstance().getServerPath();
    params.pushFilePath = Config::getInstance().getPushFilePath();
    params.gameScript = camera ? QString() : getGameScript(ui->gameBox->currentText());
    params.logLevel = Config::getInstance().getLogLevel();
    // Apply an encoder preset (when a preset mode is selected) or the ini default.
    const int codecModeIndex = ui->codecModeBox->currentIndex();
    if (codecModeIndex > 0) {
        const EncoderPreset &preset = EncoderPresetRegistry::all().at(codecModeIndex - 1);
        // Restore from the open dialog's selection, or fall back to the saved tier.
        int level = 1;
        if (m_presetDialog) {
            level = m_presetDialog->selectedLevel();
        } else {
            level = Config::getInstance().getUserBootConfig().presetLevel;
        }
        QString codecOptions;
        quint32 bitRate = 0;
        preset.buildParams(level, codecOptions, bitRate);
        params.codecOptions = codecOptions;
        if (!preset.codecName.isEmpty()) {
            params.codecName = preset.codecName;
        }
        if (bitRate > 0) {
            params.bitRate = bitRate;
        }
    } else {
        params.codecOptions = Config::getInstance().getCodecOptions();
        params.codecName = Config::getInstance().getCodecName();
    }
    params.scid = QRandomGenerator::global()->bounded(1, 10000) & 0x7FFFFFFF;
    params.decodeMode = ui->decodeModeBox->currentIndex();
    // Disabled widgets may remain checked from persisted settings. Advanced
    // display options are not valid for camera capture, so never copy them
    // into camera session parameters.
    if (!camera && m_advancedDisplayGroup && m_advancedDisplayGroup->isChecked()) {
        const int displayMode = m_displayModeBox->currentIndex();
        if (displayMode == 1) {
            bool ok = false;
            const int displayId = m_displayIdEdit->text().trimmed().toInt(&ok);
            if (!ok || displayId < 0) {
                outLog(tr("invalid display ID"));
                return;
            }
            params.displayId = displayId;
        } else if (displayMode == 2) {
            params.newDisplay = m_newDisplayEdit->text().trimmed();
            if (params.newDisplay.isEmpty()) {
                // Match scrcpy's flex default (1280x960 at 160 dpi) while
                // keeping the command-line parameter explicit.
                params.newDisplay = "1280x960/160";
            }
            params.flexDisplay = m_flexDisplayCheck->isChecked();
            params.vdSystemDecorations = m_vdSystemDecorationsCheck->isChecked();
            params.vdDestroyContent = m_vdDestroyContentCheck->isChecked();
            params.displayImePolicy = m_displayImePolicyBox->currentData().toString();
        }
        params.keepActive = m_keepActiveCheck->isChecked();
        params.startApp = m_startAppBox->currentData().toString();
        if (params.startApp.isEmpty()) {
            params.startApp = m_startAppBox->currentText().trimmed();
        }
        // scrcpy forbids crop with flex display. Preserve the entered value
        // for a later non-flex session, but never pass it to the server.
        params.crop = params.flexDisplay ? QString() : m_cropEdit->text().trimmed();
    }
    if (params.flexDisplay && (params.newDisplay.isEmpty() || !params.display || !params.crop.isEmpty())) {
        outLog(tr("flex display requires video, a new virtual display, and no crop"));
        return;
    }

    const bool needsVirtualDisplayCheck = !params.newDisplay.isEmpty();
    if (!camera && !needsVirtualDisplayCheck) {
        qsc::IDeviceManage::getInstance().connectDevice(params);
        return;
    }

    auto *versionAdb = new qsc::AdbProcess(this);
    connect(versionAdb, &qsc::AdbProcess::adbProcessResult, this,
            [this, versionAdb, camera, params](qsc::AdbProcess::ADB_EXEC_RESULT result) {
        if (result == qsc::AdbProcess::AER_SUCCESS_EXEC) {
            bool ok = false;
            const int sdk = versionAdb->getStdOut().trimmed().toInt(&ok);
            const int minimumSdk = camera ? 31 : 29;
            if (ok && sdk >= minimumSdk) {
                qsc::IDeviceManage::getInstance().connectDevice(params);
            } else {
                outLog(camera ? tr("camera preview requires Android 12 or later")
                              : tr("virtual display requires Android 10 or later"));
            }
            versionAdb->deleteLater();
        } else if (result == qsc::AdbProcess::AER_ERROR_EXEC
                   || result == qsc::AdbProcess::AER_ERROR_START
                   || result == qsc::AdbProcess::AER_ERROR_MISSING_BINARY) {
            outLog(tr("could not verify Android version for camera preview"));
            versionAdb->deleteLater();
        }
    });
    versionAdb->execute(params.serial, QStringList() << "shell" << "getprop" << "ro.build.version.sdk");
}

void Dialog::on_refreshCameraBtn_clicked()
{
    const QString serial = ui->serialBox->currentText().trimmed();
    if (serial.isEmpty()) {
        outLog(tr("no device"));
        return;
    }
    if (qsc::IDeviceManage::getInstance().getDevice(serial)) {
        outLog(tr("stop preview first"));
        return;
    }

    ui->refreshCameraBtn->setEnabled(false);
    ui->refreshCameraBtn->setText("...");

    auto *pushAdb = new qsc::AdbProcess(this);
    connect(pushAdb, &qsc::AdbProcess::adbProcessResult, this,
            [this, pushAdb, serial](qsc::AdbProcess::ADB_EXEC_RESULT result) {
        if (result == qsc::AdbProcess::AER_SUCCESS_EXEC) {
            pushAdb->deleteLater();

            auto *listAdb = new qsc::AdbProcess(this);
            connect(listAdb, &qsc::AdbProcess::adbProcessResult, this,
                    [this, listAdb](qsc::AdbProcess::ADB_EXEC_RESULT listResult) {
                if (listResult == qsc::AdbProcess::AER_SUCCESS_START) {
                    return;
                }
                ui->refreshCameraBtn->setText(tr("refresh"));
                ui->refreshCameraBtn->setEnabled(ui->videoSourceBox->currentIndex() == qsc::VIDEO_SOURCE_CAMERA);

                if (listResult != qsc::AdbProcess::AER_SUCCESS_EXEC) {
                    outLog(tr("camera refresh failed"));
                    listAdb->deleteLater();
                    return;
                }

                const QString output = listAdb->getStdOut() + '\n' + listAdb->getErrorOut();
                const QRegularExpression pattern(R"(--camera-id=(\S+)\s+\(([^,\)]+))");
                QRegularExpressionMatchIterator matches = pattern.globalMatch(output);

                const int facingIndex = ui->cameraFacingBox->currentIndex();
                ui->cameraFacingBox->clear();
                ui->cameraFacingBox->addItem(tr("back"));
                ui->cameraFacingBox->addItem(tr("front"));

                int cameraCount = 0;
                while (matches.hasNext()) {
                    const QRegularExpressionMatch match = matches.next();
                    const QString cameraId = match.captured(1);
                    const QString facing = match.captured(2).trimmed();
                    ui->cameraFacingBox->addItem(QString("%1 (%2)").arg(cameraId, facing), cameraId);
                    ++cameraCount;
                }

                ui->cameraFacingBox->setCurrentIndex(qMin(facingIndex, ui->cameraFacingBox->count() - 1));
                outLog(cameraCount ? tr("camera refreshed") : tr("no camera"));
                listAdb->deleteLater();
            });

            QStringList args;
            args << "shell";
            args << QString("CLASSPATH=%1").arg(Config::getInstance().getServerPath());
            args << "app_process" << "/" << "com.genymobile.scrcpy.Server" << "4.1";
            args << "list_cameras=true" << "log_level=info";
            listAdb->execute(serial, args);
        } else if (result == qsc::AdbProcess::AER_ERROR_EXEC
                   || result == qsc::AdbProcess::AER_ERROR_START
                   || result == qsc::AdbProcess::AER_ERROR_MISSING_BINARY) {
            ui->refreshCameraBtn->setText(tr("refresh"));
            ui->refreshCameraBtn->setEnabled(ui->videoSourceBox->currentIndex() == qsc::VIDEO_SOURCE_CAMERA);
            outLog(tr("camera refresh failed"));
            pushAdb->deleteLater();
        }
    });
    pushAdb->push(serial, getServerPath(), Config::getInstance().getServerPath());
}

void Dialog::refreshApps()
{
    const QString serial = ui->serialBox->currentText().trimmed();
    if (serial.isEmpty()) {
        outLog(tr("no device"));
        return;
    }
    if (qsc::IDeviceManage::getInstance().getDevice(serial)) {
        outLog(tr("stop server first"));
        return;
    }

    m_refreshAppsBtn->setEnabled(false);
    m_refreshAppsBtn->setText("...");

    auto restoreRefreshButton = [this]() {
        m_refreshAppsBtn->setText(tr("refresh"));
        m_refreshAppsBtn->setEnabled(ui->videoSourceBox->currentIndex() == qsc::VIDEO_SOURCE_DISPLAY);
    };

    auto *pushAdb = new qsc::AdbProcess(this);
    connect(pushAdb, &qsc::AdbProcess::adbProcessResult, this,
            [this, pushAdb, serial, restoreRefreshButton](qsc::AdbProcess::ADB_EXEC_RESULT result) {
        if (result == qsc::AdbProcess::AER_SUCCESS_EXEC) {
            pushAdb->deleteLater();

            auto *listAdb = new qsc::AdbProcess(this);
            connect(listAdb, &qsc::AdbProcess::adbProcessResult, this,
                    [this, listAdb, restoreRefreshButton](qsc::AdbProcess::ADB_EXEC_RESULT listResult) {
                if (listResult == qsc::AdbProcess::AER_SUCCESS_START) {
                    return;
                }
                restoreRefreshButton();

                if (listResult != qsc::AdbProcess::AER_SUCCESS_EXEC) {
                    outLog(tr("app refresh failed"));
                    listAdb->deleteLater();
                    return;
                }

                const QString selected = m_startAppBox->currentData().toString().isEmpty()
                        ? m_startAppBox->currentText().trimmed()
                        : m_startAppBox->currentData().toString();
                const QString output = listAdb->getStdOut() + '\n' + listAdb->getErrorOut();
                const QRegularExpression pattern(
                        R"(^\s*[\*\-]\s+(.+?)\s+([A-Za-z0-9_]+(?:\.[A-Za-z0-9_]+)+)\s*$)",
                        QRegularExpression::MultilineOption);
                QRegularExpressionMatchIterator matches = pattern.globalMatch(output);

                m_startAppBox->clear();
                m_startAppBox->addItem(QString(), QString());
                int appCount = 0;
                while (matches.hasNext()) {
                    const QRegularExpressionMatch match = matches.next();
                    const QString name = match.captured(1).trimmed();
                    const QString packageName = match.captured(2);
                    m_startAppBox->addItem(QString("%1 (%2)").arg(name, packageName), packageName);
                    ++appCount;
                }

                const int selectedIndex = m_startAppBox->findData(selected);
                if (selectedIndex >= 0) {
                    m_startAppBox->setCurrentIndex(selectedIndex);
                } else {
                    m_startAppBox->setEditText(selected);
                }
                outLog(appCount ? tr("apps refreshed") : tr("no launchable app"));
                listAdb->deleteLater();
            });

            QStringList args;
            args << "shell";
            args << QString("CLASSPATH=%1").arg(Config::getInstance().getServerPath());
            args << "app_process" << "/" << "com.genymobile.scrcpy.Server" << "4.1";
            args << "list_apps=true" << "log_level=info";
            listAdb->execute(serial, args);
        } else if (result == qsc::AdbProcess::AER_ERROR_EXEC
                   || result == qsc::AdbProcess::AER_ERROR_START
                   || result == qsc::AdbProcess::AER_ERROR_MISSING_BINARY) {
            restoreRefreshButton();
            outLog(tr("app refresh failed"));
            pushAdb->deleteLater();
        }
    });
    pushAdb->push(serial, getServerPath(), Config::getInstance().getServerPath());
}

void Dialog::on_stopServerBtn_clicked()
{
    if (qsc::IDeviceManage::getInstance().disconnectDevice(ui->serialBox->currentText().trimmed())) {
        outLog("stop server");
    }
}

void Dialog::on_wirelessConnectBtn_clicked()
{
    if (checkAdbRun()) {
        return;
    }
    QString addr = ui->deviceIpEdt->currentText().trimmed();
    if (addr.isEmpty()) {
        outLog("error: device ip is null", false);
        return;
    }

    if (!ui->devicePortEdt->currentText().isEmpty()) {
        addr += ":";
        addr += ui->devicePortEdt->currentText().trimmed();
    } else if (!ui->devicePortEdt->lineEdit()->placeholderText().isEmpty()) {
        addr += ":";
        addr += ui->devicePortEdt->lineEdit()->placeholderText().trimmed();
    } else {
        outLog("error: device port is null", false);
        return;
    }

    // 保存IP历史记录 - 只保存IP部分,不包含端口
    QString ip = addr.split(":").first();
    if (!ip.isEmpty()) {
        saveIpHistory(ip);
    }
    
    // 保存端口历史记录
    QString port = addr.split(":").last();
    if (!port.isEmpty() && port != ip) {
        savePortHistory(port);
    }

    outLog("wireless connect...", false);
    QStringList adbArgs;
    adbArgs << "connect";
    adbArgs << addr;
    m_adb.execute("", adbArgs);
}

void Dialog::on_startAdbdBtn_clicked()
{
    if (checkAdbRun()) {
        return;
    }
    outLog("start devices adbd...", false);
    // adb tcpip 5555
    QStringList adbArgs;
    adbArgs << "tcpip";
    adbArgs << "5555";
    m_adb.execute(ui->serialBox->currentText().trimmed(), adbArgs);
}

void Dialog::outLog(const QString &log, bool newLine)
{
    // avoid sub thread update ui
    QString backLog = log;
    QTimer::singleShot(0, this, [this, backLog, newLine]() {
        ui->outEdit->append(backLog);
        if (newLine) {
            ui->outEdit->append("<br/>");
        }
    });
}

bool Dialog::filterLog(const QString &log)
{
    if (log.contains("app_proces")) {
        return true;
    }
    if (log.contains("Unable to set geometry")) {
        return true;
    }
    return false;
}

bool Dialog::checkAdbRun()
{
    if (m_adb.isRuning()) {
        outLog("wait for the end of the current command to run");
    }
    return m_adb.isRuning();
}

void Dialog::on_getIPBtn_clicked()
{
    if (checkAdbRun()) {
        return;
    }

    outLog("get ip...", false);
    // adb -s P7C0218510000537 shell ifconfig wlan0
    // or
    // adb -s P7C0218510000537 shell ip -f inet addr show wlan0
    QStringList adbArgs;
#if 0
    adbArgs << "shell";
    adbArgs << "ip";
    adbArgs << "-f";
    adbArgs << "inet";
    adbArgs << "addr";
    adbArgs << "show";
    adbArgs << "wlan0";
#else
    adbArgs << "shell";
    adbArgs << "ifconfig";
    adbArgs << "wlan0";
#endif
    m_adb.execute(ui->serialBox->currentText().trimmed(), adbArgs);
}

void Dialog::getIPbyIp()
{
    if (checkAdbRun()) {
        return;
    }

    QStringList adbArgs;
    adbArgs << "shell";
    adbArgs << "ip -o a";

    m_adb.execute(ui->serialBox->currentText().trimmed(), adbArgs);
}

void Dialog::onDeviceConnected(bool success, const QString &serial, const QString &deviceName, const QSize &size)
{
    Q_UNUSED(deviceName);
    if (!success) {
        return;
    }
    auto videoForm = new VideoForm(ui->framelessCheck->isChecked(), Config::getInstance().getSkin(), ui->showToolbar->isChecked(), ui->decodeModeBox->currentIndex());
    videoForm->setSerial(serial);

    qsc::IDeviceManage::getInstance().getDevice(serial)->setUserData(static_cast<void*>(videoForm));
    qsc::IDeviceManage::getInstance().getDevice(serial)->registerDeviceObserver(videoForm);


    videoForm->showFPS(ui->fpsCheck->isChecked());

    if (ui->alwaysTopCheck->isChecked()) {
        videoForm->staysOnTop();
    }

#ifndef Q_OS_WIN32
    // must be show before updateShowSize
    videoForm->show();
#endif
    QString name = Config::getInstance().getNickName(serial);
    if (name.isEmpty()) {
        name = Config::getInstance().getTitle();
    }
    videoForm->setWindowTitle(name + "-" + serial);
    videoForm->updateShowSize(size);
    // VenMirror v1.1.0: el mapa con el que arranca el espejo es el mapa
    // activo (lo muestra el overlay de F1).
    videoForm->setActiveKeymapJson(getGameScript(ui->gameBox->currentText()));

    bool deviceVer = size.height() > size.width();
    QRect rc = Config::getInstance().getRect(serial);
    bool rcVer = rc.height() > rc.width();
    // same width/height rate
    if (rc.isValid() && (deviceVer == rcVer)) {
        // mark: resize is for fix setGeometry magneticwidget bug
        videoForm->resize(rc.size());
        videoForm->setGeometry(rc);
    }

#ifdef Q_OS_WIN32
    // windows是show太早可以看到resize的过程
    QTimer::singleShot(200, videoForm, [videoForm](){videoForm->show();});
#endif

    GroupController::instance().addDevice(serial);
    updateEditScriptBtnState();
}

void Dialog::onDeviceDisconnected(QString serial)
{
    GroupController::instance().removeDevice(serial);
    auto device = qsc::IDeviceManage::getInstance().getDevice(serial);
    if (!device) {
        // The device is already gone: there is no video window to edit with.
        if (serial == ui->serialBox->currentText().trimmed()) {
            ui->editScriptBtn->setEnabled(false);
        }
        return;
    }
    auto data = device->getUserData();
    if (data) {
        VideoForm* vf = static_cast<VideoForm*>(data);
        qsc::IDeviceManage::getInstance().getDevice(serial)->deRegisterDeviceObserver(vf);
        vf->close();
        vf->deleteLater();
    }
    // Do not use updateEditScriptBtnState() here: disconnect does not clear
    // the device userData, so the (closing) VideoForm pointer would still
    // look valid. Disable directly when the disconnected device is selected.
    if (serial == ui->serialBox->currentText().trimmed()) {
        ui->editScriptBtn->setEnabled(false);
    }
}

void Dialog::on_wirelessDisConnectBtn_clicked()
{
    if (checkAdbRun()) {
        return;
    }
    QString addr = ui->deviceIpEdt->currentText().trimmed();
    outLog("wireless disconnect...", false);
    QStringList adbArgs;
    adbArgs << "disconnect";
    adbArgs << addr;
    m_adb.execute("", adbArgs);
}

void Dialog::on_selectRecordPathBtn_clicked()
{
    QFileDialog::Options options = QFileDialog::DontResolveSymlinks | QFileDialog::ShowDirsOnly;
    QString directory = QFileDialog::getExistingDirectory(this, tr("select path"), "", options);
    ui->recordPathEdt->setText(directory);
}

void Dialog::on_recordPathEdt_textChanged(const QString &arg1)
{
    ui->recordPathEdt->setToolTip(arg1.trimmed());
    ui->notDisplayCheck->setCheckable(!arg1.trimmed().isEmpty());
}

void Dialog::on_adbCommandBtn_clicked()
{
    execAdbCmd();
}

void Dialog::on_stopAdbBtn_clicked()
{
    m_adb.kill();
}

void Dialog::on_clearOut_clicked()
{
    ui->outEdit->clear();
}

void Dialog::on_stopAllServerBtn_clicked()
{
    qsc::IDeviceManage::getInstance().disconnectAllDevice();
}

void Dialog::on_refreshGameScriptBtn_clicked()
{
    ui->gameBox->clear();
    initializeKeyMapDirectory();
    const QDir userDir(getKeyMapPath());
    const QDir defaultDir(getDefaultKeyMapPath());
    if (!userDir.exists() && !defaultDir.exists()) {
        outLog("keymap directory not find", true);
        return;
    }
    QSet<QString> fileNames;
    const QList<QDir> directories {userDir, defaultDir};
    for (const QDir &dir : directories) {
        const QFileInfoList list = dir.entryInfoList(QStringList() << "*.json", QDir::Files | QDir::NoSymLinks);
        for (const QFileInfo &fileInfo : list) {
            if (fileInfo.fileName() == "profiles.json") {
                // No es un mapa: es la lista de perfiles por juego.
                continue;
            }
            if (!fileNames.contains(fileInfo.fileName())) {
                fileNames.insert(fileInfo.fileName());
                ui->gameBox->addItem(fileInfo.fileName());
            }
        }
    }
    updateEditScriptBtnState();
    // Recargar también los perfiles (un mapa importado puede traer otro).
    refreshProfiles();
}

void Dialog::on_applyScriptBtn_clicked()
{
    auto curSerial = ui->serialBox->currentText().trimmed();
    auto device = qsc::IDeviceManage::getInstance().getDevice(curSerial);
    if (!device) {
        return;
    }

    const QString json = getGameScript(ui->gameBox->currentText());
    device->updateScript(json);
    // VenMirror v1.1.0: la ventana de vídeo guarda el mapa activo para
    // mostrarlo con F1 mantenida.
    auto *videoForm = static_cast<VideoForm *>(device->getUserData());
    if (videoForm) {
        videoForm->setActiveKeymapJson(json);
    }
}

void Dialog::onKeymapSaved(const QString &fileName)
{
    // El editor guardó con un nombre nuevo: la lista de mapas no lo
    // tenía; recargarla y seleccionar el archivo nuevo.
    on_refreshGameScriptBtn_clicked();
    const int index = ui->gameBox->findText(fileName);
    if (index >= 0) {
        ui->gameBox->setCurrentIndex(index);
    }
}

void Dialog::updateEditScriptBtnState()
{
    bool enabled = false;
    if (!ui->gameBox->currentText().isEmpty()
        && ui->videoSourceBox->currentIndex() != qsc::VIDEO_SOURCE_CAMERA) {
        const QString serial = ui->serialBox->currentText().trimmed();
        auto device = qsc::IDeviceManage::getInstance().getDevice(serial);
        if (device) {
            enabled = device->getUserData() != nullptr;
        }
    }
    ui->editScriptBtn->setEnabled(enabled);
}

void Dialog::on_gameBox_currentIndexChanged(const QString &arg1)
{
    Q_UNUSED(arg1);
    updateEditScriptBtnState();
    if (m_loadingProfiles || arg1.isEmpty()) {
        return;
    }
    // Sincronizar el selector de perfiles con el mapa elegido: el dato
    // del ítem es el mapa del perfil; en modo fallback el texto ya es
    // el nombre del mapa.
    m_loadingProfiles = true;
    int profileIndex = -1;
    for (int i = 0; i < ui->profileBox->count(); ++i) {
        if (ui->profileBox->itemData(i).toString() == arg1 || ui->profileBox->itemText(i) == arg1) {
            profileIndex = i;
            break;
        }
    }
    if (profileIndex >= 0) {
        ui->profileBox->setCurrentIndex(profileIndex);
    }
    m_loadingProfiles = false;
}

void Dialog::on_editScriptBtn_clicked()
{
    QString fileName = ui->gameBox->currentText();
    if (fileName.isEmpty()) {
        return;
    }

    const QString serial = ui->serialBox->currentText().trimmed();
    auto device = qsc::IDeviceManage::getInstance().getDevice(serial);
    if (!device) {
        qInfo() << "No hay ningún dispositivo conectado para editar el mapa de teclas";
        return;
    }

    VideoForm *videoForm = static_cast<VideoForm*>(device->getUserData());
    if (!videoForm) {
        qInfo() << "No hay ninguna ventana de vídeo activa para editar el mapa de teclas";
        return;
    }

    // User keymap dir: getGameScript() reads this dir first, so saving the
    // edited map here is the copy the editor and the apply path both use.
    const QString filePath = getKeyMapPath() + "/" + fileName;
    const QString json = getGameScript(fileName);
    // VenMirror v1.1.0: si el editor guarda con un nombre nuevo, avisar
    // para refrescar la lista de mapas (UniqueConnection: el editor se
    // abre muchas veces sobre la misma ventana de vídeo).
    connect(videoForm, &VideoForm::keymapSaved, this, &Dialog::onKeymapSaved, Qt::UniqueConnection);
    videoForm->openKeymapEditor(filePath, json);
}

void Dialog::on_profileBox_currentIndexChanged(const QString &arg1)
{
    if (m_loadingProfiles || arg1.isEmpty()) {
        return;
    }
    QString keymap;
    for (const GameProfile &profile : m_profiles) {
        if (profile.name == arg1) {
            keymap = profile.keymap;
            break;
        }
    }
    if (keymap.isEmpty()) {
        // Modo fallback (sin profiles.json): el texto ya es el mapa.
        keymap = arg1;
    }
    int gameIndex = ui->gameBox->findText(keymap);
    if (gameIndex < 0) {
        // El mapa no estaba en la lista (p. ej. la lista aún no se
        // cargó): recargarla una vez y reintentar.
        on_refreshGameScriptBtn_clicked();
        gameIndex = ui->gameBox->findText(keymap);
    }
    if (gameIndex < 0) {
        QMessageBox::warning(this, "Perfil",
                             QString("No se encontró el mapa de teclas \"%1\".").arg(keymap));
        return;
    }
    m_loadingProfiles = true;
    ui->gameBox->setCurrentIndex(gameIndex);
    m_loadingProfiles = false;
    // Mismo camino que el botón Aplicar.
    on_applyScriptBtn_clicked();
}

void Dialog::on_saveProfileBtn_clicked()
{
    const QString fileName = ui->gameBox->currentText();
    if (fileName.isEmpty()) {
        QMessageBox::warning(this, "Guardar perfil", "Primero elige un mapa de teclas.");
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Guardar perfil", "Nombre del perfil:",
                                               QLineEdit::Normal, QFileInfo(fileName).baseName(), &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }
    QString existingPackage;
    for (const GameProfile &profile : m_profiles) {
        if (profile.name == name.trimmed()) {
            existingPackage = profile.package;
            break;
        }
    }
    const QString package = QInputDialog::getText(this, "Guardar perfil", "Paquete del juego (opcional):",
                                                  QLineEdit::Normal, existingPackage, &ok);
    if (!ok) {
        return;
    }
    bool found = false;
    for (GameProfile &profile : m_profiles) {
        if (profile.name == name.trimmed()) {
            profile.keymap = fileName;
            profile.package = package.trimmed();
            found = true;
            break;
        }
    }
    if (!found) {
        GameProfile profile;
        profile.name = name.trimmed();
        profile.keymap = fileName;
        profile.package = package.trimmed();
        m_profiles.append(profile);
    }
    if (!saveProfiles()) {
        QMessageBox::warning(this, "Guardar perfil",
                             "No se pudo escribir profiles.json en la carpeta de mapas.");
        return;
    }
    refreshProfiles();
    const int index = ui->profileBox->findText(name.trimmed());
    if (index >= 0) {
        m_loadingProfiles = true;
        ui->profileBox->setCurrentIndex(index);
        m_loadingProfiles = false;
    }
    outLog(QString("Perfil guardado: %1 -> %2").arg(name.trimmed(), fileName), true);
}

void Dialog::on_importGameScriptBtn_clicked()
{
    const QString source = QFileDialog::getOpenFileName(this, "Importar mapa de teclas", QString(),
                                                        "Mapas de teclas (*.json)");
    if (source.isEmpty()) {
        return;
    }
    const QString fileName = QFileInfo(source).fileName();
    const QString dest = getKeyMapPath() + "/" + fileName;
    if (QFileInfo(source).absoluteFilePath() == QFileInfo(dest).absoluteFilePath()) {
        // Ya está en la carpeta de mapas: solo refrescar la lista.
        on_refreshGameScriptBtn_clicked();
        return;
    }
    QFile checkFile(source);
    if (!checkFile.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Importar", "No se pudo leer el archivo elegido.");
        return;
    }
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(checkFile.readAll(), &parseError);
    checkFile.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()
        || !doc.object().contains("keyMapNodes")) {
        QMessageBox::warning(this, "Importar",
                             "El archivo no es un mapa de teclas válido (falta \"keyMapNodes\").");
        return;
    }
    if (QFileInfo::exists(dest)) {
        if (QMessageBox::question(this, "Importar",
                                  QString("Ya existe un mapa llamado \"%1\". ¿Sobrescribirlo?").arg(fileName),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
        QFile::remove(dest);
    }
    if (!QFile::copy(source, dest)) {
        QMessageBox::warning(this, "Importar", "No se pudo copiar el archivo a la carpeta de mapas.");
        return;
    }
    on_refreshGameScriptBtn_clicked();
    const int index = ui->gameBox->findText(fileName);
    if (index >= 0) {
        ui->gameBox->setCurrentIndex(index);
    }
    outLog(QString("Mapa importado: %1").arg(fileName), true);
}

void Dialog::on_exportGameScriptBtn_clicked()
{
    const QString fileName = ui->gameBox->currentText();
    if (fileName.isEmpty()) {
        QMessageBox::warning(this, "Exportar", "Primero elige un mapa de teclas.");
        return;
    }
    const QString source = getGameScriptPath(fileName);
    if (source.isEmpty()) {
        QMessageBox::warning(this, "Exportar", QString("No se encontró el mapa \"%1\".").arg(fileName));
        return;
    }
    const QString dest = QFileDialog::getSaveFileName(this, "Exportar mapa de teclas", fileName,
                                                      "Mapas de teclas (*.json)");
    if (dest.isEmpty()) {
        return;
    }
    if (QFileInfo(dest).absoluteFilePath() == QFileInfo(source).absoluteFilePath()) {
        QMessageBox::information(this, "Exportar", "El archivo ya está en esa ubicación.");
        return;
    }
    if (QFileInfo::exists(dest)) {
        if (QMessageBox::question(this, "Exportar",
                                  QString("Ya existe \"%1\". ¿Sobrescribirlo?").arg(QFileInfo(dest).fileName()),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
        QFile::remove(dest);
    }
    if (!QFile::copy(source, dest)) {
        QMessageBox::warning(this, "Exportar", "No se pudo exportar el mapa de teclas.");
        return;
    }
    outLog(QString("Mapa exportado: %1").arg(dest), true);
}

void Dialog::on_recordScreenCheck_clicked(bool checked)
{
    if (!checked) {
        return;
    }

    QString fileDir(ui->recordPathEdt->text().trimmed());
    if (fileDir.isEmpty()) {
        qWarning() << "please select record save path!!!";
        ui->recordScreenCheck->setChecked(false);
    }
}

void Dialog::on_usbConnectBtn_clicked()
{
    on_stopAllServerBtn_clicked();
    delayMs(200);
    on_updateDevice_clicked();
    delayMs(200);

    int firstUsbDevice = findDeviceFromeSerialBox(false);
    if (-1 == firstUsbDevice) {
        qWarning() << "No use device is found!";
        return;
    }
    ui->serialBox->setCurrentIndex(firstUsbDevice);

    on_startServerBtn_clicked();
}

int Dialog::findDeviceFromeSerialBox(bool wifi)
{
    QString regStr = "\\b(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\:([0-9]|[1-9]\\d|[1-9]\\d{2}|[1-9]\\d{3}|[1-5]\\d{4}|6[0-4]\\d{3}|65[0-4]\\d{2}|655[0-2]\\d|6553[0-5])\\b";
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QRegExp regIP(regStr);
#else
    QRegularExpression regIP(regStr);
#endif
    for (int i = 0; i < ui->serialBox->count(); ++i) {
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        bool isWifi = regIP.exactMatch(ui->serialBox->itemText(i));
#else
        bool isWifi = regIP.match(ui->serialBox->itemText(i)).hasMatch();
#endif
        bool found = wifi ? isWifi : !isWifi;
        if (found) {
            return i;
        }
    }

    return -1;
}

void Dialog::on_wifiConnectBtn_clicked()
{
    on_stopAllServerBtn_clicked();
    delayMs(200);

    on_updateDevice_clicked();
    delayMs(200);

    int firstUsbDevice = findDeviceFromeSerialBox(false);
    if (-1 == firstUsbDevice) {
        qWarning() << "No use device is found!";
        return;
    }
    ui->serialBox->setCurrentIndex(firstUsbDevice);

    on_getIPBtn_clicked();
    delayMs(200);

    on_startAdbdBtn_clicked();
    delayMs(1000);

    on_wirelessConnectBtn_clicked();
    delayMs(2000);

    on_updateDevice_clicked();
    delayMs(200);

    int firstWifiDevice = findDeviceFromeSerialBox(true);
    if (-1 == firstWifiDevice) {
        qWarning() << "No wifi device is found!";
        return;
    }
    ui->serialBox->setCurrentIndex(firstWifiDevice);

    on_startServerBtn_clicked();
}

void Dialog::on_connectedPhoneList_itemDoubleClicked(QListWidgetItem *item)
{
    Q_UNUSED(item);
    ui->serialBox->setCurrentIndex(ui->connectedPhoneList->currentRow());
    on_startServerBtn_clicked();
}

void Dialog::on_updateNameBtn_clicked()
{
    if (ui->serialBox->count() != 0) {
        if (ui->userNameEdt->text().isEmpty()) {
            Config::getInstance().setNickName(ui->serialBox->currentText(), "Phone");
        } else {
            Config::getInstance().setNickName(ui->serialBox->currentText(), ui->userNameEdt->text());
        }

        on_updateDevice_clicked();

        qDebug() << "Update OK!";
    } else {
        qWarning() << "No device is connected!";
    }
}

void Dialog::on_useSingleModeCheck_clicked()
{
    if (ui->useSingleModeCheck->isChecked()) {
        ui->rightWidget->hide();
    } else {
        ui->rightWidget->show();
    }

    adjustSize();
}

void Dialog::on_serialBox_currentIndexChanged(const QString &arg1)
{
    ui->userNameEdt->setText(Config::getInstance().getNickName(arg1));
}

quint32 Dialog::getBitRate()
{
    return ui->bitRateEdit->text().trimmed().toUInt() *
            (ui->bitRateBox->currentText() == QString("Mbps") ? 1000000 : 1000);
}

const QString &Dialog::getServerPath()
{
    static QString serverPath;
    if (serverPath.isEmpty()) {
        serverPath = QString::fromLocal8Bit(qgetenv("QTSCRCPY_SERVER_PATH"));
        QFileInfo fileInfo(serverPath);
        if (serverPath.isEmpty() || !fileInfo.isFile()) {
            serverPath = QCoreApplication::applicationDirPath() + "/scrcpy-server";
        }
    }
    return serverPath;
}

void Dialog::on_startAudioBtn_clicked()
{
    if (ui->serialBox->count() == 0) {
        qWarning() << "No device is connected!";
        return;
    }

    m_audioOutput.start(ui->serialBox->currentText(), 28200);
}

void Dialog::on_stopAudioBtn_clicked()
{
    m_audioOutput.stop();
}

void Dialog::on_installSndcpyBtn_clicked()
{
    if (ui->serialBox->count() == 0) {
        qWarning() << "No device is connected!";
        return;
    }
    m_audioOutput.installonly(ui->serialBox->currentText(), 28200);
}

void Dialog::on_autoUpdatecheckBox_toggled(bool checked)
{
    if (checked) {
        m_autoUpdatetimer.start(5000);
    } else {
        m_autoUpdatetimer.stop();
    }
}

void Dialog::loadIpHistory()
{
    QStringList ipList = Config::getInstance().getIpHistory();
    ui->deviceIpEdt->clear();
    ui->deviceIpEdt->addItems(ipList);

    if (ui->deviceIpEdt->lineEdit()) {
        ui->deviceIpEdt->lineEdit()->setFrame(false);
        ui->deviceIpEdt->lineEdit()->setTextMargins(0, 0, 0, 0);
        ui->deviceIpEdt->lineEdit()->setMaxLength(128);
        ui->deviceIpEdt->lineEdit()->setPlaceholderText("192.168.0.1");
    }
}

void Dialog::saveIpHistory(const QString &ip)
{
    if (ip.isEmpty()) {
        return;
    }
    
    Config::getInstance().saveIpHistory(ip);
    
    // 更新ComboBox
    loadIpHistory();
    ui->deviceIpEdt->setCurrentText(ip);
}

void Dialog::showIpEditMenu(const QPoint &pos)
{
    QMenu *menu = ui->deviceIpEdt->lineEdit()->createStandardContextMenu();
    menu->addSeparator();
    
    QAction *clearHistoryAction = new QAction(tr("Clear History"), menu);
    connect(clearHistoryAction, &QAction::triggered, this, [this]() {
        Config::getInstance().clearIpHistory();
        loadIpHistory();
    });
    
    menu->addAction(clearHistoryAction);
    menu->exec(ui->deviceIpEdt->lineEdit()->mapToGlobal(pos));
    delete menu;
}

void Dialog::loadPortHistory()
{
    QStringList portList = Config::getInstance().getPortHistory();
    ui->devicePortEdt->clear();
    ui->devicePortEdt->addItems(portList);

    if (ui->devicePortEdt->lineEdit()) {
        ui->devicePortEdt->lineEdit()->setFrame(false);
        ui->devicePortEdt->lineEdit()->setTextMargins(0, 0, 0, 0);
        ui->devicePortEdt->lineEdit()->setMaxLength(6);
        ui->devicePortEdt->lineEdit()->setPlaceholderText("5555");
    }
}

void Dialog::savePortHistory(const QString &port)
{
    if (port.isEmpty()) {
        return;
    }
    
    Config::getInstance().savePortHistory(port);
    
    // 更新ComboBox
    loadPortHistory();
    ui->devicePortEdt->setCurrentText(port);
}

void Dialog::showPortEditMenu(const QPoint &pos)
{
    QMenu *menu = ui->devicePortEdt->lineEdit()->createStandardContextMenu();
    menu->addSeparator();
    
    QAction *clearHistoryAction = new QAction(tr("Clear History"), menu);
    connect(clearHistoryAction, &QAction::triggered, this, [this]() {
        Config::getInstance().clearPortHistory();
        loadPortHistory();
    });
    
    menu->addAction(clearHistoryAction);
    menu->exec(ui->devicePortEdt->lineEdit()->mapToGlobal(pos));
    delete menu;
}


void Dialog::on_codecModeBox_currentIndexChanged(int index)
{
    // Block signals while updating bitrate to avoid recursive calls
    ui->bitRateEdit->blockSignals(true);
    ui->bitRateBox->blockSignals(true);

    const EncoderPreset *preset = nullptr;
    if (index > 0) {
        preset = &EncoderPresetRegistry::all().at(index - 1);
    }

    if (index == 0) {
        // Default: restore previous settings
        if (m_prevBitRate == 0) {
            ui->bitRateEdit->setText("2");
            ui->bitRateBox->setCurrentText("Mbps");
        } else if (m_prevBitRate % 1000000 == 0) {
            ui->bitRateEdit->setText(QString::number(m_prevBitRate / 1000000));
            ui->bitRateBox->setCurrentText("Mbps");
        } else {
            ui->bitRateEdit->setText(QString::number(m_prevBitRate / 1000));
            ui->bitRateBox->setCurrentText("Kbps");
        }
        // Restore previous max size
        ui->maxSizeBox->setCurrentIndex(m_prevMaxSizeIndex);
    } else {
        // Preset mode: save current settings for Default-mode restoration
        m_prevBitRate = getBitRate();
        m_prevMaxSizeIndex = ui->maxSizeBox->currentIndex();

        // Sync bitrate & maxSize to match the saved encoder tier
        UserBootConfig config = Config::getInstance().getUserBootConfig();
        int level = config.presetLevel;
        quint32 bitrate = preset->levelBitRate(level);
        if (bitrate > 0) {
            if (bitrate % 1000000 == 0) {
                ui->bitRateEdit->setText(QString::number(bitrate / 1000000));
                ui->bitRateBox->setCurrentText("Mbps");
            } else {
                ui->bitRateEdit->setText(QString::number(bitrate / 1000));
                ui->bitRateBox->setCurrentText("Kbps");
            }
        }
        int maxSizeIdx = preset->levelMaxSizeIndex(level);
        if (maxSizeIdx >= 0) {
            ui->maxSizeBox->setCurrentIndex(maxSizeIdx);
        }
    }

    // Show the encoder settings button only for presets with adjustable tiers
    ui->presetConfigBtn->setVisible(preset && preset->levelCount() >= 2);

    ui->bitRateEdit->blockSignals(false);
    ui->bitRateBox->blockSignals(false);
}

void Dialog::on_presetConfigBtn_clicked()
{
    const int codecModeIndex = ui->codecModeBox->currentIndex();
    if (codecModeIndex <= 0) {
        return;
    }
    const EncoderPreset &preset = EncoderPresetRegistry::all().at(codecModeIndex - 1);

    // (Re)create the dialog when the selected preset changes.
    if (!m_presetDialog || m_presetDialog->presetId() != preset.id) {
        m_presetDialog = new PresetConfigDialog(preset, this);
        // Only restore the saved tier on first creation — subsequent opens
        // preserve the user's last selection in the dialog.
        UserBootConfig bootConfig = Config::getInstance().getUserBootConfig();
        m_presetDialog->setLevel(bootConfig.presetLevel);

        // Sync bitrate & maxSize UI in real-time as the user switches tiers.
        connect(m_presetDialog.data(), &PresetConfigDialog::levelChanged,
                this, &Dialog::syncPresetLevelToUi);
    }

    m_presetDialog->exec();

    // Sync after dialog closes — covers the case where the initial tier was
    // restored via setLevel() above and no levelChanged signal fired.
    syncPresetLevelToUi();
}

void Dialog::syncPresetLevelToUi()
{
    const int codecModeIndex = ui->codecModeBox->currentIndex();
    if (codecModeIndex <= 0 || !m_presetDialog) {
        return;
    }
    const EncoderPreset &preset = EncoderPresetRegistry::all().at(codecModeIndex - 1);
    int level = m_presetDialog->selectedLevel();

    // Update bitrate
    quint32 bitrate = preset.levelBitRate(level);
    if (bitrate > 0) {
        ui->bitRateEdit->blockSignals(true);
        ui->bitRateBox->blockSignals(true);
        if (bitrate % 1000000 == 0) {
            ui->bitRateEdit->setText(QString::number(bitrate / 1000000));
            ui->bitRateBox->setCurrentText("Mbps");
        } else {
            ui->bitRateEdit->setText(QString::number(bitrate / 1000));
            ui->bitRateBox->setCurrentText("Kbps");
        }
        ui->bitRateEdit->blockSignals(false);
        ui->bitRateBox->blockSignals(false);
    }

    // Update max size (only when the tier prescribes one)
    int maxSizeIdx = preset.levelMaxSizeIndex(level);
    if (maxSizeIdx >= 0) {
        ui->maxSizeBox->setCurrentIndex(maxSizeIdx);
    }
}

// ---------------------------------------------------------------------------
// WiFi sin USB — VenMirror v1.1.0 (depuración inalámbrica, Android 11+).
//
// Dos caminos visibles y separados:
//   A) "WiFi por USB (primera vez)": lo que ya existía (cable, botón
//      "Start adbd" = `adb tcpip 5555`, y después `adb connect IP:5555`).
//      Sigue siendo el camino para Android 10 o anterior.
//   B) "WiFi sin USB" (este panel): emparejar con código de 6 dígitos
//      (`adb pair IP:puerto CÓDIGO`) o con código QR, y conectar
//      (`adb connect IP:puerto`). Sin cable en ningún momento.
// Descubrimiento sin escribir la IP: `adb mdns services` (el propio adb
// encuentra los servicios _adb-tls-connect._tcp y _adb._tcp de la red).
// Como respaldo, escaneo del puerto 5555 en la subred (solo teléfonos ya
// activados por tcpip). Nada de esto bloquea la UI: adb ya es asíncrono
// y el escaneo 5555 corre en un std::thread con el progreso en el log.
// Límite honesto: sin USB solo funciona en Android 11 o superior.
// ---------------------------------------------------------------------------

void Dialog::initWifiNoUsbUi()
{
    // El grupo por USB que ya existía queda como camino A, con su nombre
    // explícito. No se toca dialog.ui: todo se crea por código.
    ui->wirelessGroupBox->setTitle(tr("WiFi por USB (primera vez)"));

    m_wifiNoUsbGroup = new QGroupBox(tr("WiFi sin USB (depuración inalámbrica, Android 11+)"), this);
    auto *form = new QFormLayout(m_wifiNoUsbGroup);

    m_wifiIpEdit = new QLineEdit(m_wifiNoUsbGroup);
    m_wifiIpEdit->setPlaceholderText("192.168.0.1");
    m_wifiIpEdit->setMaxLength(15);
    if (!ui->deviceIpEdt->currentText().trimmed().isEmpty()) {
        m_wifiIpEdit->setText(ui->deviceIpEdt->currentText().trimmed());
    }
    form->addRow(tr("IP del teléfono:"), m_wifiIpEdit);

    m_wifiPairPortEdit = new QLineEdit(m_wifiNoUsbGroup);
    m_wifiPairPortEdit->setPlaceholderText(tr("p. ej. 37099"));
    m_wifiPairPortEdit->setMaxLength(5);
    form->addRow(tr("Puerto de emparejamiento:"), m_wifiPairPortEdit);

    m_wifiCodeEdit = new QLineEdit(m_wifiNoUsbGroup);
    m_wifiCodeEdit->setPlaceholderText("123456");
    m_wifiCodeEdit->setMaxLength(6);
    form->addRow(tr("Código (6 dígitos):"), m_wifiCodeEdit);

    m_wifiConnectPortEdit = new QLineEdit(m_wifiNoUsbGroup);
    m_wifiConnectPortEdit->setPlaceholderText(tr("p. ej. 39543"));
    m_wifiConnectPortEdit->setMaxLength(5);
    form->addRow(tr("Puerto de conexión:"), m_wifiConnectPortEdit);

    auto *buttons = new QHBoxLayout();
    m_wifiPairBtn = new QPushButton(tr("Emparejar"), m_wifiNoUsbGroup);
    m_wifiConnectWifiBtn = new QPushButton(tr("Conectar"), m_wifiNoUsbGroup);
    m_wifiScanBtn = new QPushButton(tr("Buscar en WiFi"), m_wifiNoUsbGroup);
    m_wifiQrBtn = new QPushButton(tr("Mostrar QR"), m_wifiNoUsbGroup);
    buttons->addWidget(m_wifiPairBtn);
    buttons->addWidget(m_wifiConnectWifiBtn);
    buttons->addWidget(m_wifiScanBtn);
    buttons->addWidget(m_wifiQrBtn);
    form->addRow(buttons);

    auto *hint = new QLabel(m_wifiNoUsbGroup);
    hint->setWordWrap(true);
    hint->setText(tr("En el teléfono: Ajustes > Opciones de desarrollador > Depuración inalámbrica > "
                     "«Emparejar dispositivo con código de emparejamiento» (o con código QR). "
                     "Sin USB, solo Android 11 o superior; en Android 10 o anterior usa «WiFi por USB»."));
    form->addRow(hint);

    connect(m_wifiPairBtn, &QPushButton::clicked, this, &Dialog::on_wifiPairBtn_clicked);
    connect(m_wifiConnectWifiBtn, &QPushButton::clicked, this, &Dialog::on_wifiConnectWifiBtn_clicked);
    connect(m_wifiScanBtn, &QPushButton::clicked, this, &Dialog::on_wifiScanBtn_clicked);
    connect(m_wifiQrBtn, &QPushButton::clicked, this, &Dialog::on_wifiQrBtn_clicked);

    m_wifiMdnsTimer.setInterval(2000);
    connect(&m_wifiMdnsTimer, &QTimer::timeout, this, &Dialog::onWifiMdnsTimer);

    ui->verticalLayout_5->addWidget(m_wifiNoUsbGroup);
}

bool Dialog::wifiIsValidIp(const QString &ip)
{
    const QStringList parts = ip.split('.');
    if (parts.size() != 4) {
        return false;
    }
    for (const QString &part : parts) {
        if (part.isEmpty() || part.size() > 3) {
            return false;
        }
        bool ok = false;
        const int value = part.toInt(&ok);
        if (!ok || value < 0 || value > 255) {
            return false;
        }
        for (const QChar c : part) {
            if (!c.isDigit()) {
                return false;
            }
        }
    }
    return true;
}

QString Dialog::wifiRandomSecret(int length)
{
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    QString secret;
    secret.reserve(length);
    for (int i = 0; i < length; i++) {
        secret.append(chars[QRandomGenerator::global()->bounded(62)]);
    }
    return secret;
}

void Dialog::on_wifiPairBtn_clicked()
{
    if (checkAdbRun()) {
        return;
    }
    const QString ip = m_wifiIpEdit->text().trimmed();
    const QString pairPort = m_wifiPairPortEdit->text().trimmed();
    const QString code = m_wifiCodeEdit->text().trimmed();
    if (!wifiIsValidIp(ip)) {
        outLog("Escribe la IP del teléfono (p. ej. 192.168.0.1).");
        return;
    }
    bool ok = false;
    const int port = pairPort.toInt(&ok);
    if (!ok || port <= 0 || port > 65535) {
        outLog("Escribe el puerto de emparejamiento que muestra el teléfono.");
        return;
    }
    if (code.size() != 6) {
        outLog("El código de emparejamiento tiene 6 dígitos.");
        return;
    }
    for (const QChar c : code) {
        if (!c.isDigit()) {
            outLog("El código de emparejamiento tiene 6 dígitos.");
            return;
        }
    }
    saveIpHistory(ip);
    outLog(QString("Emparejando con %1:%2...").arg(ip, pairPort), false);
    m_adb.execute("", QStringList() << "pair" << (ip + ":" + pairPort) << code);
}

void Dialog::on_wifiConnectWifiBtn_clicked()
{
    if (checkAdbRun()) {
        return;
    }
    const QString ip = m_wifiIpEdit->text().trimmed();
    const QString port = m_wifiConnectPortEdit->text().trimmed();
    if (!wifiIsValidIp(ip)) {
        outLog("Escribe la IP del teléfono (p. ej. 192.168.0.1).");
        return;
    }
    bool ok = false;
    const int portNum = port.toInt(&ok);
    if (!ok || portNum <= 0 || portNum > 65535) {
        outLog("Escribe el puerto de conexión que muestra el teléfono.");
        return;
    }
    saveIpHistory(ip);
    wifiQueueConnect(ip + ":" + port);
}

void Dialog::on_wifiScanBtn_clicked()
{
    if (checkAdbRun()) {
        return;
    }
    m_wifiManualScan = true;
    outLog("Buscando teléfonos por WiFi (mDNS)...", false);
    m_adb.execute("", QStringList() << "mdns" << "services");
}

void Dialog::on_wifiQrBtn_clicked()
{
    if (checkAdbRun()) {
        return;
    }
    if (m_wifiQrDialog) {
        m_wifiQrDialog->close();
    }
    // El teléfono, al escanear el QR, publica este nombre como servicio
    // _adb-tls-pairing._tcp; adb lo encuentra con `adb mdns services` y
    // empareja con la contraseña. Formato de Android Studio (AOSP):
    // WIFI:T:ADB;S:<servicio>;P:<contraseña>;;
    // La contraseña solo lleva letras y dígitos (sin ';' ni ':' que
    // romperían el formato). El anuncio mDNS dura poco: el sondeo
    // empieza ANTES de mostrar el QR.
    m_wifiQrService = "studio-" + wifiRandomSecret(10);
    m_wifiQrPassword = wifiRandomSecret(10);
    const QString payload = QString("WIFI:T:ADB;S:%1;P:%2;;").arg(m_wifiQrService, m_wifiQrPassword);
    m_wifiQrPairing = true;
    m_wifiAfterPair = false;
    m_wifiMdnsPollsLeft = 60;  // 60 sondeos x 2 s = 2 minutos
    m_wifiMdnsTimer.start();
    wifiShowQrDialog(payload);
    outLog("Escanea el QR desde el teléfono: Depuración inalámbrica > Emparejar dispositivo con código QR.", false);
}

void Dialog::wifiShowQrDialog(const QString &payload)
{
    venmirror::QrMatrix qr;
    if (!venmirror::encodeQr(payload.toStdString(), qr)) {
        outLog("No se pudo generar el código QR.");
        m_wifiMdnsTimer.stop();
        m_wifiQrPairing = false;
        return;
    }
    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(tr("Emparejar por QR"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(dialog);
    auto *imageLabel = new QLabel(dialog);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setPixmap(QPixmap::fromImage(qrCodeImage(qr, 8)));
    layout->addWidget(imageLabel);
    auto *instructions = new QLabel(dialog);
    instructions->setWordWrap(true);
    instructions->setText(tr("En el teléfono: Ajustes > Opciones de desarrollador > Depuración inalámbrica > "
                             "«Emparejar dispositivo con código QR» y escanea este código. "
                             "El código caduca a los 2 minutos: si se cierra esta ventana, pulsa «Mostrar QR» otra vez."));
    layout->addWidget(instructions);
    auto *closeBtn = new QPushButton(tr("Cerrar"), dialog);
    layout->addWidget(closeBtn);
    connect(closeBtn, &QPushButton::clicked, dialog, &QDialog::close);
    connect(dialog, &QDialog::finished, this, &Dialog::onWifiQrDialogFinished);
    m_wifiQrDialog = dialog;
    m_wifiQrImageLabel = imageLabel;
    dialog->show();
    dialog->raise();
}

void Dialog::onWifiQrDialogFinished()
{
    m_wifiMdnsTimer.stop();
    m_wifiQrPairing = false;
    m_wifiQrDialog = nullptr;
    m_wifiQrImageLabel = nullptr;
}

void Dialog::onWifiMdnsTimer()
{
    if (m_adb.isRuning()) {
        return;  // este sondeo se salta; el siguiente lo intentará
    }
    if (m_wifiMdnsPollsLeft <= 0) {
        m_wifiMdnsTimer.stop();
        if (m_wifiQrPairing) {
            m_wifiQrPairing = false;
            outLog("Tiempo agotado: el teléfono no escaneó el QR. Pulsa «Mostrar QR» otra vez.");
        } else if (m_wifiAfterPair) {
            m_wifiAfterPair = false;
            outLog("Emparejado, pero no apareció el puerto de conexión. Espera unos segundos y pulsa «Buscar en WiFi».");
        }
        return;
    }
    m_wifiMdnsPollsLeft--;
    m_adb.execute("", QStringList() << "mdns" << "services");
}

void Dialog::wifiQueueConnect(const QString &addr)
{
    if (!m_wifiConnectQueue.contains(addr)) {
        m_wifiConnectQueue.append(addr);
    }
    wifiConnectNext();
}

void Dialog::wifiConnectNext()
{
    if (m_wifiConnectQueue.isEmpty()) {
        on_updateDevice_clicked();
        return;
    }
    if (checkAdbRun()) {
        QTimer::singleShot(500, this, &Dialog::wifiConnectNext);
        return;
    }
    const QString addr = m_wifiConnectQueue.takeFirst();
    outLog(QString("Conectando con %1 por WiFi...").arg(addr), false);
    m_adb.execute("", QStringList() << "connect" << addr);
}

void Dialog::wifiParseMdnsServices(const QString &output)
{
    // Formato de `adb mdns services` (AOSP):
    //   List of discovered mdns services
    //   adb-14141FDF600081 _adb._tcp             192.168.86.38:5555
    //   adb-14141FDF600081-QXjCrW _adb-tls-pairing._tcp 192.168.86.38:33861
    //   adb-14141FDF600081-TnSdi9 _adb-tls-connect._tcp 192.168.86.38:33015
    //   studio-g@<xeYnap/ _adb-tls-pairing._tcp 192.168.86.39:55861
    static const QRegularExpression lineRe(
        "^([^\\s]+)\\s+(_adb(?:-tls-(?:pairing|connect))?\\._tcp)\\s+(\\d+\\.\\d+\\.\\d+\\.\\d+:\\d+)\\s*$");
    QStringList connectAddrs;
    for (const QString &line : output.split('\n')) {
        const QRegularExpressionMatch match = lineRe.match(line.trimmed());
        if (!match.hasMatch()) {
            continue;
        }
        const QString instance = match.captured(1);
        const QString type = match.captured(2);
        const QString addr = match.captured(3);
        if (m_wifiQrPairing && instance == m_wifiQrService && type == "_adb-tls-pairing._tcp") {
            // El teléfono escaneó nuestro QR y publicó el servicio de
            // emparejamiento: emparejar con la contraseña del QR.
            m_wifiMdnsTimer.stop();
            m_wifiQrPairing = false;
            outLog(QString("Teléfono encontrado por QR (%1). Emparejando...").arg(addr), false);
            m_adb.execute("", QStringList() << "pair" << addr << m_wifiQrPassword);
            return;
        }
        if (type == "_adb-tls-connect._tcp" || type == "_adb._tcp") {
            if (!connectAddrs.contains(addr)) {
                connectAddrs.append(addr);
            }
        }
    }
    if (m_wifiAfterPair) {
        if (connectAddrs.isEmpty()) {
            // Si el temporizador sigue sondeando, esperar al siguiente
            // sondeo; si ya se agotó, onWifiMdnsTimer ya avisó.
            if (!m_wifiMdnsTimer.isActive()) {
                m_wifiAfterPair = false;
                outLog("Emparejado, pero no apareció el puerto de conexión. Espera unos segundos y pulsa «Buscar en WiFi».");
            }
            return;
        }
        m_wifiMdnsTimer.stop();
        m_wifiAfterPair = false;
        for (const QString &addr : connectAddrs) {
            wifiQueueConnect(addr);
        }
        return;
    }
    if (m_wifiManualScan) {
        m_wifiManualScan = false;
        if (connectAddrs.isEmpty()) {
            // Respaldo: escaneo del puerto 5555 en la subred (solo
            // teléfonos ya activados por tcpip).
            outLog("mDNS no encontró ningún teléfono. Probando con el escaneo del puerto 5555...");
            wifiStart5555Scan();
            return;
        }
        outLog(QString("Encontrados %1 teléfono(s) por WiFi: %2").arg(connectAddrs.size()).arg(connectAddrs.join(", ")));
        for (const QString &addr : connectAddrs) {
            wifiQueueConnect(addr);
        }
    }
}

void Dialog::wifiStart5555Scan()
{
    if (m_wifiScanning5555) {
        return;
    }
    m_wifiScanning5555 = true;
    outLog("Escaneando el puerto 5555 en la red local (esto tarda unos segundos)...", false);
    QPointer<Dialog> guard(this);
    std::thread([guard]() {
        QStringList found;
        const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
        for (const QHostAddress &local : addresses) {
            if (local.protocol() != QAbstractSocket::IPv4Protocol || local.isLoopback()) {
                continue;
            }
            const quint32 ip = local.toIPv4Address();
            if ((ip & 0xFFFF0000) == 0xA9FE0000) {  // 169.254.x.x (sin red)
                continue;
            }
            const quint32 base = ip & 0xFFFFFF00;
            for (int host = 1; host <= 254; host++) {
                const quint32 target = base | static_cast<quint32>(host);
                if (target == ip) {
                    continue;
                }
                QTcpSocket socket;
                socket.connectToHost(QHostAddress(target), 5555);
                if (socket.waitForConnected(200)) {
                    found.append(QHostAddress(target).toString() + ":5555");
                }
                socket.disconnectFromHost();
            }
        }
        // Si la ventana se cerró mientras escaneábamos, no hay nada
        // que notificar: el QPointer ya es nulo y no se invoca nada.
        if (guard) {
            QMetaObject::invokeMethod(guard, [guard, found]() {
                if (guard) {
                    guard->wifiFinish5555Scan(found);
                }
            }, Qt::QueuedConnection);
        }
    }).detach();
}

void Dialog::wifiFinish5555Scan(const QStringList &found)
{
    m_wifiScanning5555 = false;
    if (found.isEmpty()) {
        outLog("No se encontró ningún teléfono con el puerto 5555 abierto. "
               "Activa primero la depuración inalámbrica en el teléfono o conecta por USB una vez.");
        return;
    }
    outLog(QString("Encontrados %1 teléfono(s) con el puerto 5555: %2").arg(found.size()).arg(found.join(", ")));
    for (const QString &addr : found) {
        wifiQueueConnect(addr);
    }
}
