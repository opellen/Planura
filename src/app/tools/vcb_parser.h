#pragma once

// Pure std-only parser for the VCB (Measurements Box) text field grammar.
// No Qt/geometry dependency: tool.h includes this directly, and the gtest
// binary compiles vcb_parser.cpp with no library target of its own.

// Grammar: <num> Scalar; <num>,<num>[,<num>] (or ;-separated) Dims2/Dims3;
// <int>s Segments; <num>r Radius; <int>c CircleSegments; *N/xN/XN
// ArrayTimes; /N ArrayDivide; <num>:<num> Slope. Anything else -> nullopt.

// Grammar-valid but semantically invalid values (e.g. "0s", Slope's b==0)
// parse fine -- validation is each consumer's job. Slope's a/b are raw
// rise/run (consumer computes atan(a/b)); unit suffixes/coordinate triples aren't recognized.

#include <optional>
#include <string>

namespace plnr::tools {

struct VcbValue {
    enum class Kind { Scalar, Dims2, Dims3, Segments, Radius, CircleSegments, ArrayTimes, ArrayDivide, Slope };

    Kind kind{};
    double a{};  // Scalar's value; Dims2's/Dims3's first field; Radius's value; Slope's rise (numerator).
    double b{};  // Dims2's/Dims3's second field; Slope's run (denominator).
    double c{};  // Dims3's third field only.
    int count{};  // Segments'/CircleSegments'/ArrayTimes'/ArrayDivide's integer count only.
};

// Parses text per the grammar above. Leading/trailing whitespace is trimmed
// first; an empty or all-whitespace string is unparseable (nullopt).
std::optional<VcbValue> parseVcb(const std::string& text);

}  // namespace plnr::tools
