// Copyright (c) 2026 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "port/port_config.h"

#include "gtest/gtest.h"

#if HAVE_ZLIB

#include <random>
#include <string>

#include "table/compression/zlib_compressor.h"

namespace leveldb {

namespace {

std::string Sample(size_t length, uint32_t seed) {
  std::mt19937 random(seed);
  std::string data(length, '\0');
  // half repetitive, half noise, like chunk records
  for (size_t i = 0; i < length; ++i) {
    data[i] = (i / 64) % 2 == 0 ? static_cast<char>(i % 7)
                                : static_cast<char>(random());
  }
  return data;
}

void RoundTrip(const ZlibCompressorBase& compressor, size_t length) {
  const std::string input = Sample(length, static_cast<uint32_t>(length));
  std::string compressed;
  compressor.compress(input.data(), input.size(), compressed);

  // decompress appends, like the zlib path always has
  std::string output = "prefix";
  ASSERT_TRUE(compressor.decompress(compressed.data(), compressed.size(), output));
  ASSERT_EQ(output, "prefix" + input);

  std::string reference;
  ASSERT_EQ(compressor.inflate(compressed.data(), compressed.size(), reference),
            0);
  ASSERT_EQ(reference, input);
}

}  // namespace

TEST(ZlibCompressor, RoundTripsEverySize) {
  const ZlibCompressorRaw raw;
  const ZlibCompressor wrapped;
  // includes sizes past the first output guess, which make the buffer grow
  for (size_t length : {size_t{0}, size_t{1}, size_t{100}, size_t{16 << 10},
                        size_t{64 << 10}, size_t{1 << 20}, size_t{5 << 20}}) {
    RoundTrip(raw, length);
    RoundTrip(wrapped, length);
  }
}

TEST(ZlibCompressor, HighlyCompressibleBlock) {
  // inflates to far more than four times its size on the first try
  const ZlibCompressorRaw raw;
  const std::string input(8 << 20, 'a');
  std::string compressed;
  raw.compress(input.data(), input.size(), compressed);
  std::string output;
  ASSERT_TRUE(raw.decompress(compressed.data(), compressed.size(), output));
  ASSERT_EQ(output, input);
}

TEST(ZlibCompressor, RejectsCorruptBlocks) {
  const ZlibCompressorRaw raw;
  const std::string input = Sample(32 << 10, 7);
  std::string compressed;
  raw.compress(input.data(), input.size(), compressed);

  std::string truncated = compressed.substr(0, compressed.size() / 2);
  std::string output = "kept";
  ASSERT_FALSE(raw.decompress(truncated.data(), truncated.size(), output));

  std::string garbage(compressed.size(), '\xff');
  output = "kept";
  ASSERT_FALSE(raw.decompress(garbage.data(), garbage.size(), output));
}

}  // namespace leveldb

#endif  // HAVE_ZLIB

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
