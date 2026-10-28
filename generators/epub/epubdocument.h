/*
    SPDX-FileCopyrightText: 2008 Ely Levy <elylevy@cs.huji.ac.il>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef EPUB_DOCUMENT_H
#define EPUB_DOCUMENT_H

#include <QImage>
#include <QLoggingCategory>
#include <QTextDocument>
#include <QTextObjectInterface>
#include <QUrl>
#include <QVariant>
#include <epub.h>

namespace Epub
{
// Draws images smoothly scaled to the resolution they are painted at. QPainter alone
// doesn't filter properly when scaling images down a lot, which makes high resolution
// images look bad at low zoom levels. Sizing is left to Qt's default image handler.
class ImageHandler : public QObject, public QTextObjectInterface
{
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)

public:
    ImageHandler(QTextObjectInterface *defaultHandler, QObject *parent);
    QSizeF intrinsicSize(QTextDocument *doc, int posInDocument, const QTextFormat &format) override;
    void drawObject(QPainter *painter, const QRectF &rect, QTextDocument *doc, int posInDocument, const QTextFormat &format) override;

private:
    QTextObjectInterface *mDefaultHandler;
};

class EpubDocument : public QTextDocument
{
    Q_OBJECT

public:
    explicit EpubDocument(const QString &fileName, const QFont &font);
    ~EpubDocument() override;
    bool isValid();
    struct epub *getEpub();
    void setCurrentSubDocument(const QString &doc);
    int maxContentHeight() const;
    int maxContentWidth() const;
    enum Multimedia { MovieResource = QTextDocument::UserResource, AudioResource };

protected:
    QVariant loadResource(int type, const QUrl &name) override;

private:
    QString checkCSS(const QString &css);

    struct epub *mEpub;
    QUrl mCurrentSubDocument;

    int padding;
    QFont mFont;

    friend class Converter;
};

}
Q_DECLARE_LOGGING_CATEGORY(OkularEpuDebug)
#endif
