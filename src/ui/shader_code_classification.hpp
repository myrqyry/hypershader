#pragma once
#include <QString>

// Leave string literals intact, but replace comment characters with spaces.
// Retaining newlines keeps meaningful shader tokens from joining accidentally.
inline QString stripShaderComments(const QString &source)
{
    QString output;
    output.reserve(source.size());
    bool line_comment = false, block_comment = false;
    QChar quote;
    for (qsizetype i = 0; i < source.size(); ++i) {
        const QChar c = source[i];
        const QChar next = i + 1 < source.size() ? source[i + 1] : QChar();
        if (line_comment) {
            if (c == QLatin1Char('\n')) {
                line_comment = false;
                output += c;
            } else {
                output += QLatin1Char(' ');
            }
        } else if (block_comment) {
            if (c == QLatin1Char('*') && next == QLatin1Char('/')) {
                output += QLatin1String("  ");
                ++i;
                block_comment = false;
            } else {
                output += c == QLatin1Char('\n') ? c : QLatin1Char(' ');
            }
        } else if (!quote.isNull()) {
            output += c;
            if (c == QLatin1Char('\\') && i + 1 < source.size()) {
                output += source[++i];
            } else if (c == quote) {
                quote = QChar();
            }
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
            output += c;
        } else if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
            line_comment = true;
            output += QLatin1String("  ");
            ++i;
        } else if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
            block_comment = true;
            output += QLatin1String("  ");
            ++i;
        } else {
            output += c;
        }
    }
    return output;
}
