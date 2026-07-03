#include "sacm/version.h"

#include <gtest/gtest.h>

namespace {

TEST(Sacm23Library, SACM23_LIB_001_ExposesSacm23VersionMetadata) {
    EXPECT_EQ(sacm::standard_version(), "2.3");
    EXPECT_FALSE(sacm::library_version().empty());
}

}  // namespace
