/**
 * NekoKeyer  -  PCから文字を送ると、ネコがパドルを打つ (UIAPduino + WebHID)
 *
 * ボード : HID ProMicro CH32V003
 * USB    : WebHID Only  (Tools -> USB)
 *
 * サーボ (以前の ch32fun 版と同じ配線)
 *   PD2 (TIM1_CH1) : DIT レバー
 *   PA1 (TIM1_CH2) : DAH レバー
 *
 * 通信 (PC -> 基板, Feature Report, reportId 0, 先頭バイトがコマンド)
 *   0x10 CLEAR  : [0x10, unit_ms, loop]   文字バッファを空にして速度・繰り返しを設定
 *   0x11 DATA   : [0x11, n, c0, c1, ...]  文字を n 個追加 (最大 MAX_TEXT)
 *   0x12 PLAY   : [0x12]                  先頭から打ち始める
 *   0x13 STOP   : [0x13]                  止める
 *   0x14 SPEED  : [0x14, unit_ms]         打っている途中でも速度を変える
 *   (0x01 は他ページが送る接続通知なので無視)
 *
 * 通信 (基板 -> PC, Input Report 8 バイト)
 *   0x61 [pos, total]  pos 番目の文字を打ち始めた
 *   0x62               最後まで打ち終えた
 *   0x63 [total]       PLAY を受け付けた
 *   0x64               STOP で止まった
 *
 * 1 unit = 短点の長さ。WPM = 1200 / unit_ms
 */

#include <WebHID.h>

// ---- サーボの角度 (以前のプログラムと同じ値) --------------------------
#define DIT_REST    15
#define DIT_ACTIVE  0
#define DAH_REST    0
#define DAH_ACTIVE  15

#define MAX_TEXT    200
#define DEFAULT_UNIT_MS 60

// ---- モールス表 -------------------------------------------------------
static const char* const morse_alpha[26] = {
    ".-",   "-...", "-.-.", "-..",  ".",    "..-.", "--.",  "....",
    "..",   ".---", "-.-",  ".-..", "--",   "-.",   "---",  ".--.",
    "--.-", ".-.",  "...",  "-",    "..-",  "...-", ".--",  "-..-",
    "-.--", "--.."
};

static const char* const morse_digit[10] = {
    "-----", ".----", "..---", "...--", "....-",
    ".....", "-....", "--...", "---..", "----."
};

static const struct { char sym; const char* code; } morse_punct[] = {
    { '.', ".-.-.-" }, { ',', "--..--" }, { '?', "..--.." }, { '/', "-..-."  },
    { '-', "-....-" }, { '(', "-.--."  }, { ')', "-.--.-" }, { '+', ".-.-."  },
    { '@', ".--.-." }, { ':', "---..." }, { ';', "-.-.-." }, { '=', "-...-"  },
};

static const char* lookup(char c) {
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c >= 'A' && c <= 'Z') return morse_alpha[c - 'A'];
    if (c >= '0' && c <= '9') return morse_digit[c - '0'];
    for (uint8_t i = 0; i < sizeof(morse_punct) / sizeof(morse_punct[0]); i++) {
        if (morse_punct[i].sym == c) return morse_punct[i].code;
    }
    return 0;
}

// ---- サーボ (TIM1 を直接制御。以前のプログラムと同じ設定) -------------
static inline uint16_t angle_to_us(uint8_t angle) {
    return 1000 + (uint16_t)((uint32_t)angle * 1000 / 180);
}

static void servo_init(void) {
    RCC->APB2PCENR |= (1 << 2) | (1 << 5) | (1 << 11);   // GPIOA, GPIOD, TIM1

    GPIOD->CFGLR &= ~(0xf << (2 * 4));                   // PD2 = AF-PP 10MHz
    GPIOD->CFGLR |=  (0x9 << (2 * 4));
    GPIOA->CFGLR &= ~(0xf << (1 * 4));                   // PA1 = AF-PP 10MHz
    GPIOA->CFGLR |=  (0x9 << (1 * 4));

    TIM1->PSC   = (SystemCoreClock / 1000000) - 1;       // 1MHz
    TIM1->ATRLR = 20000 - 1;                             // 20ms = 50Hz

    TIM1->CH1CVR = angle_to_us(DIT_REST);
    TIM1->CH2CVR = angle_to_us(DAH_REST);

    TIM1->CHCTLR1 = (6 << 4) | (1 << 3) | (6 << 12) | (1 << 11);  // PWM mode1 + preload
    TIM1->CCER  = (1 << 0) | (1 << 4);                   // CC1E, CC2E
    TIM1->BDTR  = (1 << 15);                             // MOE
    TIM1->CTLR1 = (1 << 7) | (1 << 0);                   // ARPE, CEN
}

static inline void ditPress(bool on) { TIM1->CH1CVR = angle_to_us(on ? DIT_ACTIVE : DIT_REST); }
static inline void dahPress(bool on) { TIM1->CH2CVR = angle_to_us(on ? DAH_ACTIVE : DAH_REST); }

// ---- 打鍵ステートマシン (delay を使わないので、打っている間も受信できる) ----
enum { ST_ON, ST_GAP, ST_CHAR };

static char     text[MAX_TEXT];
static uint8_t  textLen  = 0;
static uint8_t  textIdx  = 0;      // 次に読む文字
static uint8_t  unitMs   = DEFAULT_UNIT_MS;
static bool     loopPlay = false;
static bool     playing  = false;

static const char* code = 0;       // いま打っている文字の符号
static uint8_t  codeIdx  = 0;
static bool     curDit   = true;
static uint8_t  st       = ST_CHAR;
static uint32_t tNext    = 0;

static void report(uint8_t type, uint8_t a, uint8_t b) {
    uint8_t r[8] = { type, a, b, 0, 0, 0, 0, 0 };
    WebHID.send(r, 8);
}

static void releaseAll(void) {
    ditPress(false);
    dahPress(false);
}

static void startElement(uint32_t now) {
    curDit = (code[codeIdx++] == '.');
    if (curDit) ditPress(true); else dahPress(true);
    tNext = now + (curDit ? 1 : 3) * (uint32_t)unitMs;
    st = ST_ON;
}

static void advance(uint32_t now) {
    for (;;) {
        if (textIdx >= textLen) {
            if (loopPlay && textLen > 0) {
                textIdx = 0;
                tNext = now + 7 * (uint32_t)unitMs;   // 繰り返す前の間
                st = ST_CHAR;
            } else {
                playing = false;
                releaseAll();
                report(0x62, 0, 0);
            }
            return;
        }
        uint8_t pos = textIdx;
        char c = text[textIdx++];
        report(0x61, pos, textLen);
        if (c == ' ') {                               // 単語間: 7 unit のうち 3 は経過済み
            tNext = now + 4 * (uint32_t)unitMs;
            st = ST_CHAR;
            return;
        }
        code = lookup(c);
        if (!code) continue;                          // 対応外の文字は読み飛ばす
        codeIdx = 0;
        startElement(now);
        return;
    }
}

static void keyerTick(void) {
    if (!playing) return;
    uint32_t now = millis();
    if ((int32_t)(now - tNext) < 0) return;

    switch (st) {
    case ST_ON:                                       // 打ち終わり -> 1 unit の間
        releaseAll();
        tNext = now + unitMs;
        st = ST_GAP;
        break;
    case ST_GAP:
        if (code && code[codeIdx]) {
            startElement(now);
        } else {                                      // 文字間: 3 unit のうち 1 は経過済み
            code = 0;
            tNext = now + 2 * (uint32_t)unitMs;
            st = ST_CHAR;
        }
        break;
    case ST_CHAR:
        advance(now);
        break;
    }
}

// ---- 受信 -------------------------------------------------------------
static void handleCommand(const uint8_t* b, uint8_t len) {
    if (len < 1) return;
    switch (b[0]) {
    case 0x10:                                        // CLEAR
        playing = false;
        releaseAll();
        textLen = 0;
        textIdx = 0;
        if (len > 1 && b[1] >= 10) unitMs = b[1];
        loopPlay = (len > 2) && b[2];
        break;
    case 0x11: {                                      // DATA
        if (len < 2) break;
        uint8_t n = b[1];
        if (n > len - 2) n = len - 2;
        for (uint8_t i = 0; i < n && textLen < MAX_TEXT; i++) text[textLen++] = (char)b[2 + i];
        break;
    }
    case 0x12:                                        // PLAY
        releaseAll();
        textIdx = 0;
        code = 0;
        st = ST_CHAR;
        tNext = millis() + 3 * (uint32_t)unitMs;
        playing = (textLen > 0);
        report(0x63, textLen, 0);
        break;
    case 0x13:                                        // STOP
        playing = false;
        releaseAll();
        report(0x64, 0, 0);
        break;
    case 0x14:                                        // SPEED
        if (len > 1 && b[1] >= 10) unitMs = b[1];
        break;
    default:                                          // 0x01 接続通知など
        break;
    }
}

void setup() {
    WebHID.begin();
    servo_init();
    delay(2000);                                      // USB 接続待ち
}

void loop() {
    if (WebHID.available()) {
        uint8_t buf[32];                              // Feature Report サイズ (32 バイト)
        uint8_t len = WebHID.recv(buf, sizeof(buf));
        handleCommand(buf, len);
    }
    keyerTick();
}
