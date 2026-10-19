/* fzlzx.c - decompress Xbox 360 XMemCompress (LZX) streams as used by
 * Forza "method 21" zip entries. Built as a shared library and driven
 * from forza_zip.py via ctypes. Uses libmspack's lzxd (rexglue-sdk/thirdparty).
 *
 * Stream layout: repeated chunks of
 *   [0xFF][u16be uncompressed][u16be compressed][data]   or
 *   [u16be compressed][data]                (uncompressed = 0x8000)
 * terminated by a zero size / end of buffer. Chunk payloads are concatenated
 * and decoded as one LZX stream (reset interval 0, frame 0x8000).
 */
#include <stdlib.h>
#include <string.h>
#include "mspack.h"
#include "lzx.h"

typedef struct { char *buf; long size, off; } memf;

static struct mspack_file *m_open(struct mspack_system *s, const char *f, int m) { return NULL; }
static void m_close(struct mspack_file *f) {}
static int m_read(struct mspack_file *f, void *b, int n) {
  memf *m = (memf *)f; long r = m->size - m->off; if (n < r) r = n; if (r < 0) r = 0;
  memcpy(b, m->buf + m->off, r); m->off += r; return (int)r; }
static int m_write(struct mspack_file *f, void *b, int n) {
  memf *m = (memf *)f; long r = m->size - m->off; if (n < r) r = n; if (r < 0) r = 0;
  memcpy(m->buf + m->off, b, r); m->off += r; return n; }
static int m_seek(struct mspack_file *f, off_t o, int mode) { return -1; }
static off_t m_tell(struct mspack_file *f) { return ((memf *)f)->off; }
static void m_msg(struct mspack_file *f, const char *fmt, ...) {}
static void *m_alloc(struct mspack_system *s, size_t n) { return calloc(n, 1); }
static void m_free(void *p) { free(p); }
static void m_copy(void *s, void *d, size_t n) { memmove(d, s, n); }

static struct mspack_system sys = { m_open, m_close, m_read, m_write, m_seek,
                                     m_tell, m_msg, m_alloc, m_free, m_copy, NULL };

/* returns 0 on success, otherwise error code; window_bits 15..21 */
int fz_lzx_decompress(const unsigned char *src, long src_len, unsigned char *dst,
                      long dst_len, int window_bits) {
  unsigned char *cat = malloc(src_len + 64);
  long p = 0, c = 0; int rc;
  while (p + 2 <= src_len) {
    long csz, usz = 0x8000;
    if (src[p] == 0xFF) {
      if (p + 5 > src_len) break;
      usz = (src[p+1] << 8) | src[p+2]; csz = (src[p+3] << 8) | src[p+4]; p += 5;
    } else { csz = (src[p] << 8) | src[p+1]; p += 2; }
    if (csz == 0 || p + csz > src_len) break;
    memcpy(cat + c, src + p, csz); c += csz; p += csz; (void)usz;
  }
  memset(cat + c, 0, 64);
  memf in = { (char *)cat, c, 0 }, out = { (char *)dst, dst_len, 0 };
  struct lzxd_stream *lz = lzxd_init(&sys, (struct mspack_file *)&in,
      (struct mspack_file *)&out, window_bits, 0, 0x8000, dst_len, 0);
  if (!lz) { free(cat); return 100; }
  rc = lzxd_decompress(lz, dst_len);
  lzxd_free(lz); free(cat);
  return rc;
}
