#include "io/obj_reader.h"

#include <utility>

#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace plnr::io {

namespace {

// Exact inverse of obj_writer.cpp's toObjSpace (currently identity -- see
// obj_writer.h's Axis/units section)
geo::Vec3 fromObjSpace(const geo::Vec3& objSpace) {
    return objSpace;
}

ReadObjResult fail(int line, const QString& message) {
    ReadObjResult r;
    r.ok = false;
    r.errorLine = line;
    r.error = line > 0 ? (QStringLiteral("line %1: ").arg(line) + message) : message;
    return r;
}

// Parses a face-corner token ("5","5/2","5//3","5/2/3") to its leading v
// field (raw signed int); vt/vn parts are read but discarded.
bool parseFaceCornerIndex(const QString& token, long long& outRaw) {
    const QStringList parts = token.split(QChar('/'));
    if (parts.isEmpty() || parts[0].isEmpty()) {
        return false;
    }
    bool ok = false;
    const long long v = parts[0].toLongLong(&ok);
    if (!ok) {
        return false;
    }
    outRaw = v;
    return true;
}

const QRegularExpression& whitespaceSplitter() {
    static const QRegularExpression re(QStringLiteral("\\s+"));
    return re;
}

}  // namespace

ReadObjResult readObj(const QByteArray& bytes) {
    if (bytes.trimmed().isEmpty()) {
        return fail(0, QStringLiteral("empty file"));
    }

    ObjMesh mesh;
    const QString text = QString::fromUtf8(bytes);
    const QStringList rawLines = text.split(QChar('\n'));

    int lineNo = 0;
    for (const QString& rawLine : rawLines) {
        ++lineNo;
        QString line = rawLine;
        if (line.endsWith(QChar('\r'))) {
            line.chop(1);  // tolerate CRLF line endings
        }
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QChar('#'))) {
            continue;  // blank line, or a comment (OBJ comments always start the line with '#')
        }

        const QStringList tokens = trimmed.split(whitespaceSplitter(), Qt::SkipEmptyParts);
        if (tokens.isEmpty()) {
            continue;
        }
        const QString& tag = tokens[0];

        if (tag == QStringLiteral("v")) {
            if (tokens.size() < 4) {
                return fail(lineNo, QStringLiteral("malformed 'v' record (need x y z)"));
            }
            bool okx = false;
            bool oky = false;
            bool okz = false;
            const double x = tokens[1].toDouble(&okx);
            const double y = tokens[2].toDouble(&oky);
            const double z = tokens[3].toDouble(&okz);
            if (!okx || !oky || !okz) {
                return fail(lineNo, QStringLiteral("malformed 'v' record (non-numeric coordinate)"));
            }
            mesh.vertices.push_back(fromObjSpace(geo::Vec3{x, y, z}));
            continue;
        }

        if (tag == QStringLiteral("vn") || tag == QStringLiteral("vt")) {
            continue;  // ignored on import -- see readObj()'s own header comment
        }

        if (tag == QStringLiteral("f")) {
            if (tokens.size() < 4) {
                return fail(lineNo, QStringLiteral("malformed 'f' record (fewer than 3 vertices)"));
            }
            std::vector<std::size_t> face;
            face.reserve(static_cast<std::size_t>(tokens.size() - 1));
            for (int i = 1; i < tokens.size(); ++i) {
                long long raw = 0;
                if (!parseFaceCornerIndex(tokens[i], raw) || raw == 0) {
                    return fail(lineNo, QStringLiteral("malformed face index '%1'").arg(tokens[i]));
                }
                // OBJ spec: a positive index is 1-based absolute; a negative
                // index is relative to the CURRENT vertex count at this
                // point in the file (-1 = the most recently defined vertex).
                const long long resolved =
                    raw > 0 ? raw - 1 : static_cast<long long>(mesh.vertices.size()) + raw;
                if (resolved < 0 || resolved >= static_cast<long long>(mesh.vertices.size())) {
                    return fail(lineNo, QStringLiteral("face references unknown vertex index %1").arg(raw));
                }
                face.push_back(static_cast<std::size_t>(resolved));
            }
            mesh.faces.push_back(std::move(face));
            continue;
        }

        if (tag == QStringLiteral("g") || tag == QStringLiteral("o") || tag == QStringLiteral("mtllib") ||
            tag == QStringLiteral("usemtl")) {
            continue;  // tolerated, ignored -- see readObj()'s own header comment
        }

        // Any other/unrecognized record type -- tolerated, ignored (see
        // readObj()'s own header comment on robustness over strictness).
    }

    ReadObjResult result;
    result.ok = true;
    result.mesh = std::move(mesh);
    return result;
}

}  // namespace plnr::io
