// #include <QDesktopWidget>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QKeySequence>
#include <QLabel>
#include <QLineF>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QShortcut>
#include <QStyle>
#include <QStyleOption>
#include <QTextStream>
#include <QTimer>
#include <QWindow>
#include <QtWidgets/QHBoxLayout>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

#if defined(Q_OS_WIN32)
#include <Windows.h>
#endif

#include "config.h"
#include "iconhelper.h"
#include "keymap.h"
#include "qyuvopenglwidget.h"
#include "toolform.h"
#include "mousetap/mousetap.h"
#include "ui_videoform.h"
#include "videoform.h"

#ifdef Q_OS_MACOS
#include "metalvideowindow.h"
#endif

VideoForm::VideoForm(bool framelessWindow, bool skin, bool showToolbar, int decodeMode, QWidget *parent) : QWidget(parent), ui(new Ui::videoForm), m_skin(skin), m_decodeMode(decodeMode)
{
    ui->setupUi(this);
    m_flexResizeTimer.setSingleShot(true);
    m_flexResizeTimer.setInterval(300);
    connect(&m_flexResizeTimer, &QTimer::timeout, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (device && device->isFlexDisplay() && !m_pendingDisplaySize.isEmpty()) {
            device->resizeDisplay(m_pendingDisplaySize);
        }
    });
    initUI();
    installShortcut();
    updateShowSize(size());
    bool vertical = size().height() > size().width();
    this->show_toolbar = showToolbar;
    if (m_skin) {
        updateStyleSheet(vertical);
    }
    if (framelessWindow) {
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    }

    // 监听显示器增删：副屏被拔掉时，位于该屏幕上的窗口会变成"幽灵窗口"
    // （坐标仍在已断开的屏幕区域）。这里在屏幕配置变化后做一次越界检查，
    // 必要时把窗口移回主屏。延迟执行是为了等 Qt/系统完成屏幕布局更新。
    //
    // Windows 上 screenRemoved 的触发时机与屏幕布局更新并不完全同步，
    // 因此做多次延迟重试，确保在布局稳定后仍能完成回收。
    connect(qApp, &QGuiApplication::screenAdded, this, [this]() {
        QTimer::singleShot(300, this, [this]() { ensureOnScreen(); });
        QTimer::singleShot(1000, this, [this]() { ensureOnScreen(); });
    });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this]() {
        QTimer::singleShot(300, this, [this]() { ensureOnScreen(); });
        QTimer::singleShot(1000, this, [this]() { ensureOnScreen(); });
        QTimer::singleShot(2000, this, [this]() { ensureOnScreen(); });
    });
}

VideoForm::~VideoForm()
{
    delete ui;
}

bool VideoForm::isMetalMode() const
{
#ifdef Q_OS_MACOS
    return !m_metalWidget.isNull();
#else
    return false;
#endif
}

QWidget* VideoForm::videoWidget() const
{
#ifdef Q_OS_MACOS
    if (isMetalMode()) {
        return m_metalWidget.data();
    }
#endif
    return m_videoWidget.data();
}

void VideoForm::initUI()
{
    if (m_skin) {
        QPixmap phone;
        if (phone.load(":/res/phone.png")) {
            m_widthHeightRatio = 1.0f * phone.width() / phone.height();
        }

#ifndef Q_OS_MACOS
        // mac下去掉标题栏影响showfullscreen
        // 去掉标题栏
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        // 根据图片构造异形窗口
        setAttribute(Qt::WA_TranslucentBackground);
#endif
    }

#ifdef Q_OS_MACOS
    // Apple Silicon: 使用 VideoToolbox + Metal 渲染
    if (m_decodeMode == 1) {
        m_metalWidget = new MetalVideoWidget();
        ui->keepRatioWidget->setWidget(m_metalWidget);

        // FPS label 作为 Metal widget 的子控件
        m_fpsLabel = new QLabel(m_metalWidget);
    } else
#endif
    {
        // OpenGL 路径（原有逻辑）
        m_videoWidget = new QYUVOpenGLWidget();
        m_videoWidget->hide();
        ui->keepRatioWidget->setWidget(m_videoWidget);

        // FPS label 作为 OpenGL widget 的子控件
        m_fpsLabel = new QLabel(m_videoWidget);
    }

    ui->keepRatioWidget->setWidthHeightRatio(m_widthHeightRatio);

    QFont ft;
    ft.setPointSize(15);
    ft.setWeight(QFont::Light);
    ft.setBold(true);
    m_fpsLabel->setFont(ft);
    m_fpsLabel->move(5, 15);
    m_fpsLabel->setMinimumWidth(100);
    m_fpsLabel->setStyleSheet(R"(QLabel {color: #00FF00;})");

    setMouseTracking(true);
    if (m_videoWidget) {
        m_videoWidget->setMouseTracking(true);
    }
    ui->keepRatioWidget->setMouseTracking(true);
}

QRect VideoForm::getGrabCursorRect()
{
    QRect rc;
    QWidget *vw = videoWidget();
#if defined(Q_OS_WIN32)
    rc = QRect(ui->keepRatioWidget->mapToGlobal(vw->pos()), vw->size());
    // high dpi support
    rc.setTopLeft(rc.topLeft() * vw->devicePixelRatioF());
    rc.setBottomRight(rc.bottomRight() * vw->devicePixelRatioF());

    rc.setX(rc.x() + 10);
    rc.setY(rc.y() + 10);
    rc.setWidth(rc.width() - 20);
    rc.setHeight(rc.height() - 20);
#elif defined(Q_OS_MACOS)
    rc = vw->geometry();
    rc.setTopLeft(ui->keepRatioWidget->mapToGlobal(rc.topLeft()));
    rc.setBottomRight(ui->keepRatioWidget->mapToGlobal(rc.bottomRight()));

    rc.setX(rc.x() + 10);
    rc.setY(rc.y() + 10);
    rc.setWidth(rc.width() - 20);
    rc.setHeight(rc.height() - 20);
#elif defined(Q_OS_LINUX)
    rc = QRect(ui->keepRatioWidget->mapToGlobal(vw->pos()), vw->size());
    // high dpi support -- taken from the WIN32 section and untested
    rc.setTopLeft(rc.topLeft() * vw->devicePixelRatioF());
    rc.setBottomRight(rc.bottomRight() * vw->devicePixelRatioF());

    rc.setX(rc.x() + 10);
    rc.setY(rc.y() + 10);
    rc.setWidth(rc.width() - 20);
    rc.setHeight(rc.height() - 20);
#endif
    return rc;
}

const QSize &VideoForm::frameSize()
{
    return m_frameSize;
}

void VideoForm::resizeSquare()
{
    QRect screenRect = getScreenRect();
    if (screenRect.isEmpty()) {
        qWarning() << "getScreenRect is empty";
        return;
    }
    resize(screenRect.height(), screenRect.height());
}

void VideoForm::removeBlackRect()
{
    resize(ui->keepRatioWidget->goodSize());
}

void VideoForm::showFPS(bool show)
{
    if (!m_fpsLabel) {
        return;
    }
    m_fpsLabel->setVisible(show);
}

void VideoForm::updateRender(int width, int height, uint8_t* dataY, uint8_t* dataU, uint8_t* dataV, int linesizeY, int linesizeU, int linesizeV)
{
    if (isMetalMode()) {
        // Metal 路径不通过此方法渲染，使用 onFrameMetal
        return;
    }

    if (!m_videoWidget) {
        return;
    }

    if (m_videoWidget->isHidden()) {
        if (m_loadingWidget) {
            m_loadingWidget->close();
        }
        m_videoWidget->show();
    }

    if (!m_flexDisplay) {
        updateShowSize(QSize(width, height));
    } else {
        m_frameSize = QSize(width, height);
    }
    m_videoWidget->setFrameSize(QSize(width, height));
    m_videoWidget->updateTextures(dataY, dataU, dataV, linesizeY, linesizeU, linesizeV);
}

void VideoForm::setSerial(const QString &serial)
{
    m_serial = serial;
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    m_flexDisplay = device && device->isFlexDisplay();
    if (m_flexDisplay) {
        ui->keepRatioWidget->setWidthHeightRatio(-1.0f);
    }
}

void VideoForm::showToolForm(bool show)
{
    if (!m_toolForm) {
        m_toolForm = new ToolForm(this, ToolForm::AP_OUTSIDE_RIGHT);
        m_toolForm->setSerial(m_serial);
    }
    m_toolForm->move(pos().x() + geometry().width(), pos().y() + 30);
    m_toolForm->setVisible(show);
}

void VideoForm::moveCenter()
{
    QRect screenRect = getScreenRect();
    if (screenRect.isEmpty()) {
        qWarning() << "getScreenRect is empty";
        return;
    }
    // 窗口居中
    move(screenRect.center() - QRect(0, 0, size().width(), size().height()).center());
}

bool VideoForm::isRectOnAnyScreen(const QRect &rect) const
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (!screen) {
            continue;
        }
        // 用 intersects 而非 contains：窗口只要有一部分可见就认为在屏内，
        // 允许用户把窗口放在屏幕边缘（部分露出）的情况。
        if (screen->availableGeometry().intersects(rect)) {
            return true;
        }
    }
    return false;
}

void VideoForm::ensureOnScreen()
{
    // 全屏状态下由系统接管，不必干预
    if (isFullScreen()) {
        return;
    }

    // 使用 frameGeometry 而非 geometry：前者包含窗口边框，
    // 更能反映窗口真实占用的屏幕区域。
    const QRect curRect = frameGeometry();
    if (isRectOnAnyScreen(curRect)) {
        return;
    }

    // 当前窗口已完全落在所有屏幕之外（典型场景：副屏被拔掉后
    // 窗口坐标仍停留在已断开的屏幕区域）。移回主屏并居中。
    //
    // 注意：此处不能直接调用 moveCenter()。因为 getScreenRect() 会优先
    // 取窗口当前所属的 QScreen，而窗口此时正位于已失效的屏幕区域，
    // 可能得到错误的目标位置。因此这里显式取主屏的可用区域来计算。
    qWarning() << "window is off-screen, moving back to primary screen. rect =" << curRect;

    QScreen *primary = QGuiApplication::primaryScreen();
    if (!primary) {
        qWarning() << "no primary screen available, skip off-screen recovery";
        return;
    }

    const QRect avail = primary->availableGeometry();
    if (avail.isEmpty()) {
        return;
    }

    const QPoint target = avail.center() - QRect(0, 0, size().width(), size().height()).center();
    move(target);
    qWarning() << "window recovered to" << target;
}

void VideoForm::installShortcut()
{
    QShortcut *shortcut = nullptr;

    // switchFullScreen
    shortcut = new QShortcut(QKeySequence("Ctrl+f"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        switchFullScreen();
    });

    // resizeSquare
    shortcut = new QShortcut(QKeySequence("Ctrl+g"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() { resizeSquare(); });

    // removeBlackRect
    shortcut = new QShortcut(QKeySequence("Ctrl+w"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() { removeBlackRect(); });

    // postGoHome
    shortcut = new QShortcut(QKeySequence("Ctrl+h"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        device->postGoHome();
    });

    // postGoBack
    shortcut = new QShortcut(QKeySequence("Ctrl+b"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        device->postGoBack();
    });

    // postAppSwitch
    shortcut = new QShortcut(QKeySequence("Ctrl+s"), this);
    // En edición Ctrl+S guarda el mapa: se guarda el puntero para
    // desactivar este atajo mientras el editor está abierto.
    m_shortcutAppSwitch = shortcut;
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->postAppSwitch();
    });

    // postGoMenu
    shortcut = new QShortcut(QKeySequence("Ctrl+m"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        device->postGoMenu();
    });

    // postVolumeUp
    shortcut = new QShortcut(QKeySequence("Ctrl+up"), this);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->postVolumeUp();
    });

    // postVolumeDown
    shortcut = new QShortcut(QKeySequence("Ctrl+down"), this);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->postVolumeDown();
    });

    // postPower
    shortcut = new QShortcut(QKeySequence("Ctrl+p"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->postPower();
    });

    shortcut = new QShortcut(QKeySequence("Ctrl+o"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->setDisplayPower(false);
    });

    // expandNotificationPanel
    shortcut = new QShortcut(QKeySequence("Ctrl+n"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->expandNotificationPanel();
    });

    shortcut = new QShortcut(QKeySequence("Ctrl+Alt+n"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (device) {
            device->expandSettingsPanel();
        }
    });

    shortcut = new QShortcut(QKeySequence("Ctrl+r"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (device) {
            device->rotateDevice();
        }
    });

    // collapsePanel
    shortcut = new QShortcut(QKeySequence("Ctrl+Shift+n"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->collapsePanel();
    });

    // copy
    shortcut = new QShortcut(QKeySequence("Ctrl+c"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->postCopy();
    });

    // cut
    shortcut = new QShortcut(QKeySequence("Ctrl+x"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->postCut();
    });

    // clipboardPaste
    shortcut = new QShortcut(QKeySequence("Ctrl+v"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->setDeviceClipboard();
    });

    // setDeviceClipboard
    shortcut = new QShortcut(QKeySequence("Ctrl+Shift+v"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->clipboardPaste();
    });
}

QRect VideoForm::getScreenRect()
{
    QRect screenRect;
    QScreen *screen = QGuiApplication::primaryScreen();
    QWidget *win = window();
    if (win) {
        QWindow *winHandle = win->windowHandle();
        if (winHandle) {
            screen = winHandle->screen();
        }
    }

    if (screen) {
        screenRect = screen->availableGeometry();
    }
    return screenRect;
}

void VideoForm::updateStyleSheet(bool vertical)
{
    if (vertical) {
        setStyleSheet(R"(
                 #videoForm {
                     border-image: url(:/image/videoform/phone-v.png) 150px 65px 85px 65px;
                     border-width: 150px 65px 85px 65px;
                 }
                 )");
    } else {
        setStyleSheet(R"(
                 #videoForm {
                     border-image: url(:/image/videoform/phone-h.png) 65px 85px 65px 150px;
                     border-width: 65px 85px 65px 150px;
                 }
                 )");
    }
    layout()->setContentsMargins(getMargins(vertical));
}

QMargins VideoForm::getMargins(bool vertical)
{
    QMargins margins;
    if (vertical) {
        margins = QMargins(10, 68, 12, 62);
    } else {
        margins = QMargins(68, 12, 62, 10);
    }
    return margins;
}

void VideoForm::updateShowSize(const QSize &newSize)
{
    if (m_frameSize != newSize) {
        m_frameSize = newSize;

        m_widthHeightRatio = 1.0f * newSize.width() / newSize.height();
        ui->keepRatioWidget->setWidthHeightRatio(m_widthHeightRatio);

        bool vertical = m_widthHeightRatio < 1.0f ? true : false;
        QSize showSize = newSize;
        QRect screenRect = getScreenRect();
        if (screenRect.isEmpty()) {
            qWarning() << "getScreenRect is empty";
            return;
        }
        if (vertical) {
            showSize.setHeight(qMin(newSize.height(), screenRect.height() - 200));
            showSize.setWidth(showSize.height() * m_widthHeightRatio);
        } else {
            showSize.setWidth(qMin(newSize.width(), screenRect.width() / 2));
            showSize.setHeight(showSize.width() / m_widthHeightRatio);
        }

        if (isFullScreen() && qsc::IDeviceManage::getInstance().getDevice(m_serial)) {
            switchFullScreen();
        }

        if (isMaximized()) {
            showNormal();
        }

        if (m_skin) {
            QMargins m = getMargins(vertical);
            showSize.setWidth(showSize.width() + m.left() + m.right());
            showSize.setHeight(showSize.height() + m.top() + m.bottom());
        }

        if (showSize != size()) {
            resize(showSize);
            if (m_skin) {
                updateStyleSheet(vertical);
            }
            moveCenter();
        }
    }
}

void VideoForm::onVideoSessionChanged(const QSize &size, bool clientResized)
{
    if (m_flexDisplay) {
        m_frameSize = size;
        m_preventAutoResize = clientResized;
        ui->keepRatioWidget->setWidthHeightRatio(-1.0f);
        return;
    }
    // clientResized is only meaningful for flex display. Normal display
    // rotations must retain the longstanding auto-resize behavior.
    m_preventAutoResize = false;
    updateShowSize(size);
}

void VideoForm::switchFullScreen()
{
    if (isFullScreen()) {
        // 横屏全屏铺满全屏，恢复时，恢复保持宽高比
        if (m_widthHeightRatio > 1.0f) {
            ui->keepRatioWidget->setWidthHeightRatio(m_widthHeightRatio);
        }

        showNormal();
        // back to normal size.
        resize(m_normalSize);
        // fullscreen window will move (0,0). qt bug?
        move(m_fullScreenBeforePos);

#ifdef Q_OS_MACOS
        //setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        //show();
#endif
        if (m_skin) {
            updateStyleSheet(m_frameSize.height() > m_frameSize.width());
        }
        showToolForm(this->show_toolbar);
#ifdef Q_OS_WIN32
        ::SetThreadExecutionState(ES_CONTINUOUS);
#endif
    } else {
        // 横屏全屏铺满全屏，不保持宽高比
        if (m_widthHeightRatio > 1.0f) {
            ui->keepRatioWidget->setWidthHeightRatio(-1.0f);
        }

        // record current size before fullscreen, it will be used to rollback size after exit fullscreen.
        m_normalSize = size();

        m_fullScreenBeforePos = pos();
        // 这种临时增加标题栏再全屏的方案会导致收不到mousemove事件，导致setmousetrack失效
        // mac fullscreen must show title bar
#ifdef Q_OS_MACOS
        //setWindowFlags(windowFlags() & ~Qt::FramelessWindowHint);
#endif
        showToolForm(false);
        if (m_skin) {
            layout()->setContentsMargins(0, 0, 0, 0);
        }
        showFullScreen();

        // 全屏状态禁止电脑休眠、息屏
#ifdef Q_OS_WIN32
        ::SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
#endif
    }
}

bool VideoForm::isHost()
{
    if (!m_toolForm) {
        return false;
    }
    return m_toolForm->isHost();
}

void VideoForm::updateFPS(quint32 fps)
{
    if (!m_fpsLabel) {
        return;
    }
    m_fpsLabel->setText(QString("FPS:%1").arg(fps));
}

void VideoForm::grabCursor(bool grab)
{
    QRect rc = getGrabCursorRect();
    MouseTap::getInstance()->enableMouseEventTap(rc, grab);
}

void VideoForm::onFrame(int width, int height, uint8_t *dataY, uint8_t *dataU, uint8_t *dataV, int linesizeY, int linesizeU, int linesizeV)
{
    updateRender(width, height, dataY, dataU, dataV, linesizeY, linesizeU, linesizeV);
}

void VideoForm::onFrameMetal(void *cvPixelBuffer, int width, int height)
{
#ifdef Q_OS_MACOS
    if (!m_metalWidget || !cvPixelBuffer) {
        return;
    }

    if (m_metalFirstFrame) {
        m_metalFirstFrame = false;
        if (m_loadingWidget) {
            m_loadingWidget->close();
        }
        ui->keepRatioWidget->updateGeometry();
    }

    updateShowSize(QSize(width, height));
    m_metalWidget->renderFrame((CVPixelBufferRef)cvPixelBuffer, width, height);
#else
    Q_UNUSED(cvPixelBuffer);
    Q_UNUSED(width);
    Q_UNUSED(height);
#endif
}

void VideoForm::staysOnTop(bool top)
{
    bool needShow = false;
    if (isVisible()) {
        needShow = true;
    }
    setWindowFlag(Qt::WindowStaysOnTopHint, top);
    if (m_toolForm) {
        m_toolForm->setWindowFlag(Qt::WindowStaysOnTopHint, top);
    }
    if (needShow) {
        show();
    }
}

void VideoForm::mousePressEvent(QMouseEvent *event)
{
    if (m_editing && editHandleMousePress(event)) {
        return;
    }

    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (event->button() == Qt::MiddleButton) {
        if (device && !device->isCurrentCustomKeymap()) {
            device->postGoHome();
            return;
        }
    }

    if (event->button() == Qt::RightButton) {
        if (device && !device->isCurrentCustomKeymap()) {
            device->postGoBack();
            return;
        }
    }

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        QPointF localPos = event->localPos();
        QPointF globalPos = event->globalPos();
#else
        QPointF localPos = event->position();
        QPointF globalPos = event->globalPosition();
#endif

    QWidget *vw = videoWidget();
    if (vw && vw->geometry().contains(event->pos())) {
        if (!device) {
            return;
        }
        QPointF mappedPos = vw->mapFrom(this, localPos.toPoint());
        QMouseEvent newEvent(event->type(), mappedPos, globalPos, event->button(), event->buttons(), event->modifiers());
        emit device->mouseEvent(&newEvent, m_frameSize, vw->size());

        // debug keymap pos
        if (event->button() == Qt::LeftButton) {
            qreal x = localPos.x() / vw->size().width();
            qreal y = localPos.y() / vw->size().height();
            QString posTip = QString(R"("pos": {"x": %1, "y": %2})").arg(x).arg(y);
            qInfo() << posTip.toStdString().c_str();
        }
    } else {
        if (event->button() == Qt::LeftButton) {
            m_dragPosition = globalPos.toPoint() - frameGeometry().topLeft();
            event->accept();
        }
    }
}

void VideoForm::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_editing && editHandleMouseRelease(event)) {
        return;
    }

    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (m_dragPosition.isNull()) {
        if (!device) {
            return;
        }
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        QPointF localPos = event->localPos();
        QPointF globalPos = event->globalPos();
#else
        QPointF localPos = event->position();
        QPointF globalPos = event->globalPosition();
#endif
        QWidget *vw = videoWidget();
        if (!vw) {
            return;
        }

        // local check
        QPointF local = vw->mapFrom(this, localPos.toPoint());
        if (local.x() < 0) {
            local.setX(0);
        }
        if (local.x() > vw->width()) {
            local.setX(vw->width());
        }
        if (local.y() < 0) {
            local.setY(0);
        }
        if (local.y() > vw->height()) {
            local.setY(vw->height());
        }
        QMouseEvent newEvent(event->type(), local, globalPos, event->button(), event->buttons(), event->modifiers());
        emit device->mouseEvent(&newEvent, m_frameSize, vw->size());
    } else {
        m_dragPosition = QPoint(0, 0);
    }
}

void VideoForm::mouseMoveEvent(QMouseEvent *event)
{
    if (m_editing && editHandleMouseMove(event)) {
        return;
    }

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        QPointF localPos = event->localPos();
        QPointF globalPos = event->globalPos();
#else
        QPointF localPos = event->position();
        QPointF globalPos = event->globalPosition();
#endif
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    QWidget *vw = videoWidget();
    if (vw && vw->geometry().contains(event->pos())) {
        if (!device) {
            return;
        }
        QPointF mappedPos = vw->mapFrom(this, localPos.toPoint());
        QMouseEvent newEvent(event->type(), mappedPos, globalPos, event->button(), event->buttons(), event->modifiers());
        emit device->mouseEvent(&newEvent, m_frameSize, vw->size());
    } else if (!m_dragPosition.isNull()) {
        if (event->buttons() & Qt::LeftButton) {
            move(globalPos.toPoint() - m_dragPosition);
            event->accept();
        }
    }
}

void VideoForm::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_editing) {
        // En edición el doble clic no llega al dispositivo.
        event->accept();
        return;
    }

    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    QWidget *vw = videoWidget();
    if (event->button() == Qt::LeftButton && vw && !vw->geometry().contains(event->pos())) {
        if (!isMaximized()) {
            removeBlackRect();
        }
    }

    if (event->button() == Qt::RightButton && device && !device->isCurrentCustomKeymap()) {
        emit device->postBackOrScreenOn(event->type() == QEvent::MouseButtonPress);
    }

    if (vw && vw->geometry().contains(event->pos())) {
        if (!device) {
            return;
        }
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        QPointF localPos = event->localPos();
        QPointF globalPos = event->globalPos();
#else
        QPointF localPos = event->position();
        QPointF globalPos = event->globalPosition();
#endif
        QPointF mappedPos = vw->mapFrom(this, localPos.toPoint());
        QMouseEvent newEvent(event->type(), mappedPos, globalPos, event->button(), event->buttons(), event->modifiers());
        emit device->mouseEvent(&newEvent, m_frameSize, vw->size());
    }
}

void VideoForm::wheelEvent(QWheelEvent *event)
{
    if (m_editing) {
        // En edición la rueda no llega al dispositivo.
        event->accept();
        return;
    }

    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    QWidget *vw = videoWidget();
    if (!vw) {
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    if (vw->geometry().contains(event->position().toPoint())) {
        if (!device) {
            return;
        }
        QPointF pos = vw->mapFrom(this, event->position().toPoint());
        QWheelEvent wheelEvent(
            pos, event->globalPosition(), event->pixelDelta(), event->angleDelta(), event->buttons(), event->modifiers(), event->phase(), event->inverted());
#else
    if (vw->geometry().contains(event->pos())) {
        if (!device) {
            return;
        }
        QPointF pos = vw->mapFrom(this, event->pos());

        QWheelEvent wheelEvent(
            pos, event->globalPosF(), event->pixelDelta(), event->angleDelta(), event->delta(), event->orientation(),
            event->buttons(), event->modifiers(), event->phase(), event->source(), event->inverted());
#endif
        emit device->wheelEvent(&wheelEvent, m_frameSize, vw->size());
    }
}

void VideoForm::keyPressEvent(QKeyEvent *event)
{
    if (m_editing) {
        editHandleKeyPress(event);
        return;
    }

    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    if (Qt::Key_Escape == event->key() && !event->isAutoRepeat() && isFullScreen()) {
        switchFullScreen();
    }

    QWidget *vw = videoWidget();
    QSize widgetSize = vw ? vw->size() : m_frameSize;
    emit device->keyEvent(event, m_frameSize, widgetSize);
}

void VideoForm::keyReleaseEvent(QKeyEvent *event)
{
    if (m_editing) {
        // En edición la liberación de teclas tampoco llega al dispositivo.
        event->accept();
        return;
    }

    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    QWidget *vw = videoWidget();
    QSize widgetSize = vw ? vw->size() : m_frameSize;
    emit device->keyEvent(event, m_frameSize, widgetSize);
}

void VideoForm::paintEvent(QPaintEvent *paint)
{
    Q_UNUSED(paint)
    QStyleOption opt;
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    opt.init(this);
#else
    opt.initFrom(this);
#endif
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
    if (m_editing) {
        // El dibujo del editor lo hace el overlay transparente sobre el
        // vídeo (pintar aquí quedaría tapado por el widget OpenGL); aquí
        // solo se asegura de que el overlay esté colocado y visible.
        updateEditOverlay();
    }
}

void VideoForm::showEvent(QShowEvent *event)
{
    Q_UNUSED(event)
    // 窗口每次显示时先做一次越界检查，处理"上次退出时窗口位于副屏、
    // 本次启动副屏已不存在"导致窗口出现在屏幕外的情况。
    //
    // Dialog::onDeviceConnected 会先 setGeometry(savedRect) 再 show()，
    // 因此这里能捕获到从配置中恢复出来的屏外坐标。
    ensureOnScreen();
    // 窗口显示后 Qt 仍可能根据尺寸/边框策略再做一次位置调整，
    // 故稍后复查一次，确保最终位置落在屏内。
    QTimer::singleShot(300, this, [this]() { ensureOnScreen(); });
    if (!isFullScreen() && this->show_toolbar) {
        QTimer::singleShot(500, this, [this](){
            showToolForm(this->show_toolbar);
        });
    }
}

void VideoForm::resizeEvent(QResizeEvent *event)
{
    if (m_editing) {
        updateEditOverlay();
    }

    Q_UNUSED(event)
    if (m_flexDisplay) {
        m_pendingDisplaySize = ui->keepRatioWidget->size();
        if (!m_pendingDisplaySize.isEmpty()) {
            m_flexResizeTimer.start();
        }
        return;
    }

    QSize goodSize = ui->keepRatioWidget->goodSize();
    if (goodSize.isEmpty()) {
        return;
    }
    QSize curSize = size();
    // 限制VideoForm尺寸不能小于keepRatioWidget good size
    if (m_widthHeightRatio > 1.0f) {
        // hor
        if (curSize.height() <= goodSize.height()) {
            setMinimumHeight(goodSize.height());
        } else {
            setMinimumHeight(0);
        }
    } else {
        // ver
        if (curSize.width() <= goodSize.width()) {
            setMinimumWidth(goodSize.width());
        } else {
            setMinimumWidth(0);
        }
    }
}

void VideoForm::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event)
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    Config::getInstance().setRect(device->getSerial(), geometry());
    device->disconnectDevice();
}

void VideoForm::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}

void VideoForm::dragMoveEvent(QDragMoveEvent *event)
{
    Q_UNUSED(event)
}

void VideoForm::dragLeaveEvent(QDragLeaveEvent *event)
{
    Q_UNUSED(event)
}

void VideoForm::dropEvent(QDropEvent *event)
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    const QMimeData *qm = event->mimeData();
    QList<QUrl> urls = qm->urls();

    for (const QUrl &url : urls) {
        QString file = url.toLocalFile();
        QFileInfo fileInfo(file);

        if (!fileInfo.exists()) {
            QMessageBox::warning(this, "VenMirror", tr("file does not exist"), QMessageBox::Ok);
            continue;
        }

        if (fileInfo.isFile() && fileInfo.suffix() == "apk") {
            emit device->installApkRequest(file);
            continue;
        }
        emit device->pushFileRequest(file, Config::getInstance().getPushFilePath() + fileInfo.fileName());
    }
}

// =====================================================================
// Editor visual de keymap (VenMirror Fase 2)
// ---------------------------------------------------------------------
// El editor dibuja sobre el vídeo con un overlay transparente hijo del
// widget de vídeo: pintar en VideoForm::paintEvent quedaría tapado por
// el widget OpenGL. El overlay ignora el ratón, así que todos los
// clics siguen llegando a los handlers de VideoForm. Mientras dura la
// edición ningún evento de ratón/teclado/rueda llega al dispositivo.
// =====================================================================

EditOverlayWidget::EditOverlayWidget(VideoForm *form, QWidget *parent) : QWidget(parent), m_form(form)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
}

void EditOverlayWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    if (!m_form) {
        return;
    }
    QPainter painter(this);
    m_form->paintEditOverlay(&painter);
}

void VideoForm::openKeymapEditor(const QString &filePath, const QString &jsonText)
{
    m_editFilePath = filePath;
    // KeyMap nuevo en cada apertura: loadKeyMap acumula nodos en vez de
    // limpiarlos, así que reutilizar el anterior duplicaría el mapa.
    m_editKeyMap.reset(new KeyMap);
    // La pista se calcula después de fijar la tecla de cambio para que
    // ya muestre la correcta (editDefaultHint la incluye, Fase 3).
    QString startHint;
    if (jsonText.trimmed().isEmpty()) {
        m_editKeyMap->setSwitchKey(KeyMap::AT_KEY, Qt::Key_F);
        startHint = editDefaultHint();
    } else {
        QJsonParseError parseError;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
        Q_UNUSED(jsonDoc)
        if (parseError.error != QJsonParseError::NoError) {
            startHint = QString("El archivo no es un JSON válido, se empieza con un mapa vacío.");
        }
        m_editKeyMap->loadKeyMap(jsonText);
        // Si el JSON no traía una tecla de cambio válida, loadKeyMap deja
        // la de fábrica (`); forzar F para que el cambio de modo sea el
        // conocido por Winel. Un mapa que use ` de verdad lo dice en su JSON.
        if (m_editKeyMap->getSwitchKey() == Qt::Key_QuoteLeft && !jsonText.contains("QuoteLeft")) {
            m_editKeyMap->setSwitchKey(KeyMap::AT_KEY, Qt::Key_F);
        }
        if (startHint.isEmpty()) {
            startHint = editDefaultHint();
        }
    }

    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    m_editHadDevice = !device.isNull();
    m_editWasCustom = device ? device->isCurrentCustomKeymap() : false;
    if (device) {
        // Modo normal para que los clics no disparen acciones del juego.
        device->setGameMap(false);
    }

    m_editing = true;
    m_editSelected = -1;
    m_editTool = EditTool::None;
    m_editAwaitingKey = -1;
    m_editAwaitingSwitchKey = false;
    m_editDragging = false;
    m_editDragEndHandle = false;
    m_editDragEndPendingValid = false;
    setEditHint(startHint);
    if (m_shortcutAppSwitch) {
        m_shortcutAppSwitch->setEnabled(false);
    }
    updateEditOverlay();
    update();
}

void VideoForm::exitKeymapEdit()
{
    if (!m_editing) {
        return;
    }
    m_editing = false;
    m_editSelected = -1;
    m_editTool = EditTool::None;
    m_editAwaitingKey = -1;
    m_editAwaitingSwitchKey = false;
    m_editDragging = false;
    m_editDragEndHandle = false;
    m_editDragEndPendingValid = false;
    m_editHint.clear();
    if (m_shortcutAppSwitch) {
        m_shortcutAppSwitch->setEnabled(true);
    }
    if (m_editHadDevice) {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (device) {
            device->setGameMap(m_editWasCustom);
        }
    }
    if (m_editOverlay) {
        m_editOverlay->hide();
    }
    update();
}

bool VideoForm::saveKeymapEdit()
{
    if (m_editKeyMap.isNull() || m_editFilePath.isEmpty()) {
        setEditHint(QString("No hay archivo donde guardar el mapa."));
        update();
        return false;
    }

    QString json = m_editKeyMap->saveKeyMap();

    // Validar antes de escribir: el JSON debe parsear y, al cargarlo en
    // un KeyMap temporal, debe dar los mismos nodos y la misma tecla de
    // cambio (si algún nodo se perdiera al recargar, no se guarda nada).
    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    bool valid = (parseError.error == QJsonParseError::NoError) && jsonDoc.isObject();
    if (valid) {
        KeyMap checkMap;
        checkMap.loadKeyMap(json);
        valid = (checkMap.keyMapNodes().size() == m_editKeyMap->keyMapNodes().size())
                && (checkMap.getSwitchKey() == m_editKeyMap->getSwitchKey());
    }
    if (!valid) {
        setEditHint(QString("Error: el mapa generado no es válido, no se guardó."));
        update();
        return false;
    }

    QFile file(m_editFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        setEditHint(QString("Error: no se pudo escribir %1").arg(m_editFilePath));
        update();
        return false;
    }
    QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    out.setEncoding(QStringConverter::Utf8);
#else
    out.setCodec("UTF-8");
#endif
    out << json;
    out.flush();
    file.close();
    if (file.error() != QFile::NoError) {
        setEditHint(QString("Error: no se pudo escribir %1").arg(m_editFilePath));
        update();
        return false;
    }

    // Aplicar en vivo sin reiniciar el espejo: el mismo camino del
    // botón Aplicar. Sin dispositivo solo se guarda el archivo.
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (device) {
        device->updateScript(json);
    }

    setEditHint(QString("Mapa guardado y aplicado"));
    update();
    // Mostrar la confirmación ~2 s y luego salir del modo edición.
    QTimer::singleShot(2000, this, [this]() { exitKeymapEdit(); });
    return true;
}

void VideoForm::setEditHint(const QString &hint)
{
    m_editHint = hint;
}

QString VideoForm::editDefaultHint() const
{
    // Fase 3: la pista muestra siempre la tecla de cambio actual.
    if (m_editKeyMap.isNull()) {
        return QString("Clic: seleccionar · Arrastrar: mover · Tecla: asignar · Supr: borrar · Ctrl+D: duplicar · Ctrl+S: guardar · Esc: salir");
    }
    return QString("Cambio: %1 · Clic: seleccionar · Arrastrar: mover · Tecla: asignar · Supr: borrar · Ctrl+D: duplicar · Ctrl+S: guardar · Esc: salir")
        .arg(editSwitchKeyLabel());
}

QString VideoForm::editSwitchKeyLabel() const
{
    if (m_editKeyMap.isNull()) {
        return QString("?");
    }
    // KeyMap no expone el tipo de la tecla de cambio, solo si es de teclado.
    KeyMap::ActionType type = m_editKeyMap->isSwitchOnKeyboard() ? KeyMap::AT_KEY : KeyMap::AT_MOUSE;
    return editKeyLabel(type, m_editKeyMap->getSwitchKey());
}

KeyMap::KeyMapNode VideoForm::editDefaultMouseMoveNode() const
{
    // Valores del mapa de MHO de Winel: al cargar ese archivo, el
    // "speedRatio" general (10) queda tapado por speedRatioX/Y, así que
    // el nodo guarda solo (3.25, 1.25); saveKeyMap escribe exactamente
    // esos dos campos (sin smallEyes) y el round-trip es idéntico.
    KeyMap::KeyMapNode node;
    node.type = KeyMap::KMT_MOUSE_MOVE;
    node.data.mouseMove.startPos = QPointF(0.60, 0.45);
    node.data.mouseMove.speedRatio = QPointF(3.25, 1.25);
    node.data.mouseMove.smallEyes = KeyMap::KeyNode(KeyMap::AT_INVALID, Qt::Key_unknown);
    return node;
}

bool VideoForm::editKeyUsedByNode(KeyMap::ActionType type, int key) const
{
    if (m_editKeyMap.isNull()) {
        return false;
    }
    auto matches = [type, key](KeyMap::ActionType nodeType, int nodeKey) {
        return nodeType == type && nodeKey == key;
    };
    const QVector<KeyMap::KeyMapNode> &nodes = m_editKeyMap->keyMapNodes();
    for (const KeyMap::KeyMapNode &node : nodes) {
        switch (node.type) {
        case KeyMap::KMT_CLICK:
        case KeyMap::KMT_CLICK_TWICE:
            if (matches(node.data.click.keyNode.type, node.data.click.keyNode.key)) {
                return true;
            }
            break;
        case KeyMap::KMT_CLICK_MULTI:
            if (matches(node.data.clickMulti.keyNode.type, node.data.clickMulti.keyNode.key)) {
                return true;
            }
            break;
        case KeyMap::KMT_STEER_WHEEL:
            if (matches(node.data.steerWheel.left.type, node.data.steerWheel.left.key)
                || matches(node.data.steerWheel.right.type, node.data.steerWheel.right.key)
                || matches(node.data.steerWheel.up.type, node.data.steerWheel.up.key)
                || matches(node.data.steerWheel.down.type, node.data.steerWheel.down.key)) {
                return true;
            }
            break;
        case KeyMap::KMT_DRAG:
            if (matches(node.data.drag.keyNode.type, node.data.drag.keyNode.key)) {
                return true;
            }
            break;
        case KeyMap::KMT_MOUSE_MOVE:
            if (matches(node.data.mouseMove.smallEyes.type, node.data.mouseMove.smallEyes.key)) {
                return true;
            }
            break;
        case KeyMap::KMT_ANDROID_KEY:
            if (matches(node.data.androidKey.keyNode.type, node.data.androidKey.keyNode.key)) {
                return true;
            }
            break;
        default:
            break;
        }
    }
    return false;
}

void VideoForm::editToggleMouseMoveMap()
{
    if (m_editKeyMap.isNull()) {
        return;
    }
    if (m_editKeyMap->isValidMouseMoveMap()) {
        m_editKeyMap->removeMouseMoveMap();
        // Quitar el nodo desplaza los índices: no queda selección válida.
        m_editSelected = -1;
        m_editAwaitingKey = -1;
        setEditHint(QString("Cámara por ratón desactivada."));
    } else {
        m_editKeyMap->setMouseMoveMap(editDefaultMouseMoveNode());
        // Seleccionar el nodo nuevo para poder arrastrarlo a su sitio.
        m_editSelected = -1;
        m_editAwaitingKey = -1;
        const QVector<KeyMap::KeyMapNode> &nodes = m_editKeyMap->keyMapNodes();
        for (int i = 0; i < nodes.size(); ++i) {
            if (nodes.at(i).type == KeyMap::KMT_MOUSE_MOVE) {
                m_editSelected = i;
                break;
            }
        }
        setEditHint(QString("Cámara por ratón activada. Arrástrala para mover el punto de inicio."));
    }
    update();
}

void VideoForm::updateEditOverlay()
{
    if (!m_editing) {
        return;
    }
    QWidget *vw = videoWidget();
    if (!vw) {
        return;
    }
    if (m_editOverlay.isNull()) {
        m_editOverlay = new EditOverlayWidget(this, vw);
    } else if (m_editOverlay->parentWidget() != vw) {
        m_editOverlay->setParent(vw);
    }
    QRect geom(0, 0, vw->width(), vw->height());
    if (m_editOverlay->geometry() != geom) {
        m_editOverlay->setGeometry(geom);
    }
    if (!m_editOverlay->isVisible()) {
        m_editOverlay->show();
        m_editOverlay->raise();
    }
    m_editOverlay->update();
}

QRect VideoForm::editVideoRect() const
{
    QWidget *vw = videoWidget();
    if (!vw || vw->width() <= 0 || vw->height() <= 0) {
        return QRect();
    }
    QRect vwRect(vw->mapTo(this, QPoint(0, 0)), vw->size());
    if (m_frameSize.width() <= 0 || m_frameSize.height() <= 0) {
        return vwRect;
    }
    // El widget de vídeo ya guarda el aspecto del frame (KeepRatioWidget),
    // pero en flex display ocupa todo el hueco: ahí el frame queda con
    // letterbox dentro del widget y hay que recortar el rectángulo.
    qreal frameAspect = 1.0 * m_frameSize.width() / m_frameSize.height();
    qreal widgetAspect = 1.0 * vwRect.width() / vwRect.height();
    if (qAbs(widgetAspect - frameAspect) < 0.001) {
        return vwRect;
    }
    if (widgetAspect > frameAspect) {
        int w = int(vwRect.height() * frameAspect);
        int x = vwRect.x() + (vwRect.width() - w) / 2;
        return QRect(x, vwRect.y(), w, vwRect.height());
    }
    int h = int(vwRect.width() / frameAspect);
    int y = vwRect.y() + (vwRect.height() - h) / 2;
    return QRect(vwRect.x(), y, vwRect.width(), h);
}

QRect VideoForm::editVideoRectLocal() const
{
    QWidget *vw = videoWidget();
    if (!vw) {
        return QRect();
    }
    return editVideoRect().translated(-vw->mapTo(this, QPoint(0, 0)));
}

QPointF VideoForm::widgetToRel(const QPoint &p) const
{
    QRect rect = editVideoRect();
    if (rect.isEmpty()) {
        return QPointF(0, 0);
    }
    qreal x = 1.0 * (p.x() - rect.x()) / rect.width();
    qreal y = 1.0 * (p.y() - rect.y()) / rect.height();
    return QPointF(qBound(0.0, x, 1.0), qBound(0.0, y, 1.0));
}

QPoint VideoForm::relToWidget(const QPointF &rel) const
{
    QRect rect = editVideoRect();
    if (rect.isEmpty()) {
        return QPoint(0, 0);
    }
    return QPoint(rect.x() + qRound(rel.x() * rect.width()),
                  rect.y() + qRound(rel.y() * rect.height()));
}

QPointF VideoForm::editLocalToRel(const QPoint &p, const QRect &videoRect) const
{
    if (videoRect.isEmpty()) {
        return QPointF(0, 0);
    }
    qreal x = 1.0 * (p.x() - videoRect.x()) / videoRect.width();
    qreal y = 1.0 * (p.y() - videoRect.y()) / videoRect.height();
    return QPointF(qBound(0.0, x, 1.0), qBound(0.0, y, 1.0));
}

QPoint VideoForm::editRelToLocal(const QPointF &rel, const QRect &videoRect) const
{
    if (videoRect.isEmpty()) {
        return QPoint(0, 0);
    }
    return QPoint(videoRect.x() + qRound(rel.x() * videoRect.width()),
                  videoRect.y() + qRound(rel.y() * videoRect.height()));
}

QRect VideoForm::editPaletteBarRect(const QRect &videoRect) const
{
    const int margin = 6;
    return QRect(videoRect.x() + margin, videoRect.y() + margin,
                 qMax(0, videoRect.width() - margin * 2), 34);
}

QRect VideoForm::editHintRect(const QRect &videoRect) const
{
    QRect bar = editPaletteBarRect(videoRect);
    return QRect(videoRect.x() + 6, bar.bottom() + 4, qMax(0, videoRect.width() - 12), 20);
}

QVector<VideoForm::EditPaletteButton> VideoForm::editPaletteButtons(const QRect &videoRect) const
{
    // Esta función es la única fuente de la geometría de la paleta: la
    // usan tanto el pintado como el hit-test, así que nunca discrepan.
    QVector<EditPaletteButton> buttons;
    QRect bar = editPaletteBarRect(videoRect);
    if (bar.width() <= 0) {
        return buttons;
    }
    struct ToolDef { const char *id; const char *text; int width; };
    static const ToolDef tools[] = {
        { "click", "Toque", 62 },
        { "wheel", "Joystick", 76 },
        { "aim", "Mirilla", 62 },
        { "drag", "Arrastre", 76 },
        { "switch", "Cambio", 68 },
        { "camera", "Cámara", 68 },
    };
    static const ToolDef actions[] = {
        { "save", "Guardar", 68 },
        { "exit", "Salir", 54 },
    };
    const int margin = 6;
    const int gap = 6;
    int needed = 0;
    for (const ToolDef &t : tools) {
        needed += t.width + gap;
    }
    for (const ToolDef &a : actions) {
        needed += a.width + gap;
    }
    needed -= gap;
    int avail = bar.width() - margin * 2;
    qreal scale = 1.0;
    if (needed > avail && needed > 0) {
        scale = qMax(0.45, 1.0 * avail / needed);
    }
    int x = bar.x() + margin;
    for (const ToolDef &t : tools) {
        int w = qMax(40, int(t.width * scale));
        buttons.append({ QString::fromUtf8(t.id), QString::fromUtf8(t.text), QRect(x, bar.y(), w, bar.height()) });
        x += w + gap;
    }
    int actionW = 0;
    for (const ToolDef &a : actions) {
        actionW += qMax(40, int(a.width * scale));
    }
    actionW += gap * (2 - 1);
    int ax = bar.right() - margin - actionW + 1;
    for (const ToolDef &a : actions) {
        int w = qMax(40, int(a.width * scale));
        buttons.append({ QString::fromUtf8(a.id), QString::fromUtf8(a.text), QRect(ax, bar.y(), w, bar.height()) });
        ax += w + gap;
    }
    return buttons;
}

QString VideoForm::editPaletteHit(const QPoint &localPos) const
{
    QRect videoRect = editVideoRectLocal();
    if (videoRect.isEmpty()) {
        return QString();
    }
    for (const EditPaletteButton &button : editPaletteButtons(videoRect)) {
        if (button.rect.contains(localPos)) {
            return button.id;
        }
    }
    return QString();
}

QPointF VideoForm::editNodeAnchor(const KeyMap::KeyMapNode &node) const
{
    switch (node.type) {
    case KeyMap::KMT_CLICK:
    case KeyMap::KMT_CLICK_TWICE:
        return node.data.click.keyNode.pos;
    case KeyMap::KMT_CLICK_MULTI:
        if (node.data.clickMulti.keyNode.delayClickNodesCount > 0) {
            return node.data.clickMulti.keyNode.delayClickNodes[0].pos;
        }
        return QPointF(0, 0);
    case KeyMap::KMT_STEER_WHEEL:
        return node.data.steerWheel.centerPos;
    case KeyMap::KMT_DRAG:
        return node.data.drag.keyNode.pos;
    case KeyMap::KMT_MOUSE_MOVE:
        return node.data.mouseMove.startPos;
    case KeyMap::KMT_ANDROID_KEY:
        return node.data.androidKey.keyNode.pos;
    default:
        return QPointF(0, 0);
    }
}

QPointF VideoForm::editDragEndPos(const KeyMap::KeyMapNode &node, int index) const
{
    if (node.type != KeyMap::KMT_DRAG) {
        return QPointF(0, 0);
    }
    if (m_editDragEndPendingValid && index == m_editSelected) {
        return m_editDragEndPending;
    }
    return node.data.drag.keyNode.extendPos;
}

QPoint VideoForm::editClampDisplayPoint(const QPointF &rel, const QRect &videoRect, int radius) const
{
    // El centro dibujado nunca tapa la paleta ni se sale del vídeo. El
    // hit-test usa este mismo centro, así que clic y dibujo coinciden.
    QPoint center = editRelToLocal(rel, videoRect);
    QRect hintRect = editHintRect(videoRect);
    int top = hintRect.bottom() + radius + 4;
    int left = videoRect.left() + radius + 2;
    int right = videoRect.right() - radius - 2;
    int bottom = videoRect.bottom() - radius - 2;
    if (right < left) {
        left = videoRect.center().x();
        right = left;
    }
    if (bottom < top) {
        top = videoRect.center().y();
        bottom = top;
    }
    return QPoint(qBound(left, center.x(), right), qBound(top, center.y(), bottom));
}

QPoint VideoForm::editNodeDisplayCenter(const KeyMap::KeyMapNode &node, const QRect &videoRect, int radius) const
{
    return editClampDisplayPoint(editNodeAnchor(node), videoRect, radius);
}

int VideoForm::editHitTestNode(const QPoint &localPos, bool *hitEndHandle) const
{
    if (hitEndHandle) {
        *hitEndHandle = false;
    }
    if (m_editKeyMap.isNull()) {
        return -1;
    }
    const QVector<KeyMap::KeyMapNode> &nodes = m_editKeyMap->keyMapNodes();
    QRect videoRect = editVideoRectLocal();
    if (videoRect.isEmpty()) {
        return -1;
    }
    // El extremo final de un arrastre se comprueba primero: está cerca
    // del inicio y si no se mirara antes, el inicio se lo comería.
    int bestEnd = -1;
    int bestEndDist = 20 * 20;
    for (int i = 0; i < nodes.size(); ++i) {
        if (nodes.at(i).type != KeyMap::KMT_DRAG) {
            continue;
        }
        QPoint endPt = editClampDisplayPoint(editDragEndPos(nodes.at(i), i), videoRect, 8);
        int dx = localPos.x() - endPt.x();
        int dy = localPos.y() - endPt.y();
        int d2 = dx * dx + dy * dy;
        if (d2 <= bestEndDist) {
            bestEndDist = d2;
            bestEnd = i;
        }
    }
    if (bestEnd >= 0) {
        if (hitEndHandle) {
            *hitEndHandle = true;
        }
        return bestEnd;
    }
    int best = -1;
    int bestDist = 46 * 46;
    for (int i = 0; i < nodes.size(); ++i) {
        const KeyMap::KeyMapNode &node = nodes.at(i);
        int radius = (node.type == KeyMap::KMT_STEER_WHEEL) ? 46 : 28;
        QPoint center = editNodeDisplayCenter(node, videoRect, radius == 46 ? 46 : 16);
        int dx = localPos.x() - center.x();
        int dy = localPos.y() - center.y();
        int d2 = dx * dx + dy * dy;
        // <= para que en empate gane el último (el dibujado encima).
        if (d2 <= radius * radius && d2 <= bestDist) {
            bestDist = d2;
            best = i;
        }
    }
    return best;
}

QString VideoForm::editKeyLabel(KeyMap::ActionType type, int key) const
{
    if (key == Qt::Key_unknown) {
        return QString("?");
    }
    if (type == KeyMap::AT_MOUSE) {
        QString name = m_editKeyMap.isNull() ? QString() : m_editKeyMap->keyName(type, key);
        if (name == "LeftButton") {
            return QString("Clic izq.");
        }
        if (name == "RightButton") {
            return QString("Clic der.");
        }
        if (name == "MiddleButton") {
            return QString("Clic medio");
        }
        return name.isEmpty() ? QString("Clic") : name;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QString text = QKeySequence(QKeyCombination(Qt::NoModifier, Qt::Key(key))).toString();
#else
    QString text = QKeySequence(key).toString();
#endif
    return text.isEmpty() ? QString("?") : text;
}

QString VideoForm::editNodeKeyLabel(const KeyMap::KeyMapNode &node) const
{
    switch (node.type) {
    case KeyMap::KMT_CLICK:
    case KeyMap::KMT_CLICK_TWICE:
        return editKeyLabel(node.data.click.keyNode.type, node.data.click.keyNode.key);
    case KeyMap::KMT_CLICK_MULTI:
        return editKeyLabel(node.data.clickMulti.keyNode.type, node.data.clickMulti.keyNode.key);
    case KeyMap::KMT_DRAG:
        return editKeyLabel(node.data.drag.keyNode.type, node.data.drag.keyNode.key);
    case KeyMap::KMT_ANDROID_KEY:
        if (node.data.androidKey.keyNode.key == Qt::Key_unknown) {
            return QString("Android");
        }
        return editKeyLabel(node.data.androidKey.keyNode.type, node.data.androidKey.keyNode.key);
    case KeyMap::KMT_MOUSE_MOVE:
        return QString("Cámara");
    case KeyMap::KMT_STEER_WHEEL:
        return QString("Joystick");
    default:
        return QString("?");
    }
}

bool VideoForm::editNodeHasKey(const KeyMap::KeyMapNode &node) const
{
    switch (node.type) {
    case KeyMap::KMT_CLICK:
    case KeyMap::KMT_CLICK_TWICE:
        return node.data.click.keyNode.key != Qt::Key_unknown;
    case KeyMap::KMT_CLICK_MULTI:
        return node.data.clickMulti.keyNode.key != Qt::Key_unknown;
    case KeyMap::KMT_STEER_WHEEL:
        return node.data.steerWheel.left.key != Qt::Key_unknown
               && node.data.steerWheel.right.key != Qt::Key_unknown
               && node.data.steerWheel.up.key != Qt::Key_unknown
               && node.data.steerWheel.down.key != Qt::Key_unknown;
    case KeyMap::KMT_DRAG:
        return node.data.drag.keyNode.key != Qt::Key_unknown;
    case KeyMap::KMT_ANDROID_KEY:
        return node.data.androidKey.keyNode.key != Qt::Key_unknown;
    case KeyMap::KMT_MOUSE_MOVE:
        return true;
    default:
        return false;
    }
}

void VideoForm::editDrawMarkerLabel(QPainter *p, const QPoint &center, const QString &label) const
{
    QFont savedFont = p->font();
    if (QFontMetrics(savedFont).horizontalAdvance(label) <= 30) {
        p->drawText(QRect(center.x() - 18, center.y() - 9, 36, 18), Qt::AlignCenter, label);
        return;
    }
    QFont smallFont = savedFont;
    smallFont.setPointSize(9);
    p->setFont(smallFont);
    int w = QFontMetrics(smallFont).horizontalAdvance(label) + 8;
    p->drawText(QRect(center.x() - w / 2, center.y() + 17, w, 16), Qt::AlignCenter, label);
    p->setFont(savedFont);
}

void VideoForm::editDrawNode(QPainter *p, const KeyMap::KeyMapNode &node, int index, const QRect &videoRect) const
{
    bool selected = (index == m_editSelected);
    bool awaiting = (index == m_editAwaitingKey);
    bool hasKey = editNodeHasKey(node);
    QColor borderColor = selected ? QColor(255, 193, 7) : QColor(0, 229, 255);
    QPen pen(borderColor, selected ? 3 : 2);
    if (!hasKey) {
        pen.setStyle(Qt::DashLine);
    }
    QFont labelFont = p->font();
    labelFont.setPointSize(10);
    labelFont.setBold(true);
    p->setFont(labelFont);

    switch (node.type) {
    case KeyMap::KMT_STEER_WHEEL: {
        QPoint center = editNodeDisplayCenter(node, videoRect, 46);
        p->setPen(pen);
        p->setBrush(QColor(0, 0, 0, 140));
        p->drawEllipse(center, 46, 46);
        p->setPen(Qt::white);
        QFont dirFont = labelFont;
        dirFont.setPointSize(9);
        p->setFont(dirFont);
        p->drawText(QRect(center.x() - 30, center.y() - 46 - 17, 60, 15), Qt::AlignCenter,
                    editKeyLabel(node.data.steerWheel.up.type, node.data.steerWheel.up.key));
        p->drawText(QRect(center.x() - 30, center.y() + 46 + 2, 60, 15), Qt::AlignCenter,
                    editKeyLabel(node.data.steerWheel.down.type, node.data.steerWheel.down.key));
        p->drawText(QRect(center.x() - 46 - 42, center.y() - 8, 40, 16), Qt::AlignCenter,
                    editKeyLabel(node.data.steerWheel.left.type, node.data.steerWheel.left.key));
        p->drawText(QRect(center.x() + 46 + 2, center.y() - 8, 40, 16), Qt::AlignCenter,
                    editKeyLabel(node.data.steerWheel.right.type, node.data.steerWheel.right.key));
        p->setFont(labelFont);
        break;
    }
    case KeyMap::KMT_DRAG: {
        QPoint start = editNodeDisplayCenter(node, videoRect, 16);
        QPoint end = editClampDisplayPoint(editDragEndPos(node, index), videoRect, 8);
        QPen linePen(QColor(255, 213, 74), selected ? 3 : 2);
        p->setPen(linePen);
        p->setBrush(linePen.color());
        QLineF line(start, end);
        p->drawLine(line);
        if (line.length() > 1.0) {
            QPointF dir = line.unitVector().p2() - line.unitVector().p1();
            QPointF perp(-dir.y(), dir.x());
            QPointF tip(end);
            QPolygonF arrow;
            arrow << tip << (tip - dir * 12 + perp * 6) << (tip - dir * 12 - perp * 6);
            p->drawPolygon(arrow);
        }
        p->setPen(pen);
        p->setBrush(QColor(0, 0, 0, 140));
        p->drawEllipse(start, 16, 16);
        p->setPen(Qt::white);
        editDrawMarkerLabel(p, start, editNodeKeyLabel(node));
        p->setPen(pen);
        p->setBrush(QColor(0, 0, 0, 140));
        p->drawRect(end.x() - 7, end.y() - 7, 14, 14);
        break;
    }
    case KeyMap::KMT_MOUSE_MOVE: {
        QPoint center = editNodeDisplayCenter(node, videoRect, 16);
        p->setPen(pen);
        p->setBrush(QColor(0, 0, 0, 140));
        p->drawRoundedRect(center.x() - 14, center.y() - 10, 28, 20, 4, 4);
        p->drawEllipse(QPoint(center.x(), center.y()), 5, 5);
        p->setPen(Qt::white);
        QFont smallFont = labelFont;
        smallFont.setPointSize(9);
        p->setFont(smallFont);
        p->drawText(QRect(center.x() - 30, center.y() + 12, 60, 15), Qt::AlignCenter, QString("Cámara"));
        p->setFont(labelFont);
        break;
    }
    case KeyMap::KMT_CLICK:
    case KeyMap::KMT_CLICK_TWICE:
    case KeyMap::KMT_CLICK_MULTI:
    case KeyMap::KMT_ANDROID_KEY: {
        QPoint center = editNodeDisplayCenter(node, videoRect, 16);
        p->setPen(pen);
        p->setBrush(QColor(0, 0, 0, 140));
        p->drawEllipse(center, 16, 16);
        p->setPen(Qt::white);
        editDrawMarkerLabel(p, center, editNodeKeyLabel(node));
        if (node.type == KeyMap::KMT_CLICK_MULTI) {
            int count = node.data.clickMulti.keyNode.delayClickNodesCount;
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(255, 193, 7));
            p->drawEllipse(QPoint(center.x() + 13, center.y() - 13), 8, 8);
            p->setPen(Qt::black);
            QFont countFont = labelFont;
            countFont.setPointSize(8);
            p->setFont(countFont);
            p->drawText(QRect(center.x() + 5, center.y() - 21, 16, 16), Qt::AlignCenter, QString::number(count));
            p->setFont(labelFont);
        }
        break;
    }
    default:
        break;
    }

    if (awaiting) {
        // Anillo discontinuo: este nodo está esperando una tecla.
        int r = (node.type == KeyMap::KMT_STEER_WHEEL) ? 46 : 16;
        QPoint center = editNodeDisplayCenter(node, videoRect, r);
        QPen awaitPen(QColor(255, 193, 7), 2, Qt::DashLine);
        p->setPen(awaitPen);
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(center, r + 8, r + 8);
    }
}

void VideoForm::paintEditOverlay(QPainter *painter)
{
    if (!m_editing || m_editKeyMap.isNull()) {
        return;
    }
    QRect videoRect = editVideoRectLocal();
    if (videoRect.isEmpty()) {
        return;
    }
    QPainter &p = *painter;
    p.setRenderHint(QPainter::Antialiasing, true);
    // Oscurecer un poco el vídeo para que se vean los marcadores.
    p.fillRect(videoRect, QColor(0, 0, 0, 70));

    // Paleta
    QRect bar = editPaletteBarRect(videoRect);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 20, 30, 210));
    p.drawRoundedRect(bar, 8, 8);
    QFont buttonFont = p.font();
    buttonFont.setPointSize(10);
    buttonFont.setBold(true);
    p.setFont(buttonFont);
    for (const EditPaletteButton &button : editPaletteButtons(videoRect)) {
        bool armed = (button.id == "click" && m_editTool == EditTool::Click)
                     || (button.id == "wheel" && m_editTool == EditTool::SteerWheel)
                     || (button.id == "aim" && m_editTool == EditTool::Aim)
                     || (button.id == "drag" && m_editTool == EditTool::Drag)
                     || (button.id == "switch" && m_editAwaitingSwitchKey)
                     || (button.id == "camera" && !m_editKeyMap.isNull() && m_editKeyMap->isValidMouseMoveMap());
        QColor bg;
        if (button.id == "exit") {
            bg = QColor(180, 60, 60, 230);
        } else if (button.id == "save") {
            bg = QColor(0, 150, 80, 230);
        } else if (armed) {
            bg = QColor(255, 170, 0, 230);
        } else {
            bg = QColor(70, 70, 90, 230);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(button.rect, 6, 6);
        p.setPen(Qt::white);
        p.drawText(button.rect, Qt::AlignCenter, button.text);
    }

    // Pista de ayuda
    QRect hintRect = editHintRect(videoRect);
    QFont hintFont = p.font();
    hintFont.setPointSize(9);
    hintFont.setBold(false);
    p.setFont(hintFont);
    p.setPen(QColor(255, 255, 255, 230));
    QString hint = QFontMetrics(hintFont).elidedText(m_editHint, Qt::ElideRight, hintRect.width());
    p.drawText(hintRect, Qt::AlignLeft | Qt::AlignVCenter, hint);

    // Nodos
    const QVector<KeyMap::KeyMapNode> &nodes = m_editKeyMap->keyMapNodes();
    for (int i = 0; i < nodes.size(); ++i) {
        editDrawNode(&p, nodes.at(i), i, videoRect);
    }
}

QPointF VideoForm::editOffsetPos(const QPointF &pos, const QPointF &delta) const
{
    return QPointF(qBound(0.0, pos.x() + delta.x(), 1.0), qBound(0.0, pos.y() + delta.y(), 1.0));
}

void VideoForm::editClearNodeKeys(KeyMap::KeyMapNode &node) const
{
    // Dejar la tecla sin asignar para que la copia nunca repita la
    // tecla del original (dos controles con la misma tecla serían
    // ambiguos). La copia queda en espera de tecla.
    switch (node.type) {
    case KeyMap::KMT_CLICK:
    case KeyMap::KMT_CLICK_TWICE:
        node.data.click.keyNode.type = KeyMap::AT_KEY;
        node.data.click.keyNode.key = Qt::Key_unknown;
        break;
    case KeyMap::KMT_CLICK_MULTI:
        node.data.clickMulti.keyNode.type = KeyMap::AT_KEY;
        node.data.clickMulti.keyNode.key = Qt::Key_unknown;
        break;
    case KeyMap::KMT_DRAG:
        node.data.drag.keyNode.type = KeyMap::AT_KEY;
        node.data.drag.keyNode.key = Qt::Key_unknown;
        break;
    case KeyMap::KMT_ANDROID_KEY:
        node.data.androidKey.keyNode.type = KeyMap::AT_KEY;
        node.data.androidKey.keyNode.key = Qt::Key_unknown;
        break;
    default:
        break;
    }
}

void VideoForm::editPlaceNode(const QPointF &rel)
{
    if (m_editKeyMap.isNull()) {
        return;
    }
    QPointF pos(qBound(0.0, rel.x(), 1.0), qBound(0.0, rel.y(), 1.0));
    KeyMap::KeyMapNode node;
    EditTool tool = m_editTool;
    m_editTool = EditTool::None;
    switch (tool) {
    case EditTool::Click:
    case EditTool::Aim:
        // Mirilla es un KMT_CLICK normal: el mantener pulsado mientras
        // la tecla está abajo ya es el comportamiento de KMT_CLICK.
        node.type = KeyMap::KMT_CLICK;
        node.data.click.keyNode = KeyMap::KeyNode(KeyMap::AT_KEY, Qt::Key_unknown, pos);
        node.data.click.switchMap = false;
        break;
    case EditTool::SteerWheel:
        node.type = KeyMap::KMT_STEER_WHEEL;
        node.data.steerWheel.centerPos = pos;
        node.data.steerWheel.left = KeyMap::KeyNode(KeyMap::AT_KEY, Qt::Key_A, QPointF(), QPointF(), 0.05);
        node.data.steerWheel.right = KeyMap::KeyNode(KeyMap::AT_KEY, Qt::Key_D, QPointF(), QPointF(), 0.05);
        node.data.steerWheel.up = KeyMap::KeyNode(KeyMap::AT_KEY, Qt::Key_W, QPointF(), QPointF(), 0.11);
        node.data.steerWheel.down = KeyMap::KeyNode(KeyMap::AT_KEY, Qt::Key_S, QPointF(), QPointF(), 0.11);
        break;
    case EditTool::Drag: {
        node.type = KeyMap::KMT_DRAG;
        QPointF endPos(qBound(0.0, pos.x() + 0.1, 1.0), pos.y());
        node.data.drag.keyNode = KeyMap::KeyNode(KeyMap::AT_KEY, Qt::Key_unknown, pos, endPos);
        node.data.drag.startDelay = 0;
        node.data.drag.dragSpeed = 1.0f;
        break;
    }
    default:
        return;
    }
    if (!m_editKeyMap->addNode(node)) {
        setEditHint(QString("No se pudo añadir el control."));
        update();
        return;
    }
    int index = m_editKeyMap->keyMapNodes().size() - 1;
    m_editSelected = index;
    if (tool == EditTool::SteerWheel) {
        m_editAwaitingKey = -1;
        setEditHint(QString("Joystick colocado (WASD). Arrástralo para moverlo."));
    } else {
        m_editAwaitingKey = index;
        setEditHint(QString("Pulsa una tecla para asignar el control… (Esc cancela)"));
    }
    update();
}

void VideoForm::editDuplicateNode(int index)
{
    if (m_editKeyMap.isNull()) {
        return;
    }
    const QVector<KeyMap::KeyMapNode> &nodes = m_editKeyMap->keyMapNodes();
    if (index < 0 || index >= nodes.size()) {
        return;
    }
    const KeyMap::KeyMapNode &src = nodes.at(index);
    // El joystick no se puede duplicar con teclas nuevas: setNodeKey no
    // reasigna sus 4 teclas de dirección, así que la copia quedaría
    // inútil. La cámara tampoco: solo puede haber una y addNode
    // reemplazaría la original. En ambos casos, colocar uno nuevo.
    if (src.type == KeyMap::KMT_STEER_WHEEL || src.type == KeyMap::KMT_MOUSE_MOVE) {
        setEditHint(QString("Este control no se puede duplicar; coloca uno nuevo."));
        update();
        return;
    }
    // Copia campo a campo (sin copiar el struct entero) con un
    // desplazamiento de (+0.03, +0.03) y las teclas limpias.
    KeyMap::KeyMapNode copy;
    copy.type = src.type;
    QPointF delta(0.03, 0.03);
    switch (src.type) {
    case KeyMap::KMT_CLICK:
    case KeyMap::KMT_CLICK_TWICE:
        // KMT_CLICK_TWICE comparte el almacenamiento con KMT_CLICK.
        copy.data.click.keyNode = src.data.click.keyNode;
        copy.data.click.keyNode.pos = editOffsetPos(src.data.click.keyNode.pos, delta);
        copy.data.click.switchMap = src.data.click.switchMap;
        break;
    case KeyMap::KMT_CLICK_MULTI:
        copy.data.clickMulti.keyNode = src.data.clickMulti.keyNode;
        for (int i = 0; i < copy.data.clickMulti.keyNode.delayClickNodesCount; ++i) {
            copy.data.clickMulti.keyNode.delayClickNodes[i].pos =
                editOffsetPos(copy.data.clickMulti.keyNode.delayClickNodes[i].pos, delta);
        }
        break;
    case KeyMap::KMT_DRAG:
        copy.data.drag.keyNode = src.data.drag.keyNode;
        copy.data.drag.keyNode.pos = editOffsetPos(src.data.drag.keyNode.pos, delta);
        copy.data.drag.keyNode.extendPos = editOffsetPos(src.data.drag.keyNode.extendPos, delta);
        copy.data.drag.startDelay = src.data.drag.startDelay;
        copy.data.drag.dragSpeed = src.data.drag.dragSpeed;
        break;
    case KeyMap::KMT_ANDROID_KEY:
        copy.data.androidKey.keyNode = src.data.androidKey.keyNode;
        break;
    default:
        return;
    }
    editClearNodeKeys(copy);
    if (!m_editKeyMap->addNode(copy)) {
        setEditHint(QString("No se pudo duplicar el control."));
        update();
        return;
    }
    m_editSelected = m_editKeyMap->keyMapNodes().size() - 1;
    m_editAwaitingKey = m_editSelected;
    setEditHint(QString("Copia creada: pulsa una tecla para asignarla… (Esc cancela)"));
    update();
}

void VideoForm::editCommitDragEnd(int index, const QPointF &endPos)
{
    if (m_editKeyMap.isNull()) {
        return;
    }
    const QVector<KeyMap::KeyMapNode> &nodes = m_editKeyMap->keyMapNodes();
    if (index < 0 || index >= nodes.size()) {
        return;
    }
    const KeyMap::KeyMapNode &src = nodes.at(index);
    if (src.type != KeyMap::KMT_DRAG) {
        return;
    }
    // setNodePos solo mueve el arrastre entero (inicio y fin juntos), así
    // que mover solo el extremo se hace con una copia con el fin nuevo:
    // primero se añade la copia y luego se borra el original, para no
    // perder el nodo si algo falla.
    KeyMap::KeyMapNode copy;
    copy.type = KeyMap::KMT_DRAG;
    copy.data.drag.keyNode = src.data.drag.keyNode;
    copy.data.drag.keyNode.extendPos = QPointF(qBound(0.0, endPos.x(), 1.0), qBound(0.0, endPos.y(), 1.0));
    copy.data.drag.startDelay = src.data.drag.startDelay;
    copy.data.drag.dragSpeed = src.data.drag.dragSpeed;
    if (!m_editKeyMap->addNode(copy)) {
        return;
    }
    if (!m_editKeyMap->removeNode(index)) {
        return;
    }
    m_editSelected = m_editKeyMap->keyMapNodes().size() - 1;
}

bool VideoForm::isEditModifierKey(int key) const
{
    switch (key) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_AltGr:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_Hyper_L:
    case Qt::Key_Hyper_R:
        return true;
    default:
        return false;
    }
}

bool VideoForm::editHandleMousePress(QMouseEvent *event)
{
    if (m_editKeyMap.isNull()) {
        return false;
    }
    // Fase 3: capturando la tecla de cambio: un clic (izq./der.) sobre el
    // vídeo la asigna como botón del ratón (AT_MOUSE). Un clic sobre el
    // botón "Cambio" de la paleta cancela la captura; un clic sobre otro
    // botón de la paleta cae al despacho normal de abajo; fuera del vídeo
    // no se asigna nada.
    if (m_editAwaitingSwitchKey
        && (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)) {
        QWidget *vw = videoWidget();
        if (vw && editVideoRect().contains(event->pos())) {
            QPoint local = vw->mapFrom(this, event->pos());
            QString paletteHit = editPaletteHit(local);
            if (paletteHit == "switch") {
                m_editAwaitingSwitchKey = false;
                setEditHint(editDefaultHint());
                update();
                event->accept();
                return true;
            }
            if (paletteHit.isEmpty()) {
                int mouseKey = (event->button() == Qt::LeftButton) ? Qt::LeftButton : Qt::RightButton;
                if (editKeyUsedByNode(KeyMap::AT_MOUSE, mouseKey)) {
                    setEditHint(QString("Ese botón ya lo usa un control, elige otro."));
                } else {
                    m_editKeyMap->setSwitchKey(KeyMap::AT_MOUSE, mouseKey);
                    m_editAwaitingSwitchKey = false;
                    setEditHint(QString("Tecla de cambio: %1").arg(editSwitchKeyLabel()));
                }
                update();
                event->accept();
                return true;
            }
        }
    }
    if (event->button() != Qt::LeftButton) {
        // En edición ningún botón dispara acciones del juego.
        event->accept();
        return true;
    }
    QWidget *vw = videoWidget();
    if (!vw) {
        return false;
    }
    // Fuera del vídeo, comportamiento normal (arrastrar la ventana).
    if (!editVideoRect().contains(event->pos())) {
        return false;
    }
    QPoint local = vw->mapFrom(this, event->pos());
    QString hit = editPaletteHit(local);
    if (!hit.isEmpty()) {
        // Cualquier otro botón de la paleta cancela la captura de la tecla
        // de cambio (el botón "Cambio" la conmuta en su propia rama).
        if (hit != "switch") {
            m_editAwaitingSwitchKey = false;
        }
        if (hit == "click") {
            m_editTool = (m_editTool == EditTool::Click) ? EditTool::None : EditTool::Click;
            setEditHint(m_editTool == EditTool::Click ? QString("Toque: haz clic en el vídeo para colocarlo.")
                                                      : editDefaultHint());
        } else if (hit == "wheel") {
            m_editTool = (m_editTool == EditTool::SteerWheel) ? EditTool::None : EditTool::SteerWheel;
            setEditHint(m_editTool == EditTool::SteerWheel ? QString("Joystick: haz clic en el vídeo para colocarlo (WASD).")
                                                           : editDefaultHint());
        } else if (hit == "aim") {
            m_editTool = (m_editTool == EditTool::Aim) ? EditTool::None : EditTool::Aim;
            setEditHint(m_editTool == EditTool::Aim ? QString("Mirilla: haz clic en el vídeo para colocarla.")
                                                    : editDefaultHint());
        } else if (hit == "drag") {
            m_editTool = (m_editTool == EditTool::Drag) ? EditTool::None : EditTool::Drag;
            setEditHint(m_editTool == EditTool::Drag ? QString("Arrastre: haz clic en el vídeo para colocar el inicio.")
                                                     : editDefaultHint());
        } else if (hit == "switch") {
            m_editAwaitingSwitchKey = !m_editAwaitingSwitchKey;
            m_editTool = EditTool::None;
            m_editAwaitingKey = -1;
            setEditHint(m_editAwaitingSwitchKey ? QString("Pulsa una tecla o haz clic para la tecla de cambio… (Esc cancela)")
                                                : editDefaultHint());
        } else if (hit == "camera") {
            m_editTool = EditTool::None;
            editToggleMouseMoveMap();
        } else if (hit == "save") {
            saveKeymapEdit();
            event->accept();
            return true;
        } else if (hit == "exit") {
            exitKeymapEdit();
            event->accept();
            return true;
        }
        update();
        event->accept();
        return true;
    }

    QPointF rel = widgetToRel(event->pos());
    bool hitEnd = false;
    int hitNode = editHitTestNode(local, &hitEnd);
    if (hitNode >= 0) {
        m_editSelected = hitNode;
        m_editAwaitingKey = -1;
        m_editDragging = true;
        m_editDragLast = event->pos();
        m_editDragEndHandle = hitEnd;
        if (hitEnd) {
            m_editDragEndPending = editDragEndPos(m_editKeyMap->keyMapNodes().at(hitNode), hitNode);
            m_editDragEndPendingValid = true;
        }
        setEditHint(QString("Control seleccionado. Arrástralo para moverlo o pulsa una tecla para cambiarla."));
        update();
        event->accept();
        return true;
    }
    if (m_editTool != EditTool::None) {
        editPlaceNode(rel);
        event->accept();
        return true;
    }
    m_editSelected = -1;
    m_editAwaitingKey = -1;
    setEditHint(editDefaultHint());
    update();
    event->accept();
    return true;
}

bool VideoForm::editHandleMouseMove(QMouseEvent *event)
{
    if (m_editKeyMap.isNull()) {
        return false;
    }
    if (!m_editDragging) {
        // Dentro del vídeo no se reenvía nada al dispositivo en edición.
        return editVideoRect().contains(event->pos());
    }
    QPointF rel = widgetToRel(event->pos());
    QPointF lastRel = widgetToRel(m_editDragLast);
    QPointF delta = rel - lastRel;
    m_editDragLast = event->pos();
    if (m_editDragEndHandle && m_editDragEndPendingValid) {
        // Vista previa del extremo; se confirma al soltar el botón.
        m_editDragEndPending = QPointF(qBound(0.0, m_editDragEndPending.x() + delta.x(), 1.0),
                                       qBound(0.0, m_editDragEndPending.y() + delta.y(), 1.0));
        update();
        event->accept();
        return true;
    }
    if (m_editSelected < 0) {
        event->accept();
        return true;
    }
    const QVector<KeyMap::KeyMapNode> &nodes = m_editKeyMap->keyMapNodes();
    if (m_editSelected >= nodes.size()) {
        event->accept();
        return true;
    }
    QPointF anchor = editNodeAnchor(nodes.at(m_editSelected));
    m_editKeyMap->setNodePos(m_editSelected, anchor + delta);
    update();
    event->accept();
    return true;
}

bool VideoForm::editHandleMouseRelease(QMouseEvent *event)
{
    if (m_editKeyMap.isNull()) {
        return false;
    }
    if (m_editDragging) {
        bool wasEndHandle = m_editDragEndHandle;
        int index = m_editSelected;
        QPointF endPos = m_editDragEndPending;
        m_editDragging = false;
        m_editDragEndHandle = false;
        m_editDragEndPendingValid = false;
        if (wasEndHandle && index >= 0) {
            editCommitDragEnd(index, endPos);
        }
        update();
        event->accept();
        return true;
    }
    // Sin arrastre: dentro del vídeo no se reenvía la liberación.
    if (editVideoRect().contains(event->pos())) {
        event->accept();
        return true;
    }
    return false;
}

void VideoForm::editHandleKeyPress(QKeyEvent *event)
{
    event->accept();
    if (event->isAutoRepeat()) {
        return;
    }
    if (m_editKeyMap.isNull()) {
        return;
    }
    int key = event->key();

    if (key == Qt::Key_Escape) {
        if (m_editAwaitingSwitchKey) {
            m_editAwaitingSwitchKey = false;
            setEditHint(editDefaultHint());
            update();
        } else if (m_editAwaitingKey >= 0) {
            m_editAwaitingKey = -1;
            setEditHint(editDefaultHint());
            update();
        } else {
            exitKeymapEdit();
        }
        return;
    }
    if ((event->modifiers() & Qt::ControlModifier) && key == Qt::Key_S) {
        saveKeymapEdit();
        return;
    }

    // Fase 3: capturando la tecla de cambio de modo.
    if (m_editAwaitingSwitchKey) {
        if (isEditModifierKey(key)) {
            return;
        }
        if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
            return;
        }
        if (editKeyUsedByNode(KeyMap::AT_KEY, key)) {
            setEditHint(QString("Esa tecla ya la usa un control, elige otra."));
            update();
            return;
        }
        m_editKeyMap->setSwitchKey(KeyMap::AT_KEY, key);
        m_editAwaitingSwitchKey = false;
        setEditHint(QString("Tecla de cambio: %1").arg(editSwitchKeyLabel()));
        update();
        return;
    }

    if (m_editAwaitingKey >= 0) {
        if (isEditModifierKey(key)) {
            return;
        }
        if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
            return;
        }
        if (key == m_editKeyMap->getSwitchKey()) {
            setEditHint(QString("Esa tecla es la de cambio de mapa, elige otra."));
            update();
            return;
        }
        if (m_editKeyMap->setNodeKey(m_editAwaitingKey, KeyMap::AT_KEY, key)) {
            setEditHint(QString("Tecla asignada: %1").arg(editKeyLabel(KeyMap::AT_KEY, key)));
        } else {
            setEditHint(QString("Esa tecla no se puede asignar a este control."));
        }
        m_editAwaitingKey = -1;
        update();
        return;
    }

    if ((event->modifiers() & Qt::ControlModifier) && key == Qt::Key_D) {
        if (m_editSelected >= 0) {
            editDuplicateNode(m_editSelected);
        }
        return;
    }

    if (m_editSelected < 0) {
        return;
    }
    if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
        if (m_editKeyMap->removeNode(m_editSelected)) {
            setEditHint(QString("Control borrado."));
        }
        m_editSelected = -1;
        m_editAwaitingKey = -1;
        update();
        return;
    }
    if (isEditModifierKey(key)) {
        return;
    }
    if (event->modifiers() != Qt::NoModifier) {
        return;
    }
    if (key == m_editKeyMap->getSwitchKey()) {
        setEditHint(QString("Esa tecla es la de cambio de mapa, elige otra."));
        update();
        return;
    }
    if (m_editKeyMap->setNodeKey(m_editSelected, KeyMap::AT_KEY, key)) {
        setEditHint(QString("Tecla asignada: %1").arg(editKeyLabel(KeyMap::AT_KEY, key)));
        update();
    }
}
