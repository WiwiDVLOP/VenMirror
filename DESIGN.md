# VenMirror — Diseño del fork de QtScrcpy con editor visual de teclas

Nombre confirmado: **VenMirror** (Winel lo confirmó 2026-11-10). Origen: fork de
barry-ran/QtScrcpy (Apache-2.0) + submódulo VenMirrorCore (fork de QtScrcpyCore).
Repo: https://github.com/WiwiDVLOP/VenMirror (rama `dev`). La licencia Apache
permite el fork con otro nombre; hay que conservar los avisos de licencia y
los créditos originales. El exe de Windows se compila con GitHub Actions
(gratis); nadie compila a mano en la PC de Winel.

## El problema que resuelve
Hoy mapear teclas en QtScrcpy es editar un JSON a mano: cada botón nuevo
exige captura, medir coordenadas y reescribir el archivo en dos carpetas
(`keymap` de instalación y `AppData\Roaming\QtScrcpy\QtScrcpy\keymap`, que es
la que realmente lee el programa). Con el editor visual estilo LDPlayer,
Winel arrastra los controles sobre el juego, pulsa una tecla para asignarla
y guarda: el programa escribe el JSON y lo aplica en vivo.

## Mapa del código (estudiado 2026-11-10)
- `QtScrcpy/ui/videoform.{h,cpp}` (1071 líneas): la ventana del video.
  Tiene `mousePress/Move/Release`, `keyPress/Release`, `paintEvent`,
  `videoWidget()` y `frameSize()`. Aquí vive el editor: un modo edición que
  dibuja los controles sobre el video e intercepta mouse/teclado.
- `QtScrcpy/ui/dialog.{h,cpp}` (1783 líneas): la ventana principal.
  `getGameScript()` lee el mapa (primero carpeta de usuario, luego
  instalación), `on_applyScriptBtn_clicked()` lo aplica,
  `ui->gameBox` es el selector. Aquí va el botón "Editar mapa" y el
  guardado debe escribir en la carpeta de usuario (la que manda).
- `QtScrcpyCore/.../inputconvert/keymap/keymap.{h,cpp}` (554 líneas):
  el modelo. `loadKeyMap(json)` parsea a `m_keyMapNodes` + `m_switchKey`.
  NO tiene serializador: hay que añadir `saveKeyMap()` en el core, junto
  al parser, para que el JSON escrito sea idéntico al leído.
  Tipos: KMT_CLICK, KMT_CLICK_TWICE, KMT_CLICK_MULTI, KMT_STEER_WHEEL
  (joystick), KMT_DRAG, KMT_MOUSE_MOVE (cámara), KMT_ANDROID_KEY.
  Las teclas aceptan `Key_*` y botones de mouse (`LeftButton`,
  `RightButton`).
- `QtScrcpyCore/.../inputconvert/inputconvertgame.{h,cpp}`: convierte
  teclas/mouse en toques. `switchGameMap()` cambia normal/custom; con
  mouseMoveMap válido esconde el cursor. Mientras se edita, el dispositivo
  debe estar en modo normal para que los clics no disparen acciones.
- `ci/`: workflows de compilación. Revisar el de Windows (versión de Qt,
  artefacto del exe) antes de la Fase 1.

## Limitaciones del motor que el diseño debe respetar
- El mensaje "Press ~ key to switch keymap mode" está fijo en el core
  (`keymap.cpp:319`): siempre dice ~ aunque la tecla real sea otra.
  En VenMirror se cambia por la tecla real del mapa.
- La cámara por mouse (`mouseMoveMap`) es todo o nada con el cursor
  escondido; el clic derecho sostenido NO mueve la cámara (en modo normal
  esos movimientos se descartan). El editor lo expone como interruptor,
  sin prometer lo imposible.
- `smallEyes` solo responde a teclas, no a botones del mouse.

## Fases
- **Fase 0 (hecha):** fork local en `~/workspace/venmirror/` + este diseño.
- **Fase 1 — Renombrado + CI:** cambiar marca a VenMirror (título, about,
  `AppDataLocation`), conservar LICENSE/NOTICE, y dejar GitHub Actions
  compilando el exe de Windows. Sin esto no hay binario que probar.
- **Fase 2 — Editor visual v1:** modo edición en VideoForm: muestra los
  controles del mapa actual sobre el video; arrastrar = mover; clic =
  seleccionar; pulsar tecla = asignar; paleta para añadir toque, joystick,
  mirilla (clic sostenido) y arrastre; borrar/duplicar; guardar JSON en la
  carpeta de usuario + aplicar en vivo sin reiniciar el espejo.
- **Fase 3 — Pulido LDPlayer:** cambio de modo (F) configurable desde el
  editor, interruptor de cámara por mouse, perfiles por juego (un mapa por
  juego, p. ej. MHO), importar/exportar, y que el primer mapa de MHO de
  Winel (16 teclas) venga incluido como ejemplo.
- **Fase 4 — Release:** v1.0 en GitHub (repo WiwiDVLOP/VenMirror) con el
  ZIP del exe, como se hizo con VenCode v3.4.0.

## Decisiones pendientes de Winel
- Nombre final (VenMirror es el propuesto; vale otro).
- Crear el repo en GitHub (lo hace él, o Kalvin lo hace por navegador con
  su sesión, como con VenCode). Sin repo no hay CI ni release.
- El mapa actual de MHO (16 teclas) se incluye como ejemplo en la Fase 3.
