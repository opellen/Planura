#include "tools/vcb_parser.h"

#include <optional>
#include <string>

#include <gtest/gtest.h>

namespace {

using plnr::tools::parseVcb;
using plnr::tools::VcbValue;

TEST(VcbParserTest, ParsesIntegerScalar) {
    const std::optional<VcbValue> result = parseVcb("1200");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, VcbValue::Kind::Scalar);
    EXPECT_DOUBLE_EQ(result->a, 1200.0);
}

TEST(VcbParserTest, ParsesDecimalScalar) {
    const std::optional<VcbValue> result = parseVcb("2.5");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, VcbValue::Kind::Scalar);
    EXPECT_DOUBLE_EQ(result->a, 2.5);
}

TEST(VcbParserTest, ParsesNegativeScalar) {
    const std::optional<VcbValue> result = parseVcb("-3");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, VcbValue::Kind::Scalar);
    EXPECT_DOUBLE_EQ(result->a, -3.0);
}

TEST(VcbParserTest, TrimsLeadingAndTrailingSpaces) {
    const std::optional<VcbValue> result = parseVcb("  3.14  ");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, VcbValue::Kind::Scalar);
    EXPECT_DOUBLE_EQ(result->a, 3.14);
}

TEST(VcbParserTest, ParsesDimsWithComma) {
    const std::optional<VcbValue> result = parseVcb("1000,2000");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, VcbValue::Kind::Dims2);
    EXPECT_DOUBLE_EQ(result->a, 1000.0);
    EXPECT_DOUBLE_EQ(result->b, 2000.0);
}

TEST(VcbParserTest, ParsesDimsWithSemicolon) {
    const std::optional<VcbValue> result = parseVcb("1000;2000");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, VcbValue::Kind::Dims2);
    EXPECT_DOUBLE_EQ(result->a, 1000.0);
    EXPECT_DOUBLE_EQ(result->b, 2000.0);
}

TEST(VcbParserTest, ParsesSegmentsLowercase) {
    const std::optional<VcbValue> result = parseVcb("24s");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, VcbValue::Kind::Segments);
    EXPECT_EQ(result->count, 24);
}

TEST(VcbParserTest, ParsesSegmentsUppercase) {
    const std::optional<VcbValue> result = parseVcb("24S");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, VcbValue::Kind::Segments);
    EXPECT_EQ(result->count, 24);
}

TEST(VcbParserTest, ParsesRadius) {
    const std::optional<VcbValue> result = parseVcb("3.5r");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, VcbValue::Kind::Radius);
    EXPECT_DOUBLE_EQ(result->a, 3.5);
}

TEST(VcbParserTest, ParsesCircleSegments) {
    const std::optional<VcbValue> result = parseVcb("20c");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, VcbValue::Kind::CircleSegments);
    EXPECT_EQ(result->count, 20);
}

TEST(VcbParserTest, ZeroSegmentsParsesAsBoundary) {
    // Grammar-level parsing only -- 0 segments makes no geometric sense, but
    // validating that is the consumer's job (see parseVcb's header comment),
    // not this parser's.
    const std::optional<VcbValue> result = parseVcb("0s");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, VcbValue::Kind::Segments);
    EXPECT_EQ(result->count, 0);
}

TEST(VcbParserTest, RejectsEmptyString) {
    EXPECT_FALSE(parseVcb("").has_value());
}

TEST(VcbParserTest, RejectsWhitespaceOnlyString) {
    EXPECT_FALSE(parseVcb("   ").has_value());
}

TEST(VcbParserTest, RejectsNonNumericText) {
    EXPECT_FALSE(parseVcb("abc").has_value());
}

TEST(VcbParserTest, RejectsTrailingGarbage) {
    EXPECT_FALSE(parseVcb("12x").has_value());
}

TEST(VcbParserTest, ParsesDims3WithComma) {
    const std::optional<VcbValue> result = parseVcb("1,2,3");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, VcbValue::Kind::Dims3);
    EXPECT_DOUBLE_EQ(result->a, 1.0);
    EXPECT_DOUBLE_EQ(result->b, 2.0);
    EXPECT_DOUBLE_EQ(result->c, 3.0);
}

TEST(VcbParserTest, ParsesDims3WithSemicolon) {
    const std::optional<VcbValue> result = parseVcb("1;2;3");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, VcbValue::Kind::Dims3);
    EXPECT_DOUBLE_EQ(result->a, 1.0);
    EXPECT_DOUBLE_EQ(result->b, 2.0);
    EXPECT_DOUBLE_EQ(result->c, 3.0);
}

TEST(VcbParserTest, RejectsFourFieldDims) {
    EXPECT_FALSE(parseVcb("1,2,3,4").has_value());
}

TEST(VcbParserTest, RejectsSuffixLetterWithNoLeadingNumber) {
    EXPECT_FALSE(parseVcb("s24").has_value());
}

TEST(VcbParserTest, RejectsDoubleMinus) {
    EXPECT_FALSE(parseVcb("--3").has_value());
}

TEST(VcbParserTest, ParsesArrayTimesStar) {
    const std::optional<VcbValue> result = parseVcb("*3");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, VcbValue::Kind::ArrayTimes);
    EXPECT_EQ(result->count, 3);
}

TEST(VcbParserTest, ParsesArrayTimesLowercaseX) {
    const std::optional<VcbValue> result = parseVcb("x3");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, VcbValue::Kind::ArrayTimes);
    EXPECT_EQ(result->count, 3);
}

TEST(VcbParserTest, ParsesArrayTimesUppercaseX) {
    const std::optional<VcbValue> result = parseVcb("X3");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, VcbValue::Kind::ArrayTimes);
    EXPECT_EQ(result->count, 3);
}

TEST(VcbParserTest, ParsesArrayDivide) {
    const std::optional<VcbValue> result = parseVcb("/2");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, VcbValue::Kind::ArrayDivide);
    EXPECT_EQ(result->count, 2);
}

TEST(VcbParserTest, ArrayFormsAcceptNonPositiveCounts) {
    // Grammar-level parsing only, same "validation is the consumer's job"
    // contract as ZeroSegmentsParsesAsBoundary above -- RotateTool's
    // onVcbCommit is what rejects N < 1.
    const std::optional<VcbValue> result = parseVcb("*0");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, VcbValue::Kind::ArrayTimes);
    EXPECT_EQ(result->count, 0);
}

TEST(VcbParserTest, RejectsArrayFormWithNonIntegerCount) {
    EXPECT_FALSE(parseVcb("*2.5").has_value());
}

TEST(VcbParserTest, RejectsLoneArrayPrefix) {
    EXPECT_FALSE(parseVcb("*").has_value());
    EXPECT_FALSE(parseVcb("/").has_value());
    EXPECT_FALSE(parseVcb("x").has_value());
}

// Slope: Protractor's rise:run VCB entry -- "1:12" -> atan(1/12) is the
// consumer's job, not this parser's (see vcb_parser.h's own Slope comment).
TEST(VcbParserTest, ParsesSlope) {
    const std::optional<VcbValue> result = parseVcb("1:12");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, VcbValue::Kind::Slope);
    EXPECT_DOUBLE_EQ(result->a, 1.0);
    EXPECT_DOUBLE_EQ(result->b, 12.0);
}

TEST(VcbParserTest, ParsesSlopeWithNegativeRise) {
    // Negative rise (downward slope) -- validation-free, same as every other
    // grammar-valid-but-semantically-special case this parser lets through.
    const std::optional<VcbValue> result = parseVcb("-1:12");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->kind, VcbValue::Kind::Slope);
    EXPECT_DOUBLE_EQ(result->a, -1.0);
    EXPECT_DOUBLE_EQ(result->b, 12.0);
}

TEST(VcbParserTest, RejectsSlopeWithThreeFields) {
    EXPECT_FALSE(parseVcb("1:12:3").has_value());
}

TEST(VcbParserTest, RejectsSlopeWithMissingHalf) {
    EXPECT_FALSE(parseVcb("1:").has_value());
    EXPECT_FALSE(parseVcb(":12").has_value());
    EXPECT_FALSE(parseVcb(":").has_value());
}

}  // namespace
