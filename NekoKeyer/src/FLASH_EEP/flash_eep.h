//
// ch32v003 flash eeprom emulation
// 2025/12/20  v0.1  new JA1AOQ
// 2025/12/21  v0.2  API変更: アドレス指定 -> ページ指定 (0..15)
// 
// API ドキュメント
// - コンストラクタ: FLASH_EEP(uint32_t pages = (FLASH_SIZE / FLASH_PAGE_SIZE))
//   - pages: 使用するページ数（64B ページ単位）。指定可能範囲: 1 .. 16
//   - 内部計算: flash_size = pages * 64, flash_base_addr = FLASH_END_ADDR - flash_size + 1
// - メソッド:
//   - int erase(uint32_t page);
//   - int read(uint32_t page, uint8_t *out);
//   - int write(uint32_t page, const uint8_t *in);
//   - page は 0 から (pages-1) の範囲で指定。範囲外の場合は -2 を返す。
// - エラーコード:
//   - -1 : アドレス整列（64B 境界）エラー（主に内部関数で発生）
//   - -2 : 無効なページ／範囲エラー
// - 使用例:
//   FLASH_EEP eep(2); // 2 ページ (128B) を EEPROM として使用
//   uint8_t buf[FLASH_PAGE_SIZE];
//   eep.erase(0);             // ページ 0 を消去
//   eep.write(0, data);       // ページ 0 に書き込み
//   eep.read(0, buf);         // ページ 0 を読み出し

#pragma once
#include <stdint.h>
#include <stdio.h>

/* Hardware-specific headers are included in the implementation file to avoid conflicts with the Arduino core. */

/* Debug print macro: define DEBUG to enable debug prints (e.g. -DDEBUG) */
#ifndef DEBUG_PRINTF
#ifdef DEBUG
#define DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINTF(...) do {} while (0)
#endif
#endif

/* Control inclusion of sample/demo code (flash_test.cpp). Set to 0 to exclude sample code from build to save flash. */
#ifndef SAMPLE_CODE
#define SAMPLE_CODE 0
#endif

#define FLASH_PAGE_SIZE    64u
#define FLASH_BASE_ADDR    0x08003C00u
#define FLASH_SIZE         1024u
#define FLASH_END_ADDR     0x08003FFFu   // CH32V003 16KB

/* Minimal device definitions copied from the device headers so this public
   header does not depend on the large device-specific headers which
   conflict with the Arduino core. Only the fields and constants actually
   used by the FLASH EEPROM code are included here. */

#ifndef FLASH_R_BASE
#define PERIPH_BASE                ((uint32_t)0x40000000)
#define CORE_PERIPH_BASE           ((uint32_t)0xE0000000)
#define AHBPERIPH_BASE             (PERIPH_BASE + 0x20000)

#define FLASH_R_BASE               (AHBPERIPH_BASE + 0x2000) /* Flash registers base address */

/* Flash register layout (partial): keep fields and order to match hardware */
typedef struct
{
    volatile uint32_t ACTLR;            /* Control Register          */
    volatile uint32_t KEYR;             /* FPEC Key Register         */
    volatile uint32_t OBKEYR;           /* OBKEY Register            */
    volatile uint32_t STATR;            /* Status Register           */
    volatile uint32_t CTLR;             /* Configuration Register    */
    volatile uint32_t ADDR;             /* Address Register          */
    volatile uint32_t RESERVED;
    volatile uint32_t OBR;              /* Select Word Register      */
    volatile uint32_t WPR;              /* Write Protection Register */
    volatile uint32_t MODEKEYR;         /* Extended Key Register     */
    volatile uint32_t BOOT_MODEKEYR;    /* Unlock BOOT Key Register  */
} FLASH_TypeDef;

#define FLASH                      ((FLASH_TypeDef *)FLASH_R_BASE)
#endif /* FLASH_R_BASE */

#ifndef FLASH_KEY1
#define FLASH_KEY1                 ((uint32_t)0x45670123)
#define FLASH_KEY2                 ((uint32_t)0xCDEF89AB)
#endif

#ifndef FLASH_STATR_BSY
#define FLASH_STATR_BSY            ((uint32_t)0x00000001) /* Busy */
#endif

#ifndef FLASH_CTLR_STRT
#define FLASH_CTLR_STRT            ((uint32_t)0x00000040) /* Start */
#endif

#ifndef FLASH_CTLR_PAGE_PG
#define FLASH_CTLR_PAGE_PG         ((uint32_t)0x00010000) /* Page Programming 64Byte */
#endif

#ifndef FLASH_CTLR_PAGE_ER
#define FLASH_CTLR_PAGE_ER         ((uint32_t)0x00020000) /* Page Erase 64Byte */
#endif

#ifndef FLASH_CTLR_BUF_LOAD
#define FLASH_CTLR_BUF_LOAD        ((uint32_t)0x00040000) /* Buffer Load */
#endif

#ifndef FLASH_CTLR_BUF_RST
#define FLASH_CTLR_BUF_RST         ((uint32_t)0x00080000) /* Buffer Reset */
#endif

class FLASH_EEP {
public:
    FLASH_EEP();                                    // デフォルトコンストラクタ（軽量）
    int begin(uint32_t pages);                      // 実際の初期化: pages (1..16)
    bool is_initialized() const { return flash_page_count != 0; }

    int erase(uint32_t page);
    int read(uint32_t page, uint8_t *out);
    int write(uint32_t page, const uint8_t *in);
    void dump_state();
private:
    int write_buf(uint32_t addr, const uint8_t *buf);
    int commit(uint32_t addr);
    uint32_t flash_base_addr = 0;   // 初期化前は 0
    uint32_t flash_size = 0;        // 初期化前は 0
    uint32_t flash_page_count = 0;  // 初期化前は 0
    inline int is_64b_aligned(uint32_t a)
    {
        return (a & 0x3F) == 0;
    }

    inline int page_valid(uint32_t p)
    {
        /*
         * Validate that a page number fits within configured page count
         * and within the allowed range 0..15 (max 16 pages)
         */
        if (p > 15u) return 0;
        return (p < flash_page_count);
    }

    /* Hardware-specific flash helpers: implemented in flash_eep.cpp to avoid
       pulling device headers into this public header (which can conflict
       with the Arduino core). */
    void flash_wait_busy(void);
    void flash_clear_flags(void);
    void flash_unlock_all(void);
};
