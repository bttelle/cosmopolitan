/*-*- mode:c;indent-tabs-mode:nil;c-basic-offset:2;tab-width:8;coding:utf-8 -*-│
│ vi: set et ft=c ts=8 sts=2 sw=2 fenc=utf-8                               :vi │
╞══════════════════════════════════════════════════════════════════════════════╡
│ Copyright 2020 Justine Alexandra Roberts Tunney                              │
│                                                                              │
│ Permission to use, copy, modify, and/or distribute this software for         │
│ any purpose with or without fee is hereby granted, provided that the         │
│ above copyright notice and this permission notice appear in all copies.      │
│                                                                              │
│ THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL                │
│ WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED                │
│ WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE             │
│ AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL         │
│ DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR        │
│ PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER               │
│ TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR             │
│ PERFORMANCE OF THIS SOFTWARE.                                                │
╚─────────────────────────────────────────────────────────────────────────────*/
#include "libc/assert.h"
#include "libc/calls/calls.h"
#include "libc/errno.h"
#include "libc/intrin/weaken.h"
#include "libc/mem/mem.h"
#include "libc/runtime/runtime.h"
#include "libc/stdio/internal.h"
#include "libc/stdio/stdio.h"

/**
 * Closes standard i/o stream and its underlying thing.
 *
 * @param f is the file object
 * @return 0 on success or -1 on error, which can be a trick for
 *     differentiating between EOF and real errors during previous
 *     i/o calls, without needing to call ferror()
 */
int fclose(FILE *f) {
  int rc;
  if (!f) return 0;
  __fflush_unregister(f);
  fflush(f);
  if (_weaken(free)) {
    _weaken(free)(f->getln);
    if (!f->nofree && f->buf != f->mem) {
      _weaken(free)(f->buf);
    }
  }
  f->state = EOF;
  if (f->noclose) {
    f->fd = -1;
  } else if (f->fd != -1 && close(f->fd) == -1) {
    f->state = errno;
  }
  if (f->state == EOF) {
    rc = 0;
  } else {
    errno = f->state;
    rc = EOF;
  }
  __stdio_free(f);
  return rc;
}
