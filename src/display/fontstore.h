// SPDX-License-Identifier: GPL-2.0-only

#ifndef HUB75_FONTSTORE_H
#define HUB75_FONTSTORE_H

#include <QHash>
#include <QString>
#include <QStringList>

#include <memory>

namespace rgb_matrix {
class Font;
}

namespace hub75 {

// Provides the bitmap fonts embedded in the binary.
//
// rgb_matrix::Font can only load from a file path, so the BDF is copied out of
// the Qt resource into a temporary file on first use. That keeps the deployed
// artifact a single self-contained executable with no font files to install.
//
// "5x8" is the workhorse: 8 px tall gives exactly four text lines on a 32 px
// panel, and 5 px wide plus one pixel of kerning gives ten characters across
// a 64 px panel.
class FontStore
{
public:
    FontStore();
    ~FontStore();

    // Loads (and caches) an embedded font by name, e.g. "5x8", "4x6",
    // "tom-thumb". Returns nullptr and fills *error if it cannot be loaded.
    const rgb_matrix::Font *font(const QString &name, QString *error);

    // Convenience for the default 5x8 face; nullptr if unavailable.
    const rgb_matrix::Font *defaultFont(QString *error) { return font(QStringLiteral("5x8"), error); }

    static QStringList availableFonts();

private:
    struct Entry;
    QHash<QString, std::shared_ptr<Entry>> cache_;
};

} // namespace hub75

#endif // HUB75_FONTSTORE_H
