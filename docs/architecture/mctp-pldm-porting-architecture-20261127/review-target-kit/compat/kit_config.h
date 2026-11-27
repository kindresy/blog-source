/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <errno.h>
#include <stddef.h>
#include <string.h>
void *kit_memmem(const void *haystack, size_t haystack_len,
                 const void *needle, size_t needle_len);
#define memmem kit_memmem
/* libpldm 的内部 errno 映射需要 EUCLEAN；部分 newlib 没有这个标识。
 * 仅作此库的内部错误编号，不向线上发送 errno 数值。
 */
#ifndef EUCLEAN
#define EUCLEAN 117
#endif
#define LIBPLDM_ABI_STABLE
#define LIBPLDM_ABI_TESTING
#define LIBPLDM_ABI_DEPRECATED
#define LIBPLDM_ABI_DEPRECATED_UNSAFE
