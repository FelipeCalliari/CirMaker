#pragma once

#include <QString>

namespace cm {

// "U2" before "U10": runs of digits compare as numbers, the rest without
// case. QCollator's numeric mode does this only with ICU and a real locale
// (under LANG=C it sorts "U10" before "U2"), so it is done here.
struct NaturalOrder {
    int compare(const QString& a, const QString& b) const
    {
        qsizetype i = 0, j = 0;
        while (i < a.size() && j < b.size()) {
            if (a[i].isDigit() && b[j].isDigit()) {
                qsizetype ie = i, je = j;
                while (ie < a.size() && a[ie].isDigit()) ++ie;
                while (je < b.size() && b[je].isDigit()) ++je;
                // without leading zeros, the longer number is the larger
                qsizetype is = i, js = j;
                while (is + 1 < ie && a[is] == u'0') ++is;
                while (js + 1 < je && b[js] == u'0') ++js;
                if (ie - is != je - js) return ie - is < je - js ? -1 : 1;
                for (; is < ie; ++is, ++js)
                    if (a[is] != b[js]) return a[is] < b[js] ? -1 : 1;
                i = ie;
                j = je;
                continue;
            }
            const QChar x = a[i].toCaseFolded(), y = b[j].toCaseFolded();
            if (x != y) return x < y ? -1 : 1;
            ++i;
            ++j;
        }
        if (i < a.size() || j < b.size()) return i < a.size() ? 1 : -1;
        return QString::compare(a, b);      // only case or leading zeros differ
    }
    bool operator()(const QString& a, const QString& b) const { return compare(a, b) < 0; }
};

} // namespace cm
