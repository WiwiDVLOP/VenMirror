#ifndef VIDEOFORM_H
#define VIDEOFORM_H

#include <QPointer>
#include <QScopedPointer>
#include <QTimer>
#include <QWidget>

#include "../QtScrcpyCore/include/QtScrcpyCore.h"
#include "keymap.h"

namespace Ui
{
    class videoForm;
}

class ToolForm;
class FileHandler;
class QYUVOpenGLWidget;
class QLabel;
class MetalVideoWidget;
class EditOverlayWidget;
class QShortcut;
class QPainter;
class VideoForm : public QWidget, public qsc::DeviceObserver
{
    Q_OBJECT
public:
    explicit VideoForm(bool framelessWindow = false, bool skin = true, bool showToolBar = true, int decodeMode = 0, QWidget *parent = 0);
    ~VideoForm();

    void staysOnTop(bool top = true);
    void updateShowSize(const QSize &newSize);
    void updateRender(int width, int height, uint8_t* dataY, uint8_t* dataU, uint8_t* dataV, int linesizeY, int linesizeU, int linesizeV);
    void setSerial(const QString& serial);
    QRect getGrabCursorRect();
    const QSize &frameSize();
    void resizeSquare();
    void removeBlackRect();
    void showFPS(bool show);
    void switchFullScreen();
    bool isHost();

    // Editor visual de keymap (VenMirror Fase 2): entra en modo edición
    // sobre el vídeo. filePath es la ruta absoluta del archivo en el
    // directorio de keymaps del usuario; jsonText es su contenido (vacío
    // para empezar un mapa nuevo con la tecla de cambio F).
    void openKeymapEditor(const QString &filePath, const QString &jsonText);
    // Dibuja la paleta y los nodos del editor; lo usa el overlay
    // transparente que flota sobre el vídeo (ver EditOverlayWidget).
    void paintEditOverlay(QPainter *painter);

private:
    void onFrame(int width, int height, uint8_t* dataY, uint8_t* dataU, uint8_t* dataV,
                 int linesizeY, int linesizeU, int linesizeV) override;
    // VideoToolbox Metal 路径帧回调（仅 macOS arm64）
    void onFrameMetal(void* cvPixelBuffer, int width, int height) override;
    void updateFPS(quint32 fps) override;
    void onVideoSessionChanged(const QSize &size, bool clientResized) override;
    void grabCursor(bool grab) override;

    void updateStyleSheet(bool vertical);
    QMargins getMargins(bool vertical);
    void initUI();

    void showToolForm(bool show = true);
    void moveCenter();
    void installShortcut();
    QRect getScreenRect();
    // 窗口越界回收：显示器插拔后，若窗口落在任何屏幕的可用区域之外，
    // 将其移回主屏居中，避免成为看不见的"幽灵窗口"。
    void ensureOnScreen();
    // 是否与任一屏幕的可用区域有交集（含边界相交判定）
    bool isRectOnAnyScreen(const QRect &rect) const;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    // 获取当前视频渲染 widget（OpenGL 或 Metal 容器）
    QWidget* videoWidget() const;
    // 是否使用 Metal 渲染路径
    bool isMetalMode() const;

    // Editor visual de keymap (VenMirror Fase 2)
    enum class EditTool { None, Click, SteerWheel, Aim, Drag };
    struct EditPaletteButton { QString id; QString text; QRect rect; };

    void exitKeymapEdit();
    bool saveKeymapEdit();
    void setEditHint(const QString &hint);
    QString editDefaultHint() const;
    void updateEditOverlay();
    // Rectángulo del vídeo tal como se ve, en coordenadas de VideoForm
    // (con letterbox: el mayor rectángulo con el aspecto del frame que
    // cabe dentro del widget de vídeo, centrado).
    QRect editVideoRect() const;
    // Lo mismo en coordenadas locales del widget de vídeo (= del overlay).
    QRect editVideoRectLocal() const;
    QPointF widgetToRel(const QPoint &p) const;
    QPoint relToWidget(const QPointF &rel) const;
    QPointF editLocalToRel(const QPoint &p, const QRect &videoRect) const;
    QPoint editRelToLocal(const QPointF &rel, const QRect &videoRect) const;
    QRect editPaletteBarRect(const QRect &videoRect) const;
    QRect editHintRect(const QRect &videoRect) const;
    QVector<EditPaletteButton> editPaletteButtons(const QRect &videoRect) const;
    QString editPaletteHit(const QPoint &localPos) const;
    QPointF editNodeAnchor(const KeyMap::KeyMapNode &node) const;
    QPointF editDragEndPos(const KeyMap::KeyMapNode &node, int index) const;
    QPoint editClampDisplayPoint(const QPointF &rel, const QRect &videoRect, int radius) const;
    QPoint editNodeDisplayCenter(const KeyMap::KeyMapNode &node, const QRect &videoRect, int radius) const;
    int editHitTestNode(const QPoint &localPos, bool *hitEndHandle) const;
    QString editKeyLabel(KeyMap::ActionType type, int key) const;
    QString editNodeKeyLabel(const KeyMap::KeyMapNode &node) const;
    bool editNodeHasKey(const KeyMap::KeyMapNode &node) const;
    void editDrawNode(QPainter *p, const KeyMap::KeyMapNode &node, int index, const QRect &videoRect) const;
    void editDrawMarkerLabel(QPainter *p, const QPoint &center, const QString &label) const;
    void editPlaceNode(const QPointF &rel);
    void editDuplicateNode(int index);
    void editCommitDragEnd(int index, const QPointF &endPos);
    void editClearNodeKeys(KeyMap::KeyMapNode &node) const;
    QPointF editOffsetPos(const QPointF &pos, const QPointF &delta) const;
    bool editHandleMousePress(QMouseEvent *event);
    bool editHandleMouseMove(QMouseEvent *event);
    bool editHandleMouseRelease(QMouseEvent *event);
    void editHandleKeyPress(QKeyEvent *event);
    bool isEditModifierKey(int key) const;

    // ui
    Ui::videoForm *ui;
    QPointer<ToolForm> m_toolForm;
    QPointer<QWidget> m_loadingWidget;
    QPointer<QYUVOpenGLWidget> m_videoWidget;

    // Metal 渲染路径（仅 macOS arm64）
    QPointer<MetalVideoWidget> m_metalWidget;

    QPointer<QLabel> m_fpsLabel;

    //inside member
    QSize m_frameSize;
    QSize m_normalSize;
    QPoint m_dragPosition;
    float m_widthHeightRatio = 0.5f;
    bool m_skin = true;
    QPoint m_fullScreenBeforePos;
    QString m_serial;
    int m_decodeMode = 0;
    bool m_metalFirstFrame = true;  // Metal 首次帧标记
    bool m_flexDisplay = false;
    bool m_preventAutoResize = false;
    QTimer m_flexResizeTimer;
    QSize m_pendingDisplaySize;

    //Whether to display the toolbar when connecting a device.
    bool show_toolbar = true;

    // Editor visual de keymap (VenMirror Fase 2). m_editKeyMap es un
    // puntero y no un miembro por valor porque KeyMap::loadKeyMap
    // acumula nodos en vez de limpiarlos: cada apertura del editor
    // necesita un KeyMap nuevo. Sin padre: lo posee el QScopedPointer.
    QScopedPointer<KeyMap> m_editKeyMap;
    QString m_editFilePath;
    bool m_editing = false;
    int m_editSelected = -1;
    bool m_editWasCustom = false;
    bool m_editHadDevice = false;
    EditTool m_editTool = EditTool::None;
    int m_editAwaitingKey = -1;
    bool m_editDragging = false;
    bool m_editDragEndHandle = false;
    QPoint m_editDragLast;
    QPointF m_editDragEndPending;
    bool m_editDragEndPendingValid = false;
    QString m_editHint;
    QPointer<EditOverlayWidget> m_editOverlay;
    // Atajo Ctrl+S (cambiar app): se desactiva en edición para que no
    // dispare al mismo tiempo que Ctrl+S = guardar mapa.
    QPointer<QShortcut> m_shortcutAppSwitch;
};

// Overlay transparente sobre el vídeo que dibuja el editor de keymap.
// No recibe eventos de ratón (WA_TransparentForMouseEvents): los clics
// siguen llegando a VideoForm. Es hijo del widget de vídeo, así que
// sigue su geometría; VideoForm lo recoloca en updateEditOverlay().
class EditOverlayWidget : public QWidget
{
    Q_OBJECT
public:
    explicit EditOverlayWidget(VideoForm *form, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    VideoForm *m_form;
};

#endif // VIDEOFORM_H
