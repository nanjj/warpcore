// Copyright (c) 2014-2017, NetApp, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <arpa/inet.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <warpcore/warpcore.h>

#include "common.h"


static struct w_engine *w;
static struct w_sock *ss, *cs;


bool io(const uint32_t len)
{
    // allocate a w_iov chain for tx
    struct w_iov_sq o = w_iov_sq_initializer(o);
    w_alloc_cnt(w, &o, len, 512, 64);
    if (w_iov_sq_cnt(&o) != len)
        return false;

    // fill it with data
    struct w_iov * ov;
    uint8_t fill = 0;
    sq_foreach (ov, &o, next) {
        memset(ov->buf, fill++, ov->len);
    }
    const uint32_t olen = w_iov_sq_len(&o);

    // tx
    w_tx(cs, &o);
    while (w_tx_pending(&o))
        w_nic_tx(w);

    // read the chain back
    struct w_iov_sq i = w_iov_sq_initializer(i);
    uint32_t ilen = 0;
    do {
        w_nic_rx(w, 1000);
        w_rx(ss, &i);
        const uint32_t new_ilen = w_iov_sq_len(&i);
        if (ilen == new_ilen) {
            // we ran out of buffers or there was packet loss; abort
            w_free(w, &o);
            w_free(w, &i);
            return false;
        }
        ilen = new_ilen;
    } while (ilen < olen);
    ensure(ilen == olen, "wrong length");

    // validate data
    struct w_iov * iv = sq_first(&i);
    ov = sq_first(&o);
    while (ov && iv) {
        ensure(memcmp(iv->buf, ov->buf, iv->len) == 0,
               "ov %u = 0x%02x (len %u) != iv %u = 0x%02x (len %u)", ov->idx,
               ov->buf[0], ov->len, iv->idx, iv->buf[0], iv->len);
        ov = sq_next(ov, next);
        iv = sq_next(iv, next);
    }

    w_free(w, &o);
    w_free(w, &i);
    return true;
}


void init(void)
{
    char i[IFNAMSIZ] = "lo"
#ifndef __linux__
                       "0"
#endif
        ;

    w = w_init(i, 0, 8000);

    // bind server socket
    ss = w_bind(w, htons(55555), 0);
    ensure(w_engine(ss) == w, "same engine");

    // connect to server
    cs = w_bind(w, 0, 0);
    w_connect(cs, inet_addr("127.0.0.1"), htons(55555));
    ensure(w_connected(cs), "not connected");
}


void cleanup(void)
{
    // close down
    w_close(cs);
    w_close(ss);
    w_cleanup(w);
}
