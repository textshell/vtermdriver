#ifndef CAPTURE_H
#define CAPTURE_H

#include <vterm.h>

#include <QString>

QString formatString(const uint32_t *data, int maxlen);
QString formatString(const QString &data);
QString formatColor(const VTermColor &color);
QString captureAsJson(VTerm *vterm, bool global_reverse);
QString captureAsJsonWithoutOuterbraces(VTerm *vterm, bool global_reverse);

#endif
