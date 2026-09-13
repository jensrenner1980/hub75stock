#include "fontstore.h"

#include "graphics.h"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryFile>

namespace hub75 {

struct FontStore::Entry
{
    // The temporary file has to stay alive only until LoadFont() returns, but
    // keeping it costs nothing and helps when debugging.
    std::unique_ptr<QTemporaryFile> file;
    std::unique_ptr<rgb_matrix::Font> font;
};

FontStore::FontStore() = default;
FontStore::~FontStore() = default;

QStringList FontStore::availableFonts()
{
    return { QStringLiteral("5x8"), QStringLiteral("4x6"), QStringLiteral("tom-thumb") };
}

const rgb_matrix::Font *FontStore::font(const QString &name, QString *error)
{
    const auto cached = cache_.constFind(name);
    if (cached != cache_.constEnd())
        return cached.value()->font.get();

    const QString resourcePath = QStringLiteral(":/fonts/%1.bdf").arg(name);
    if (!QFile::exists(resourcePath)) {
        if (error) {
            *error = QStringLiteral("unknown font \"%1\" (available: %2)")
                         .arg(name, availableFonts().join(QStringLiteral(", ")));
        }
        return nullptr;
    }

    auto entry = std::make_shared<Entry>();
    entry->file = std::make_unique<QTemporaryFile>(
        QDir::tempPath() + QStringLiteral("/hub75stock-font-XXXXXX.bdf"));
    if (!entry->file->open()) {
        if (error) {
            *error = QStringLiteral("cannot create a temporary file for font \"%1\": %2")
                         .arg(name, entry->file->errorString());
        }
        return nullptr;
    }

    QFile resource(resourcePath);
    if (!resource.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot read embedded font \"%1\"").arg(name);
        return nullptr;
    }
    entry->file->write(resource.readAll());
    entry->file->flush();

    entry->font = std::make_unique<rgb_matrix::Font>();
    if (!entry->font->LoadFont(entry->file->fileName().toLocal8Bit().constData())) {
        if (error)
            *error = QStringLiteral("failed to parse embedded font \"%1\"").arg(name);
        return nullptr;
    }

    cache_.insert(name, entry);
    return entry->font.get();
}

} // namespace hub75
