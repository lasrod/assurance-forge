#include <gtest/gtest.h>

#include "core/sha256.h"

#include <string_view>

TEST(Sha256Test, HexDigestMatchesKnownVectors) {
    EXPECT_EQ(core::Sha256::HexDigest(std::string_view{}),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(core::Sha256::HexDigest(std::string_view{"abc"}),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}