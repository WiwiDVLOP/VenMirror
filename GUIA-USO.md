# VenMirror — Guía de uso

VenMirror espeja la pantalla de tu teléfono Android en la PC y te deja jugar
con teclado y ratón. Es un fork de QtScrcpy con un editor visual de teclas
estilo LDPlayer integrado: arrastras los controles sobre el juego, pulsas
una tecla para asignarla y guardas.

Versión de esta guía: **v1.1.0**.

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

El teléfono y la PC tienen que estar en la misma red WiFi. Hay tres
maneras de conectar; elige la que prefieras. La **A** es la de siempre
(por cable una vez) y la **B** y la **C** no usan cable en ningún momento.

#### A) Por USB (primera vez) — para cualquier Android

1. Con el teléfono conectado por USB, pulsa **Start adbd**.
2. Anota la IP que aparece en el campo de IP.
3. Pulsa **wireless connect**. A partir de ahí puedes desconectar el cable.
4. Para volver a cable, pulsa **wireless disconnect**.

En Android 10 o anterior, este es el único camino: la depuración
inalámbrica no existe en esos teléfonos.

#### B) Sin USB, con código de emparejamiento — Android 11 o superior

1. En el teléfono: **Ajustes > Opciones de desarrollador > Depuración
   inalámbrica > «Emparejar dispositivo con código de
   emparejamiento»**. El teléfono muestra su **IP**, un **puerto de
   emparejamiento** y un **código de 6 dígitos**.
2. En VenMirror, en el grupo **«WiFi sin USB (depuración inalámbrica)»**,
   escribe la **IP**, el **puerto de emparejamiento** y el **código**, y
   pulsa **Emparejar**.
3. El teléfono muestra ahora un **puerto de conexión** (distinto del de
   emparejamiento). Escríbelo en **Puerto de conexión** y pulsa
   **Conectar**. El teléfono aparece en la lista; haz doble clic sobre
   él como siempre.
4. El emparejamiento se guarda: la próxima vez no hace falta el código,
   basta con **Buscar en WiFi** (abajo) o con **Conectar** usando el
   puerto de conexión actual.

> El código de emparejamiento **caduca rápido** (pocos minutos) y el
> puerto de conexión **cambia** cada vez que reinicias el teléfono o
> cambias de WiFi. Si el código ya no vale, genera otro en el teléfono
> y repite el paso 2.

#### C) Sin USB, con código QR — Android 11 o superior

Es lo mismo que la B, pero sin teclear nada:

1. En VenMirror, en el grupo **«WiFi sin USB»**, pulsa **Mostrar QR**.
   Aparece un código QR en la PC.
2. En el teléfono: **Ajustes > Opciones de desarrollador > Depuración
   inalámbrica > «Emparejar dispositivo con código QR»** y escanea el
   código de la PC.
3. VenMirror empareja solo y conecta el teléfono (tarda unos segundos).
   El teléfono aparece en la lista; haz doble clic sobre él.

> El QR **caduca a los 2 minutos**: si se cierra la ventana del QR o
> pasa el tiempo, pulsa **Mostrar QR** otra vez para generar uno nuevo.

#### Buscar teléfonos en la WiFi (sin escribir la IP)

En el grupo **«WiFi sin USB»**, el botón **Buscar en WiFi** encuentra
los teléfonos de tu red que ya están emparejados (o activados por USB
alguna vez) y los conecta solos:

1. Pulsa **Buscar en WiFi**. VenMirror busca primero por mDNS (el
   propio Android anuncia sus teléfonos) y, si no encuentra nada,
   escanea el puerto 5555 de tu red.
2. Los teléfonos encontrados aparecen en el registro («Encontrados N
   teléfono(s) por WiFi: ...») y se conectan; luego aparecen en la
   lista de dispositivos.
3. Si no encuentra ninguno, revisa que el teléfono tenga la depuración
   inalámbrica activada (B o C) o que se haya activado por USB alguna
   vez (A).

> Sin depuración inalámbrica ni USB, un teléfono Android normal **no
> se puede conectar**: no escucha ningún puerto y ningún programa lo
> puede controlar (sería un agujero de seguridad). Por eso la primera
> vez siempre hace falta uno de los tres caminos de arriba.

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
- **Mantén F1 para ver el mapa.** Con el juego en marcha (fuera del
  editor), mantén pulsada **F1** y aparece sobre el video el mapa
  activo: la lista de teclas con lo que hace cada una («W A S D —
  Mover - joystick virtual», «Espacio — Evasion»...) y la etiqueta de
  cada tecla sobre su control. Suelta **F1** y desaparece. No toca el
  juego ni bloquea ninguna tecla: solo dibuja encima. **F1 no se puede
  asignar a ningún control**, está reservada para esto.
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
   tecla** (o el botón del ratón) que quieres. Vale casi cualquier
   tecla: letras, dígitos, **F2–F12**, Tab, Intro, Espacio, flechas,
   Inicio/Fin/RePág/AvPág, Insertar/Suprimir, puntuación y símbolos con
   Mayús (por ejemplo `?` o `+`). **No** valen: **F1** (reservada para
   ver el mapa), la tecla de cambio de modo (**F** por defecto) ni una
   tecla que ya use otro control; el editor te avisa en cada caso. No
   hay límite de controles: puedes añadir todos los toques que quieras.
6. Para borrarlo: selecciónalo y pulsa **Supr**. Para duplicarlo:
   **Ctrl+D**.
7. **Cambio** cambia la tecla que cambia de modo (por defecto **F**).
8. **Cámara** activa o desactiva la cámara por ratón. Al activarla,
   arrastra el punto para ponerlo donde empieza la vista.
9. Para cambiar las teclas del joystick: haz **doble clic sobre el
   joystick**. Se abre un diálogo con **Arriba / Abajo / Izquierda /
   Derecha**: pulsa cada campo y luego la tecla que quieres (por
   ejemplo, las flechas en vez de WASD). Aceptar aplica el cambio.
10. Pulsa **Guardar** (o **Ctrl+S**) para guardar el mapa y aplicarlo en
    vivo, sin reiniciar el espejo. Al guardar, VenMirror te **pregunta
    el nombre** del mapa (viene rellenado con el nombre actual): si lo
    dejas igual, sobrescribe como antes; si pones otro nombre, guarda
    una copia nueva con ese nombre y la selecciona en la lista de
    mapas. **Ctrl+Mayús+S** es «Guardar como» (siempre pregunta el
    nombre). Si el nombre ya existe y es otro archivo, te pregunta
    antes de sobrescribirlo. **Esc** o **Salir** cierra el editor.

En la parte de arriba del video siempre ves la ayuda:
«Cambio: F · Clic: seleccionar · Arrastrar: mover · Tecla: asignar ·
Supr: borrar · Ctrl+D: duplicar · Ctrl+S: guardar · Ctrl+Mayús+S:
guardar como · F1: ver mapa · Esc: salir».

> Si guardas con un nombre nuevo, el mapa queda seleccionado en la
> lista de mapas, pero el perfil del juego sigue apuntando al mapa que
> tenías antes. Para que el perfil use el nuevo, pulsa **Guardar
> perfil** con el mismo nombre del perfil: guarda el mapa actual como
> el mapa de ese perfil.

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
- WiFi sin USB (v1.1.0): solo Android 11 o superior. El código de
  emparejamiento caduca en pocos minutos y el puerto de conexión cambia
  al reiniciar el teléfono o cambiar de WiFi (el emparejamiento no se
  pierde: basta con **Buscar en WiFi**). El QR caduca a los 2 minutos.
- La tecla **F1** está reservada para ver el mapa y no se puede asignar
  a ningún control. **Ctrl+S** guarda, **Ctrl+Mayús+S** guarda con otro
  nombre, **Ctrl+D** duplica y **Esc** sale del editor: esas
  combinaciones tampoco se pueden asignar como teclas de juego.
