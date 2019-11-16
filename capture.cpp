#include "capture.h"

#include <QString>
#include <QVector>

static const char *const names[16] = {
    "black",
    "red",
    "green",
    "yellow",
    "blue",
    "magenta",
    "cyan",
    "white",
    "bright black",
    "bright red",
    "bright green",
    "bright yellow",
    "bright blue",
    "bright magenta",
    "bright cyan",
    "bright white",
};

QString hex(unsigned val, int width) {
    QString str = QString::number(val, 16);
    return QStringLiteral("0").repeated(width - str.size()) + str;
}


QString formatColor(const VTermColor &color) {
    if ((color.type & VTERM_COLOR_DEFAULT_MASK) == 0) {
        if (VTERM_COLOR_IS_RGB(&color)) {
            return QStringLiteral("#%0%1%2")
                    .arg(hex(color.rgb.red, 2), hex(color.rgb.green, 2), hex(color.rgb.blue, 2));
        } else if (VTERM_COLOR_IS_INDEXED(&color)) {
            if (color.indexed.idx < 16) {
                return QStringLiteral("%0").arg(names[color.indexed.idx]);
            } else {
                return QStringLiteral("%0").arg(QString::number(color.indexed.idx));
            }
        }
    }
    return {};
}

static QString format_color(const char* name, const VTermColor &color) {
    QString result;
    QString colorAsString = formatColor(color);
    if (colorAsString.size()) {
        result += QStringLiteral(", \"%0\": \"").arg(name);
        result += colorAsString + "\"";
    }
    return result;
}

QString formatString(const uint32_t *data, int maxlen) {
    QString result;
    for (int i = 0; i < maxlen; i++) {
        unsigned ch = data[i];
        auto str = QString::fromUcs4(data + i, 1);
        if (ch == 0) break;
        if (ch >= 32 && ch <= 126 && ch != '"' && ch != '\\') {
            result += str;
        } else {
            result += QStringLiteral("\\u%0").arg(hex(str.at(0).unicode(), 4));
            if (str.size() > 1) {
                result += QStringLiteral("\\u%0").arg(hex(str.at(1).unicode(), 4));
            }
        }
    }
    return result;
}

QString formatString(const QString &data) {
    QVector<uint> utf32 = data.toUcs4();
    return formatString(utf32.data(), utf32.size());
}

QString captureAsJson(VTerm *vterm, bool global_reverse) {
    QString result;
    return QStringLiteral("{\n") + captureAsJsonWithoutOuterbraces(vterm, global_reverse)
            + QStringLiteral("}\n");
}

QString captureAsJsonWithoutOuterbraces(VTerm *vterm, bool global_reverse) {
    /* missing details:
      protected_cell -> unclear how to model
      dwl, dhl -> would need per line information storage, also no longer widely supported
      continuation -> would need per line information storage
    */
    QString result;
    int width;
    int height;

    vterm_get_size(vterm, &height, &width);
    VTermScreen *vts = vterm_obtain_screen(vterm);
    VTermState *state = vterm_obtain_state(vterm);

    result += QStringLiteral("  \"width\": %0, \"height\": %1, \"version\": 0, ")
            .arg(QString::number(width), QString::number(height));
    QString errors;
    QString cellsStr = "\"cells\":[\n";
    QString lineMeta;
    for (int y = 0; y < height; y++) {
        bool softWrapped = y + 1 < height ? vterm_state_get_lineinfo(state, y + 1)->continuation
                                          : false;
        if (softWrapped) {
            if (lineMeta.size()) {
                lineMeta += ",\n";
            }
            lineMeta += QStringLiteral("    \"%1\": { \"soft_wrapped\": true }").arg(QString::number(y));
        }
        // soft_wrapped
        for (int x = 0; x < width; x++) {
            VTermScreenCell cell;
            vterm_screen_get_cell(vts, {y, x}, &cell);
            if (cell.chars[0] == static_cast<uint32_t>(-1)) {
                errors += QStringLiteral("unexpected Continuation cell at %0,%1;")
                        .arg(QString::number(x), QString::number(y));
                continue;
            }
            cellsStr += QStringLiteral("    {\"x\": %0, \"y\": %1,\n")
                    .arg(QString::number(x), QString::number(y));
            cellsStr += QStringLiteral("     \"t\": \"");
            if (cell.chars[0]) {
                cellsStr += formatString(cell.chars, VTERM_MAX_CHARS_PER_CELL);
            } else {
                cellsStr += QStringLiteral(" ");
            }
            cellsStr += QStringLiteral("\"");

            if (!cell.chars[0]) {
                // the cell was erased/cleared
                cellsStr += QStringLiteral(", \"cleared\": true");
            }

            if (cell.width != 1) {
                cellsStr += QStringLiteral(", \"width\": %0").arg(QString::number(cell.width));
            }

            cellsStr += format_color("fg", cell.fg);
            cellsStr += format_color("bg", cell.bg);

            if (cell.attrs.bold) cellsStr += QStringLiteral(", \"bold\": true");
            if (cell.attrs.italic) cellsStr += QStringLiteral(", \"italic\": true");
            if (cell.attrs.blink) cellsStr += QStringLiteral(", \"blink\": true");
            // the reverse here already has the global inverse xored in. Need to undo that.
            if (cell.attrs.reverse ^ global_reverse) cellsStr += QStringLiteral(", \"inverse\": true");
            if (cell.attrs.strike) cellsStr += QStringLiteral(", \"strike\": true");

            if (cell.attrs.underline == VTERM_UNDERLINE_SINGLE) cellsStr += QStringLiteral(", \"underline\": true");
            if (cell.attrs.underline == VTERM_UNDERLINE_DOUBLE) cellsStr += QStringLiteral(", \"double_underline\": true");
            if (cell.attrs.underline == VTERM_UNDERLINE_CURLY) cellsStr += QStringLiteral(", \"curly_underline\": true");

            x += cell.width - 1;

            if (x == width-1 && y == height - 1) {
                cellsStr += QStringLiteral("}\n");
            } else {
                cellsStr += QStringLiteral("},\n");
            }
        }
        cellsStr += QStringLiteral("\n");
    }
    cellsStr += QStringLiteral("]");

    if (lineMeta.size()) {
        lineMeta = ",\n  \"lines\": {" + lineMeta + "\n  }";
    }

    if (errors.size()) {
        errors = QStringLiteral("\n  \"errors\": \"\",\n").arg(errors);
    }

    return result + errors + cellsStr + lineMeta;
}
