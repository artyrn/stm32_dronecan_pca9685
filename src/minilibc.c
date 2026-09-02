#include <stddef.h>
#include <stdint.h>

void *memset(void *dst, int value, size_t size)
{
    uint8_t *p = (uint8_t *)dst;

    while (size--) {
        *p++ = (uint8_t)value;
    }

    return dst;
}

void *memcpy(void *dst, const void *src, size_t size)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    while (size--) {
        *d++ = *s++;
    }

    return dst;
}
