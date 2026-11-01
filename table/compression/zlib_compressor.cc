#include "port/port_config.h"

#if HAVE_ZLIB

#include <algorithm>
#include <zlib.h>

#if HAVE_LIBDEFLATE
#include <memory>

#include <libdeflate.h>
#endif

#include "table/compression/zlib_compressor.h"

namespace leveldb {

#if HAVE_LIBDEFLATE
namespace {

struct DecompressorDeleter {
  void operator()(libdeflate_decompressor* decompressor) const {
    libdeflate_free_decompressor(decompressor);
  }
};

// A decompressor isn't thread-safe, and compactions read on their own thread.
thread_local std::unique_ptr<libdeflate_decompressor, DecompressorDeleter>
    decompressor;

// Bounds what a corrupt or hostile block can make us allocate.
constexpr size_t kMaxInflatedSize = size_t{256} << 20;

// Inflates into output, appending like inflate() does. Returns false if
// libdeflate can't decode the block, so the caller can let zlib try.
bool InflateWithLibdeflate(bool raw, const char* input, size_t length,
                           ::std::string& output) {
  if (!decompressor) {
    decompressor.reset(libdeflate_alloc_decompressor());
    if (!decompressor) {
      return false;
    }
  }
  const size_t start = output.size();
  size_t capacity = std::max<size_t>(length * 4, 64 * 1024);
  while (true) {
    output.resize(start + capacity);
    size_t actual = 0;
    const libdeflate_result result =
        raw ? libdeflate_deflate_decompress(decompressor.get(), input, length,
                                            &output[start], capacity, &actual)
            : libdeflate_zlib_decompress(decompressor.get(), input, length,
                                         &output[start], capacity, &actual);
    if (result == LIBDEFLATE_SUCCESS) {
      output.resize(start + actual);
      return true;
    }
    if (result != LIBDEFLATE_INSUFFICIENT_SPACE ||
        capacity >= kMaxInflatedSize) {
      output.resize(start);
      return false;
    }
    capacity = std::min(capacity * 2, kMaxInflatedSize);
  }
}

}  // namespace
#endif

void ZlibCompressorBase::compress(const char* input, size_t length,
                                      ::std::string& buffer) const {
  // reserve enough memory to not reallocate on the fly
  // TODO: this memsets the whole thing to zero, big waste
  buffer.resize(compressBound(length));

  z_stream strm;
  strm.zalloc = 0;
  strm.zfree = 0;
  strm.next_in = (unsigned char*)(input);
  strm.avail_in = (uint32_t)length;
  strm.next_out = (unsigned char*)&buffer[0];
  strm.avail_out = buffer.size();

  auto res = deflateInit2(&strm, compressionLevel, Z_DEFLATED, _window(), 8,
                          Z_DEFAULT_STRATEGY);
  assert(res == Z_OK);

  res = deflate(&strm, Z_FINISH);
  assert(res == Z_STREAM_END);

  buffer.resize(strm.total_out);

  deflateEnd(&strm);
}

int ZlibCompressorBase::inflate(const char* input, size_t length,
                                ::std::string& output) const {
  const int CHUNK = 64 * 1024;

  int ret;
  size_t have;
  z_stream strm;
  unsigned char out[CHUNK];

  /* allocate inflate state */
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  strm.avail_in = (uint32_t)length;
  strm.next_in = (Bytef*)input;

  ret = inflateInit2(&strm, _window());

  if (ret != Z_OK) {
    return ret;
  }

  /* decompress until deflate stream ends or end of file */
  do {
    /* run inflate() on input until output buffer not full */
    do {
      strm.avail_out = CHUNK;
      strm.next_out = out;

      ret = ::inflate(&strm, Z_NO_FLUSH);

      if (ret == Z_NEED_DICT) {
        ret = Z_DATA_ERROR;
      }
      if (ret < 0) {
        (void)inflateEnd(&strm);
        return ret;
      }

      have = CHUNK - strm.avail_out;

      output.append((char*)out, have);

    } while (strm.avail_out == 0);

    /* done when inflate() says it's done */
  } while (ret != Z_STREAM_END);

  /* clean up and return */
  (void)inflateEnd(&strm);
  return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

bool ZlibCompressorBase::decompress(const char* input, size_t length,
                                    ::std::string& output) const {
#if HAVE_LIBDEFLATE
  // the same blocks, read about twice as fast
  if (InflateWithLibdeflate(raw, input, length, output)) {
    return true;
  }
#endif
  return inflate(input, length, output) == Z_OK;
}

int ZlibCompressorBase::_window() const { return raw ? -15 : 15; }

}  // namespace leveldb

#endif