#include "vcb_parser.h"

#include <cctype>
#include <cstdlib>
#include <vector>

namespace plnr::tools {

namespace {

std::string trim(const std::string& s) {
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    std::size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

// Parses s as a full decimal number; rejects empty input and any trailing
// garbage strtod doesn't consume (so "12x" fails).
bool parseFullDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const double value = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size()) return false;
    out = value;
    return true;
}

// Same all-consumed contract as parseFullDouble, for a signed integer.
bool parseFullInt(const std::string& s, int& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const long value = std::strtol(s.c_str(), &end, 10);
    if (end != s.c_str() + s.size()) return false;
    out = static_cast<int>(value);
    return true;
}

// Splits s on every occurrence of sep (no dedup, so "1,,2" yields three
// fields, the empty middle one unparseable downstream). Returns a
// single-element vector when sep doesn't appear at all.
std::vector<std::string> splitAll(const std::string& s, char sep) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t pos = s.find(sep, start);
        if (pos == std::string::npos) {
            parts.push_back(s.substr(start));
            break;
        }
        parts.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

}  // namespace

std::optional<VcbValue> parseVcb(const std::string& text) {
    const std::string trimmed = trim(text);
    if (trimmed.empty()) return std::nullopt;

    // "*N"/"xN"/"XN" -> ArrayTimes, "/N" -> ArrayDivide. Keyed off the first
    // character (unlike every form below): none of '*'/'x'/'X'/'/' can
    // start a valid number, so this can't shadow any other form.
    if (trimmed.size() >= 2 &&
        (trimmed.front() == '*' || trimmed.front() == 'x' || trimmed.front() == 'X' || trimmed.front() == '/')) {
        const char op = trimmed.front();
        int count = 0;
        if (!parseFullInt(trimmed.substr(1), count)) return std::nullopt;
        VcbValue v;
        v.kind = (op == '/') ? VcbValue::Kind::ArrayDivide : VcbValue::Kind::ArrayTimes;
        v.count = count;
        return v;
    }

    // Suffix forms (Segments/Radius/CircleSegments) keyed off the trimmed
    // string's last character, checked before scalar/Dims2 below -- else a
    // trailing 's'/'r'/'c' would just fail as a malformed number.
    const char last = trimmed.back();
    const std::string body = trimmed.substr(0, trimmed.size() - 1);

    if (last == 's' || last == 'S') {
        int count = 0;
        if (!parseFullInt(body, count)) return std::nullopt;
        VcbValue v;
        v.kind = VcbValue::Kind::Segments;
        v.count = count;
        return v;
    }
    if (last == 'r' || last == 'R') {
        double radius = 0.0;
        if (!parseFullDouble(body, radius)) return std::nullopt;
        VcbValue v;
        v.kind = VcbValue::Kind::Radius;
        v.a = radius;
        return v;
    }
    if (last == 'c' || last == 'C') {
        int count = 0;
        if (!parseFullInt(body, count)) return std::nullopt;
        VcbValue v;
        v.kind = VcbValue::Kind::CircleSegments;
        v.count = count;
        return v;
    }

    // Slope: <num>:<num>, checked before Dims2/Dims3 (':' is its own
    // separator). Always exactly two fields -- "1:12:3"/"1:"/":12" reject
    // rather than falling through to Dims2/Dims3 or the scalar case.
    if (trimmed.find(':') != std::string::npos) {
        const std::vector<std::string> fields = splitAll(trimmed, ':');
        if (fields.size() != 2) return std::nullopt;
        double a = 0.0, b = 0.0;
        if (!parseFullDouble(trim(fields[0]), a)) return std::nullopt;
        if (!parseFullDouble(trim(fields[1]), b)) return std::nullopt;
        VcbValue v;
        v.kind = VcbValue::Kind::Slope;
        v.a = a;
        v.b = b;
        return v;
    }

    // Dims2/Dims3: comma tried first, semicolon only if no comma appears.
    // Once a separator is found this commits to Dims2/Dims3 -- a field
    // count other than 2 or 3, or a non-numeric field, returns nullopt.
    for (const char sep : {',', ';'}) {
        if (trimmed.find(sep) == std::string::npos) continue;
        const std::vector<std::string> fields = splitAll(trimmed, sep);
        if (fields.size() != 2 && fields.size() != 3) return std::nullopt;
        double nums[3] = {0.0, 0.0, 0.0};
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (!parseFullDouble(trim(fields[i]), nums[i])) return std::nullopt;
        }
        VcbValue v;
        v.a = nums[0];
        v.b = nums[1];
        if (fields.size() == 3) {
            v.kind = VcbValue::Kind::Dims3;
            v.c = nums[2];
        } else {
            v.kind = VcbValue::Kind::Dims2;
        }
        return v;
    }

    // Plain scalar -- the fallback once no suffix letter or dims separator matched.
    double scalar = 0.0;
    if (!parseFullDouble(trimmed, scalar)) return std::nullopt;
    VcbValue v;
    v.kind = VcbValue::Kind::Scalar;
    v.a = scalar;
    return v;
}

}  // namespace plnr::tools
