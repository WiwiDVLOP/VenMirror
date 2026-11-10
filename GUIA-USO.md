# VenMirror — Guía de uso

VenMirror espeja la pantalla de tu teléfono Android en la PC y te deja jugar
con teclado y ratón. Es un fork de QtScrcpy con un editor visual de teclas
estilo LDPlayer integrado: arrastras los controles sobre el juego, pulsas
una tecla para asignarla y guardas.

Versión de esta guía: **v1.0.2**.

## 1. Descargar e instalar

1. Descarga el ZIP de la release
   https://github.com/WiwiDVLOP/VenMirror/releases/tag/v1.0.2
   - **VenMirror-win-x64-v1.0.2.zip — 32.643.160 bytes** (PC de 64 bits, la
     mayoría)
   - VenMirror-win-x86-v1.0.2.zip — 28.256.519 bytes (PC de 32 bits)
2. Descomprímelo en `D:\VenMirror\`. Es portable, no tiene instalador.
   Dentro quedan `VenMirror.exe`, `adb.exe` y la carpeta `keymap` con el
   mapa de Monster Hunter Outlanders incluido.

## 2. Conectar el teléfono (USB)

1. En el teléfono, activa **Opciones de desarrollador** y dentro
   **Depuración USB**.
2. Conecta el teléfono a la PC por cable USB.
3. En el teléfono, acepta **«Permitir depuración USB»** cuando lo pida.
4. Abre `VenMirror.exe`. El teléfono aparece en la lista
   («Double click to connect:»). Haz **doble clic** sobre él, o pulsa
   **USB Connect**.
5. Se abre la ventana del espejo con la pantalla del teléfono.

### Por WiFi (opcional)

El teléfono y la PC tienen que estar en la misma red WiFi.

1. Con el teléfono conectado por USB, pulsa **Start adbd**.
2. Anota la IP que aparece en el campo de IP.
3. Pulsa **wireless connect**. A partir de ahí puedes desconectar el cable.
4. Para volver a cable, pulsa **wireless disconnect**.

## 3. Calidad de imagen (que se vea como en el teléfono)

El espejo es un video en vivo que el teléfono comprime y envía a la PC, no
la pantalla cruda. Con los valores por defecto (2 Mbps, 1080p) se ve
borroso en juegos con movimiento. Para que quede casi igual que en el
teléfono, antes de conectar ajusta en **Start Config**:

1. **bit rate: 20 Mbps.** Escribe `20` y deja `Mbps`.
2. **max size: original.** Así no reduce la resolución del teléfono.
3. **Codec Mode: MTK Encoder** y en **Encoder Settings...** elige el nivel
   **Game Mode (20 Mbps, QP≤28)**. Está hecho para teléfonos MediaTek
   como el Infinix NOTE 50 Pro y evita que la imagen se emborrone al
   moverse.
4. Conecta por **USB**, no por WiFi: por cable hay mucho más ancho de banda.

> No uses H.265 si tu PC tiene una GT 730: esa tarjeta no lo decodifica por
> hardware y el procesador se carga. Con H.264 a 20 Mbps vas sobrado.

## 4. Elegir el juego y aplicar el mapa

En la ventana principal, en la sección del juego:

1. En el desplegable, elige el perfil **Monster Hunter Outlanders**.
2. Pulsa **refresh script** y luego **apply**.
3. Listo: las teclas ya funcionan en el juego.

### Mapa de Monster Hunter Outlanders (incluido)

| Tecla / ratón | Acción |
|---|---|
| W A S D | Joystick (moverse) |
| Clic izquierdo | Atacar |
| Espacio | Evasión |
| Q | Poción |
| T | Habilidad roja |
| E | Explosión |
| V | Explosión 2 |
| C | Mirilla |
| X | Red |
| G | Recoger |
| Z | Frío / calor |
| H | Disparo rápido II |
| 1, 2, 3, 4 | Retratos de la derecha |
| **F** | **Cambiar de modo (ver abajo)** |

## 5. Jugar con teclado y ratón

- **F cambia de modo.** En modo mapa, las teclas juegan y el ratón mueve
  la cámara. Pulsa **F** para pasar a modo normal: ahí el ratón hace
  toques normales en la pantalla y el teclado va al teléfono. Pulsa **F**
  otra vez para volver al modo mapa.
- **Mover el ratón = mover la cámara** (con el mapa activo y el cursor
  escondido).
- **Clic izquierdo = toque** en la pantalla (en MHO es Atacar).
- **Rueda del ratón = desplazar** (scroll).
- **El clic derecho sostenido NO mueve la cámara.** Es una limitación del
  motor, no un fallo de configuración.

## 6. Editar el mapa con el editor visual

1. Con el teléfono conectado y el espejo abierto, pulsa **Editar mapa**.
2. Sobre el video aparece la paleta:
   **Toque**, **Joystick**, **Mirilla**, **Arrastre**, **Cambio**,
   **Cámara**, **Guardar**, **Salir**.
3. Para añadir un control: pulsa su tipo en la paleta (**Toque** para un
   botón, **Joystick** para moverse, **Mirilla** para apuntar,
   **Arrastre** para deslizar) y luego haz clic sobre el video donde
   tiene que quedar.
4. Para mover un control: **arrástralo**.
5. Para asignar una tecla: haz **clic** sobre el control y **pulsa la
   tecla** (o el botón del ratón) que quieres.
6. Para borrarlo: selecciónalo y pulsa **Supr**. Para duplicarlo:
   **Ctrl+D**.
7. **Cambio** cambia la tecla que cambia de modo (por defecto **F**).
8. **Cámara** activa o desactiva la cámara por ratón. Al activarla,
   arrastra el punto para ponerlo donde empieza la vista.
9. Pulsa **Guardar** (o **Ctrl+S**) para guardar el mapa y aplicarlo en
   vivo, sin reiniciar el espejo. **Esc** o **Salir** cierra el editor.

En la parte de arriba del video siempre ves la ayuda:
«Cambio: F · Clic: seleccionar · Arrastrar: mover · Tecla: asignar ·
Supr: borrar · Ctrl+D: duplicar · Ctrl+S: guardar · Esc: salir».

## 7. Perfiles por juego

- **Perfil:** elige el perfil del juego que vas a jugar.
- **Guardar perfil:** guarda el mapa actual como un perfil nuevo. Te pide
  el nombre y, opcionalmente, el paquete del juego.
- **Importar:** carga un mapa de teclas desde un archivo `.json`.
- Cada juego tiene su propio mapa; al cambiar de perfil, pulsa
  **refresh script** y **apply**.

## 8. Límites conocidos

- El espejo nunca es idéntico píxel a píxel a la pantalla del teléfono:
  es video comprimido en tiempo real. A 20 Mbps y resolución original
  queda muy cerca.
- El clic derecho sostenido no mueve la cámara.
- El mensaje del motor que dice «Press ~ key to switch keymap mode»
  siempre dice `~`, pero la tecla real es la que muestra el editor
  (por defecto **F**).
- El mapa se guarda en la carpeta de usuario; si editas el JSON a mano,
  actualiza también esa copia, porque es la que lee el programa.
