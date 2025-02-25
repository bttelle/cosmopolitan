/*-*- mode:c;indent-tabs-mode:t;c-basic-offset:8;tab-width:8;coding:utf-8   -*-│
│ vi: set noet ft=c ts=8 tw=8 fenc=utf-8                                   :vi │
╞══════════════════════════════════════════════════════════════════════════════╡
│ $OpenBSD: ecvt.c,v 1.11 2019/01/25 00:19:25 millert Exp $                    │
│                                                                              │
│ Copyright (c) 2002, 2006, 2010 Todd C. Miller <millert@openbsd.org>          │
│                                                                              │
│ Permission to use, copy, modify, and distribute this software for any        │
│ purpose with or without fee is hereby granted, provided that the above       │
│ copyright notice and this permission notice appear in all copies.            │
│                                                                              │
│ THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES     │
│ WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF             │
│ MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR      │
│ ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES       │
│ WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN        │
│ ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF      │
│ OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.               │
│                                                                              │
│ Sponsored in part by the Defense Advanced Research Projects                  │
│ Agency (DARPA) and Air Force Research Laboratory, Air Force                  │
│ Materiel Command, USAF, under agreement number F39502-99-1-0512.             │
│ SUCH DAMAGE.                                                                 │
╚─────────────────────────────────────────────────────────────────────────────*/
#include "libc/mem/mem.h"
#include "libc/stdio/stdio.h"
#include "libc/str/str.h"
#include "libc/str/unicode.h"
#include "third_party/gdtoa/gdtoa.h"

asm(".ident\t\"\\n\\n\
OpenBSD ecvt/gcvt (MIT)\\n\
Copyright (c) 2002, 2006, 2010 Todd C. Miller <millert@openbsd.org>\"");
asm(".include \"libc/disclaimer.inc\"");
// clang-format off

#define DEFPREC	6

char *
gcvt(double value, int ndigit, char *buf)
{
	char *digits, *dst, *src;
	int i, decpt, sign;
	struct lconv *lconv;

	lconv = localeconv();
	if (ndigit <= 0) {
		/* Match printf(3) behavior. */
		ndigit = ndigit ? DEFPREC : 1;
	}

	digits = dtoa(value, 2, ndigit, &decpt, &sign, NULL);
	if (digits == NULL)
		return (NULL);
	if (decpt == 9999) {
		/*
		 * Infinity or NaN, convert to inf or nan with sign.
		 * We can't infer buffer size based on ndigit.
		 * We have to assume it is at least 5 chars.
		 */
		snprintf(buf, 5, "%s%s", sign ? "-" : "",
		    *digits == 'I' ? "inf" : "nan");
		freedtoa(digits);
		return (buf);
	}

	dst = buf;
	if (sign)
		*dst++ = '-';

	/* Match printf(3) behavior for exponential vs. regular fomatting. */
	if (decpt <= -4 || decpt > ndigit) {
		/* exponential format (e.g. 1.2345e+13) */
		if (--decpt < 0) {
			sign = 1;
			decpt = -decpt;
		} else
			sign = 0;
		src = digits;
		*dst++ = *src++;
		if (*src != '\0') {
			*dst++ = *lconv->decimal_point;
			do {
				*dst++ = *src++;
			} while (*src != '\0');
		}
		*dst++ = 'e';
		if (sign)
			*dst++ = '-';
		else
			*dst++ = '+';
		if (decpt < 10) {
			*dst++ = '0';
			*dst++ = '0' + decpt;
			*dst = '\0';
		} else {
			/* XXX - optimize */
			for (sign = decpt, i = 0; (sign /= 10) != 0; i++)
				continue;
			dst[i + 1] = '\0';
			while (decpt != 0) {
				dst[i--] = '0' + decpt % 10;
				decpt /= 10;
			}
		}
	} else {
		/* standard format */
		for (i = 0, src = digits; i < decpt; i++) {
			if (*src != '\0')
				*dst++ = *src++;
			else
				*dst++ = '0';
		}
		if (*src != '\0') {
			if (src == digits)
				*dst++ = '0';	/* zero before decimal point */
			*dst++ = *lconv->decimal_point;
			while (decpt < 0) {
				*dst++ = '0';
				decpt++;
			}
			for (i = decpt; digits[i] != '\0'; i++) {
				*dst++ = digits[i];
			}
		}
		*dst = '\0';
	}
	freedtoa(digits);
	return (buf);
}
