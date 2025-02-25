/*-*- mode:c;indent-tabs-mode:nil;c-basic-offset:2;tab-width:8;coding:utf-8 -*-│
│ vi: set et ft=c ts=2 sts=2 sw=2 fenc=utf-8                               :vi │
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
#include "libc/dns/dns.h"
#include "libc/dns/dnsheader.h"
#include "libc/mem/gc.internal.h"
#include "libc/mem/mem.h"
#include "libc/stdio/rand.h"
#include "libc/stdio/stdio.h"
#include "libc/str/str.h"
#include "libc/testlib/testlib.h"

TEST(SerializeDnsHeader, test) {
  uint8_t buf[12];
  struct DnsHeader header;
  memset(&header, 0, sizeof(header));
  header.id = 255;
  header.bf1 = true;
  header.qdcount = 1;
  SerializeDnsHeader(buf, &header);
  EXPECT_BINEQ(u" λ☺  ☺      ", buf);
}

TEST(SerializeDnsHeader, fuzzSymmetry) {
  uint8_t buf[12];
  struct DnsHeader in, out;
  rngset(&in, sizeof(in), _rand64, -1);
  rngset(&out, sizeof(out), _rand64, -1);
  SerializeDnsHeader(buf, &in);
  DeserializeDnsHeader(&out, buf);
  ASSERT_EQ(0, memcmp(&in, &out, 12), "%#.*s\n\t%#.*s", 12, in, 12, buf);
}
