// PC に認識されたか (USB アドレスが割り当てられたか) を調べる。
// rv003usb.h は C 用のヘッダーなので、スケッチ (C++) からはこの関数を通して使う。
#include <ch32fun.h>
#include "rv003usb.h"

int neko_usb_configured(void)
{
    return ((volatile struct rv003usb_internal *)&rv003usb_internal_data)->my_address != 0;
}
