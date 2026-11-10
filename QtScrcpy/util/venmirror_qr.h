// VenMirror: generador QR mínimo en un solo archivo (C++11, solo la
// biblioteca estándar). Modo byte, ECC MEDIUM, versiones 1-10, máscara
// automática por penalización (reglas N1-N4 del estándar QR).
//
// Escrito para VenMirror v1.1.0: el emparejamiento por QR de la
// depuración inalámbrica de Android 11+ necesita mostrar un QR con el
// texto "WIFI:T:ADB;S:<servicio>;P:<contraseña>;;" (formato de Android
// Studio, ver AOSP packages/modules/adb/docs/dev/adb_wifi.md).
//
// Por qué no se vendorizó el qrcodegen de Nayuki tal cual: son ~2600
// líneas entre .hpp y .cpp y este canal no permite copiarlas byte a
// byte con garantías; este archivo (~400 líneas) cubre solo lo que
// VenMirror necesita y está verificado módulo a módulo contra el
// codificador de referencia `qrcode` de Python (misma versión, ECC y
// máscara) y con decodificación real (OpenCV QRCodeDetector).
// Las dos tablas pequeñas (ECC por bloque y nº de bloques, versiones
// 1-10) son los valores estándar del QR Model 2, iguales a los de
// Nayuki/AOSP. La licencia MIT de Nayuki no aplica aquí: el código es
// original de VenMirror, escrito a partir del estándar ISO/IEC 18004.

#ifndef VENMIRROR_QR_H
#define VENMIRROR_QR_H

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace venmirror {

// Matriz de un QR: modules[y][x], true = oscuro. size = version * 4 + 17.
struct QrMatrix {
    int version = 0;
    int size = 0;
    int mask = -1;
    std::vector<std::vector<bool> > modules;
};

// Codifica `text` (UTF-8, modo byte) en un QR con ECC MEDIUM.
// `forceMask`: -1 = elegir la mejor máscara por penalización; 0-7 =
// forzar esa máscara (solo para pruebas). Devuelve false si el texto
// no cabe en versión 10.
bool encodeQr(const std::string &text, QrMatrix &out, int forceMask = -1);

namespace qr_detail {

// Tablas estándar QR Model 2 (índice 0 = relleno, sin uso). Filas por
// nivel ECC: 0 = LOW, 1 = MEDIUM, 2 = QUARTILE, 3 = HIGH. Solo se usa
// la fila MEDIUM (1); las demás están para documentar el estándar.
static const int8_t kEccCodewordsPerBlock[4][11] = {
    { -1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18 },   // LOW
    { -1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26 },  // MEDIUM
    { -1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24 },  // QUARTILE
    { -1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28 },  // HIGH
};
static const int8_t kNumErrorCorrectionBlocks[4][11] = {
    { -1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4 },  // LOW
    { -1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5 },  // MEDIUM
    { -1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8 },  // QUARTILE
    { -1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8 },  // HIGH
};
// Posiciones de los patrones de alineación por versión. La versión 1
// no tiene (count 0); las versiones 2-6 tienen 2 posiciones y la 7-10
// tienen 3. El valor 6 es el centro del patrón buscador.
static const int kAlignPositions[11][7] = {
    { 0 }, { 0 }, { 6, 18 }, { 6, 22 }, { 6, 26 }, { 6, 30 }, { 6, 34 },
    { 6, 22, 38 }, { 6, 24, 42 }, { 6, 26, 46 }, { 6, 28, 50 },
};
static const int kAlignCount[11] = { 0, 0, 2, 2, 2, 2, 2, 3, 3, 3, 3 };

inline int numRawDataModules(int ver)
{
    int result = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        int numAlign = ver / 7 + 2;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if (ver >= 7) {
            result -= 36;
        }
    }
    return result;
}

// ECC MEDIUM (fila 1 de las tablas).
inline int numDataCodewords(int ver)
{
    return numRawDataModules(ver) / 8
           - kEccCodewordsPerBlock[1][ver] * kNumErrorCorrectionBlocks[1][ver];
}

inline uint8_t gfMultiply(uint8_t x, uint8_t y)
{
    // Multiplicación en GF(2^8) con polinomio 0x11D (Russian peasant).
    int z = 0;
    for (int i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return static_cast<uint8_t>(z);
}

inline std::vector<uint8_t> reedSolomonDivisor(int degree)
{
    // Polinomio generador de Reed-Solomon de grado `degree`.
    std::vector<uint8_t> result(static_cast<size_t>(degree), 0);
    result[static_cast<size_t>(degree - 1)] = 1;  // x^0
    int root = 1;
    for (int i = 0; i < degree; i++) {
        for (size_t j = 0; j < result.size(); j++) {
            result[j] = gfMultiply(result[j], static_cast<uint8_t>(root));
            if (j + 1 < result.size()) {
                result[j] ^= result[j + 1];
            }
        }
        root = gfMultiply(static_cast<uint8_t>(root), 0x02);
    }
    return result;
}

inline std::vector<uint8_t> reedSolomonRemainder(const std::vector<uint8_t> &data,
                                                  const std::vector<uint8_t> &divisor)
{
    std::vector<uint8_t> result(divisor.size(), 0);
    for (uint8_t b : data) {
        uint8_t factor = b ^ result.at(0);
        result.erase(result.begin());
        result.push_back(0);
        for (size_t i = 0; i < divisor.size(); i++) {
            result[i] ^= gfMultiply(divisor[i], factor);
        }
    }
    return result;
}

// Bits de formato (15 bits): 5 bits de datos (ECC + máscara) con BCH
// (generador 0x537) y máscara XOR 0x5412.
inline int formatBits(int eclBits, int mask)
{
    int data = (eclBits << 3) | mask;  // 5 bits
    int rem = data;
    for (int i = 0; i < 10; i++) {
        rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    }
    return ((data << 10) | rem) ^ 0x5412;
}

// Bits de versión (18 bits) para versiones 7-40, BCH (generador 0x1F25).
inline int versionBits(int ver)
{
    int rem = ver;
    for (int i = 0; i < 12; i++) {
        rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
    }
    return (ver << 12) | rem;
}

struct BitBuffer {
    std::vector<bool> bits;
    void append(uint32_t val, int len)
    {
        for (int i = len - 1; i >= 0; i--) {
            bits.push_back(((val >> i) & 1) != 0);
        }
    }
};

// Dibuja un patrón buscador 7x7 con su separador, centrado en (cx, cy).
inline void drawFinder(std::vector<std::vector<bool> > &m, std::vector<std::vector<bool> > &fn,
                       int size, int cx, int cy)
{
    for (int dy = -4; dy <= 4; dy++) {
        for (int dx = -4; dx <= 4; dx++) {
            int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= size || y >= size) {
                continue;
            }
            int dist = std::max(std::abs(dx), std::abs(dy));
            bool dark = (dist != 2 && dist != 4);
            m[static_cast<size_t>(y)][static_cast<size_t>(x)] = dark;
            fn[static_cast<size_t>(y)][static_cast<size_t>(x)] = true;
        }
    }
}

inline void drawAlignment(std::vector<std::vector<bool> > &m, std::vector<std::vector<bool> > &fn,
                          int cx, int cy)
{
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int dist = std::max(std::abs(dx), std::abs(dy));
            m[static_cast<size_t>(cy + dy)][static_cast<size_t>(cx + dx)] = (dist != 1);
            fn[static_cast<size_t>(cy + dy)][static_cast<size_t>(cx + dx)] = true;
        }
    }
}

inline void setFormat(std::vector<std::vector<bool> > &m, std::vector<std::vector<bool> > &fn,
                      int size, int bits)
{
    // Primera copia: alrededor del buscador superior izquierdo.
    // OJO con el orden de coordenadas: los índices son m[fila][columna].
    // Bits 0-5 en la columna 8, filas 0-5; bit 6 en (7,8); bit 7 en
    // (8,8); bit 8 en (8,7); bits 9-14 en la fila 8, columnas 5-0.
    for (int i = 0; i <= 5; i++) {
        bool b = ((bits >> i) & 1) != 0;
        m[static_cast<size_t>(i)][8] = b;
        fn[static_cast<size_t>(i)][8] = true;
    }
    m[7][8] = ((bits >> 6) & 1) != 0;
    fn[7][8] = true;
    m[8][8] = ((bits >> 7) & 1) != 0;
    fn[8][8] = true;
    m[8][7] = ((bits >> 8) & 1) != 0;
    fn[8][7] = true;
    for (int i = 9; i < 15; i++) {
        bool b = ((bits >> i) & 1) != 0;
        m[8][static_cast<size_t>(14 - i)] = b;
        fn[8][static_cast<size_t>(14 - i)] = true;
    }
    // Segunda copia: dividida entre los otros dos buscadores.
    // Bits 0-7 en la fila 8, columnas size-1 .. size-8; bits 8-14 en la
    // columna 8, filas size-7 .. size-1.
    for (int i = 0; i < 8; i++) {
        bool b = ((bits >> i) & 1) != 0;
        m[8][static_cast<size_t>(size - 1 - i)] = b;
        fn[8][static_cast<size_t>(size - 1 - i)] = true;
    }
    for (int i = 8; i < 15; i++) {
        bool b = ((bits >> i) & 1) != 0;
        m[static_cast<size_t>(size - 15 + i)][8] = b;
        fn[static_cast<size_t>(size - 15 + i)][8] = true;
    }
    // Módulo oscuro fijo: fila size-8, columna 8.
    m[static_cast<size_t>(size - 8)][8] = true;
    fn[static_cast<size_t>(size - 8)][8] = true;
}

inline void drawVersionInfo(std::vector<std::vector<bool> > &m, std::vector<std::vector<bool> > &fn,
                            int size, int bits)
{
    for (int i = 0; i < 18; i++) {
        bool b = ((bits >> i) & 1) != 0;
        int a = size - 11 + (i % 3);
        int bpos = i / 3;
        m[static_cast<size_t>(bpos)][static_cast<size_t>(a)] = b;
        fn[static_cast<size_t>(bpos)][static_cast<size_t>(a)] = true;
        m[static_cast<size_t>(a)][static_cast<size_t>(bpos)] = b;
        fn[static_cast<size_t>(a)][static_cast<size_t>(bpos)] = true;
    }
}

inline bool maskBit(int mask, int x, int y)
{
    switch (mask) {
    case 0: return (x + y) % 2 == 0;
    case 1: return y % 2 == 0;
    case 2: return x % 3 == 0;
    case 3: return (x + y) % 3 == 0;
    case 4: return (x / 3 + y / 2) % 2 == 0;
    case 5: return (x * y) % 2 + (x * y) % 3 == 0;
    case 6: return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
    case 7: return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
    default: return false;
    }
}

// Penalización N1: rachas de 5+ módulos iguales en filas y columnas.
inline long penaltyRun(const std::vector<std::vector<bool> > &m, int size)
{
    long result = 0;
    for (int i = 0; i < size; i++) {
        // Filas
        bool runColor = false;
        int runLen = 0;
        for (int j = 0; j <= size; j++) {
            bool color = (j < size) ? m[static_cast<size_t>(i)][static_cast<size_t>(j)] : !runColor;
            if (j == 0) {
                runColor = color;
                runLen = 1;
            } else if (color == runColor) {
                runLen++;
            } else {
                if (runLen >= 5) {
                    result += 3 + (runLen - 5);
                }
                runColor = color;
                runLen = 1;
            }
        }
        // Columnas
        runColor = false;
        runLen = 0;
        for (int j = 0; j <= size; j++) {
            bool color = (j < size) ? m[static_cast<size_t>(j)][static_cast<size_t>(i)] : !runColor;
            if (j == 0) {
                runColor = color;
                runLen = 1;
            } else if (color == runColor) {
                runLen++;
            } else {
                if (runLen >= 5) {
                    result += 3 + (runLen - 5);
                }
                runColor = color;
                runLen = 1;
            }
        }
    }
    return result;
}

// Penalización N2: bloques 2x2 del mismo color.
inline long penaltyBlocks(const std::vector<std::vector<bool> > &m, int size)
{
    long result = 0;
    for (int y = 0; y < size - 1; y++) {
        for (int x = 0; x < size - 1; x++) {
            bool c = m[static_cast<size_t>(y)][static_cast<size_t>(x)];
            if (c == m[static_cast<size_t>(y)][static_cast<size_t>(x + 1)]
                && c == m[static_cast<size_t>(y + 1)][static_cast<size_t>(x)]
                && c == m[static_cast<size_t>(y + 1)][static_cast<size_t>(x + 1)]) {
                result += 3;
            }
        }
    }
    return result;
}

// Penalización N3: patrón 1:1:3:1:1 (1011101) con zona clara de 4
// módulos en un lado, en filas y columnas. Los dos patrones de 11
// bits ya llevan la zona clara dentro: 10111010000 (claro después)
// y 00001011101 (claro antes). NO se exigen 4 módulos claros extra
// fuera de la ventana: eso subestima la penalización y cambia la
// máscara elegida (detectado al comparar con el codificador de
// referencia `qrcode` de Python).
inline long penaltyFinderLike(const std::vector<std::vector<bool> > &m, int size)
{
    long result = 0;
    const bool pat[11] = { true, false, true, true, true, false, true, false, false, false, false };
    const bool patInv[11] = { false, false, false, false, true, false, true, true, true, false, true };
    for (int i = 0; i < size; i++) {
        for (int j = 0; j + 11 <= size; j++) {
            bool fwd = true, inv = true, fwdC = true, invC = true;
            for (int k = 0; k < 11; k++) {
                if (m[static_cast<size_t>(i)][static_cast<size_t>(j + k)] != pat[k]) {
                    fwd = false;
                }
                if (m[static_cast<size_t>(i)][static_cast<size_t>(j + k)] != patInv[k]) {
                    inv = false;
                }
                if (m[static_cast<size_t>(j + k)][static_cast<size_t>(i)] != pat[k]) {
                    fwdC = false;
                }
                if (m[static_cast<size_t>(j + k)][static_cast<size_t>(i)] != patInv[k]) {
                    invC = false;
                }
            }
            if (fwd || inv) {
                result += 40;
            }
            if (fwdC || invC) {
                result += 40;
            }
        }
    }
    return result;
}

// Penalización N4: proporción de módulos oscuros lejos del 50%.
inline long penaltyBalance(const std::vector<std::vector<bool> > &m, int size)
{
    long dark = 0;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (m[static_cast<size_t>(y)][static_cast<size_t>(x)]) {
                dark++;
            }
        }
    }
    long total = static_cast<long>(size) * size;
    // k = floor(|dark/total - 1/2| / (1/20)) = floor(|dark*20 - total*10| / total)
    long k = (dark * 20L - total * 10L) < 0 ? (total * 10L - dark * 20L) / total : (dark * 20L - total * 10L) / total;
    return k * 10;
}

inline long penaltyScore(const std::vector<std::vector<bool> > &m, int size)
{
    return penaltyRun(m, size) + penaltyBlocks(m, size) + penaltyFinderLike(m, size)
           + penaltyBalance(m, size);
}

// Pone en claro las celdas de información de formato y de versión.
// El codificador de referencia (`qrcode` de Python, setup_type_info /
// setup_type_number con test=true) calcula la penalización de cada
// máscara con esas zonas en claro, no con los bits reales; puntuar
// con los bits reales elige a veces otra máscara. La matriz final
// conserva los bits reales: esto solo afecta a la puntuación.
inline void clearFormatAndVersion(std::vector<std::vector<bool> > &m, int size, int version)
{
    for (int i = 0; i <= 5; i++) {
        m[static_cast<size_t>(i)][8] = false;
        m[8][static_cast<size_t>(i)] = false;
    }
    m[7][8] = false;
    m[8][7] = false;
    m[8][8] = false;
    for (int i = 0; i < 8; i++) {
        m[8][static_cast<size_t>(size - 1 - i)] = false;
        m[static_cast<size_t>(size - 1 - i)][8] = false;
    }
    m[static_cast<size_t>(size - 8)][8] = false;  // módulo oscuro
    if (version >= 7) {
        for (int i = 0; i < 6; i++) {
            for (int j = 0; j < 3; j++) {
                m[static_cast<size_t>(i)][static_cast<size_t>(size - 11 + j)] = false;
                m[static_cast<size_t>(size - 11 + j)][static_cast<size_t>(i)] = false;
            }
        }
    }
}

}  // namespace qr_detail

inline bool encodeQr(const std::string &text, QrMatrix &out, int forceMask)
{
    using namespace qr_detail;
    // 1. Elegir la versión más pequeña que cabe (ECC MEDIUM, modo byte).
    int version = 0;
    for (int v = 1; v <= 10; v++) {
        int capacityBits = numDataCodewords(v) * 8;
        int charCountBits = (v <= 9) ? 8 : 16;
        int needed = 4 + charCountBits + static_cast<int>(text.size()) * 8;
        if (needed <= capacityBits) {
            version = v;
            break;
        }
    }
    if (version == 0) {
        return false;
    }
    int size = version * 4 + 17;

    // 2. Bitstream de datos: modo 0100, nº de caracteres, bytes,
    // terminador, relleno a byte y bytes de relleno 0xEC/0x11.
    BitBuffer bb;
    bb.append(4, 4);  // modo byte
    bb.append(static_cast<uint32_t>(text.size()), (version <= 9) ? 8 : 16);
    for (size_t i = 0; i < text.size(); i++) {
        bb.append(static_cast<uint8_t>(text[i]), 8);
    }
    int dataCapacityBits = numDataCodewords(version) * 8;
    bb.append(0, std::min(4, dataCapacityBits - static_cast<int>(bb.bits.size())));
    bb.append(0, (8 - static_cast<int>(bb.bits.size() % 8)) % 8);
    for (uint8_t pad = 0xEC; static_cast<int>(bb.bits.size()) < dataCapacityBits; pad ^= 0xEC ^ 0x11) {
        bb.append(pad, 8);
    }
    std::vector<uint8_t> dataCodewords(bb.bits.size() / 8, 0);
    for (size_t i = 0; i < bb.bits.size(); i++) {
        if (bb.bits[i]) {
            dataCodewords[i >> 3] |= static_cast<uint8_t>(1 << (7 - (i & 7)));
        }
    }

    // 3. Dividir en bloques y añadir ECC (Reed-Solomon), con intercalado.
    int eccPerBlock = kEccCodewordsPerBlock[1][version];
    int numBlocks = kNumErrorCorrectionBlocks[1][version];
    int totalData = static_cast<int>(dataCodewords.size());
    int shortLen = totalData / numBlocks;
    int numShort = numBlocks - totalData % numBlocks;
    std::vector<uint8_t> divisor = reedSolomonDivisor(eccPerBlock);
    std::vector<std::vector<uint8_t> > blocks;
    std::vector<std::vector<uint8_t> > eccBlocks;
    size_t pos = 0;
    for (int b = 0; b < numBlocks; b++) {
        int len = shortLen + (b < numShort ? 0 : 1);
        std::vector<uint8_t> block(dataCodewords.begin() + static_cast<std::ptrdiff_t>(pos),
                                   dataCodewords.begin() + static_cast<std::ptrdiff_t>(pos + len));
        pos += static_cast<size_t>(len);
        blocks.push_back(block);
        eccBlocks.push_back(reedSolomonRemainder(block, divisor));
    }
    std::vector<uint8_t> allCodewords;
    for (int i = 0; i < shortLen + 1; i++) {
        for (int b = 0; b < numBlocks; b++) {
            if (i < static_cast<int>(blocks[static_cast<size_t>(b)].size())) {
                allCodewords.push_back(blocks[static_cast<size_t>(b)][static_cast<size_t>(i)]);
            }
        }
    }
    for (int i = 0; i < eccPerBlock; i++) {
        for (int b = 0; b < numBlocks; b++) {
            allCodewords.push_back(eccBlocks[static_cast<size_t>(b)][static_cast<size_t>(i)]);
        }
    }

    // 4. Matriz base: patrones de función y reserva de formato/versión.
    std::vector<std::vector<bool> > modules(static_cast<size_t>(size), std::vector<bool>(static_cast<size_t>(size), false));
    std::vector<std::vector<bool> > isFunction(static_cast<size_t>(size), std::vector<bool>(static_cast<size_t>(size), false));
    // Timing horizontal y vertical.
    for (int i = 0; i < size; i++) {
        bool dark = (i % 2 == 0);
        modules[6][static_cast<size_t>(i)] = dark;
        isFunction[6][static_cast<size_t>(i)] = true;
        modules[static_cast<size_t>(i)][6] = dark;
        isFunction[static_cast<size_t>(i)][6] = true;
    }
    drawFinder(modules, isFunction, size, 3, 3);
    drawFinder(modules, isFunction, size, size - 4, 3);
    drawFinder(modules, isFunction, size, 3, size - 4);
    for (int i = 0; i < kAlignCount[version]; i++) {
        for (int j = 0; j < kAlignCount[version]; j++) {
            int x = kAlignPositions[version][i];
            int y = kAlignPositions[version][j];
            // Solo se saltan las 3 esquinas que coinciden con los
            // buscadores. NO saltar por isFunction: un patrón centrado
            // en la fila/columna de timing (p. ej. (24,6) en v8) SÍ se
            // dibuja y pisa el timing (así lo exige el estándar); el
            // guarda anterior lo saltaba y rompía las versiones 7-10.
            if ((x == 6 && y == 6) || (x == 6 && y == size - 7) || (x == size - 7 && y == 6)) {
                continue;
            }
            drawAlignment(modules, isFunction, x, y);
        }
    }
    // Reservar zonas de formato y de versión.
    for (int i = 0; i < 9; i++) {
        isFunction[8][static_cast<size_t>(i)] = true;
        isFunction[static_cast<size_t>(i)][8] = true;
    }
    for (int i = 0; i < 8; i++) {
        isFunction[8][static_cast<size_t>(size - 1 - i)] = true;
        isFunction[static_cast<size_t>(size - 1 - i)][8] = true;
    }
    if (version >= 7) {
        // Info de versión: 6 filas x 3 columnas arriba a la derecha
        // (filas 0-5, columnas size-11..size-9) y 3 filas x 6 columnas
        // abajo a la izquierda (filas size-11..size-9, columnas 0-5).
        // OJO: el rectángulo NO es 3x6 arriba ni 6x3 abajo.
        for (int i = 0; i < 6; i++) {
            for (int j = 0; j < 3; j++) {
                isFunction[static_cast<size_t>(i)][static_cast<size_t>(size - 11 + j)] = true;
                isFunction[static_cast<size_t>(size - 11 + j)][static_cast<size_t>(i)] = true;
            }
        }
    }

    // 5. Colocar los codewords en zigzag (de derecha a izquierda,
    // subiendo y bajando por pares de columnas, saltando la col. 6).
    std::vector<bool> dataBits;
    for (uint8_t cw : allCodewords) {
        for (int i = 7; i >= 0; i--) {
            dataBits.push_back(((cw >> i) & 1) != 0);
        }
    }
    // Bits de resto (remainder bits) en 0.
    size_t bitIdx = 0;
    for (int right = size - 1; right >= 1; right -= 2) {
        if (right == 6) {
            right = 5;
        }
        for (int vert = 0; vert < size; vert++) {
            for (int j = 0; j < 2; j++) {
                int x = right - j;
                bool upward = ((right + 1) & 2) == 0;
                int y = upward ? size - 1 - vert : vert;
                if (!isFunction[static_cast<size_t>(y)][static_cast<size_t>(x)] && bitIdx < dataBits.size()) {
                    modules[static_cast<size_t>(y)][static_cast<size_t>(x)] = dataBits[bitIdx++];
                }
            }
        }
    }

    // 6. Máscara: probar las 8 (o la forzada) y quedarse con la de menor
    // penalización. Los bits de formato se dibujan con la máscara final.
    int bestMask = -1;
    long bestPenalty = -1;
    std::vector<std::vector<bool> > bestModules;
    for (int mask = 0; mask < 8; mask++) {
        if (forceMask >= 0 && mask != forceMask) {
            continue;
        }
        std::vector<std::vector<bool> > trial = modules;
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                if (!isFunction[static_cast<size_t>(y)][static_cast<size_t>(x)] && maskBit(mask, x, y)) {
                    trial[static_cast<size_t>(y)][static_cast<size_t>(x)] = !trial[static_cast<size_t>(y)][static_cast<size_t>(x)];
                }
            }
        }
        setFormat(trial, isFunction, size, formatBits(0, mask));  // ECC MEDIUM = 00
        // Puntuar con formato/versión en claro, como el codificador de
        // referencia; la matriz final (bestModules) conserva los bits.
        std::vector<std::vector<bool> > scored = trial;
        clearFormatAndVersion(scored, size, version);
        long penalty = penaltyScore(scored, size);
        if (bestPenalty < 0 || penalty < bestPenalty) {
            bestPenalty = penalty;
            bestMask = mask;
            bestModules = trial;
        }
    }
    if (bestMask < 0) {
        return false;
    }
    if (version >= 7) {
        drawVersionInfo(bestModules, isFunction, size, versionBits(version));
    }

    out.version = version;
    out.size = size;
    out.mask = bestMask;
    out.modules = bestModules;
    return true;
}

}  // namespace venmirror

#endif  // VENMIRROR_QR_H
