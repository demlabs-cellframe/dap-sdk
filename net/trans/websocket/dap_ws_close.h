#ifndef DAP_WS_CLOSE_H
#define DAP_WS_CLOSE_H
#include <stddef.h>
#include <stdint.h>
/* Close reason is one complete UTF-8 string, never split between frames. */
static inline int dap_ws_close_valid(const void *data, size_t size)
{
    if (!size) return 1;
    if (!data || size < 2 || size > 125) return 0;
    const uint8_t *p = data;
    unsigned code = ((unsigned)p[0] << 8) | p[1];
    if (!((code >= 1000 && code <= 1014 && code != 1004 && code != 1005 && code != 1006) ||
          (code >= 3000 && code <= 4999))) return 0;
    for (size_t i = 2; i < size;) {
        unsigned c = p[i++], value, count, minimum;
        if (c < 128) continue;
        if (c >= 0xc2 && c <= 0xdf) { value=c&31;count=1;minimum=0x80; }
        else if (c >= 0xe0 && c <= 0xef) { value=c&15;count=2;minimum=0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { value=c&7;count=3;minimum=0x10000; }
        else return 0;
        if (count > size-i) return 0;
        while (count--) {
            unsigned next=p[i++];
            if ((next&0xc0)!=0x80) return 0;
            value=(value<<6)|(next&63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return 0;
    }
    return 1;
}
#endif
