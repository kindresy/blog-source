/* SPDX-License-Identifier: Apache-2.0 */
#include <stddef.h>
#include <string.h>
/* libpldm 内联工具引用 GNU memmem。独立提供有界版本，不依赖 OS。 */
void *kit_memmem(const void *haystack, size_t haystack_len,
                 const void *needle, size_t needle_len)
{
    const unsigned char *h = haystack;
    if (!needle_len) return (void *)haystack;
    if (needle_len > haystack_len) return NULL;
    for (size_t i = 0; i <= haystack_len - needle_len; i++)
        if (!memcmp(h + i, needle, needle_len)) return (void *)(h + i);
    return NULL;
}
