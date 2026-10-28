/*
    SPDX-FileCopyrightText: 2008 Ely Levy <elylevy@cs.huji.ac.il>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "epubdocument.h"
#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QPainter>
#include <QTemporaryFile>

#include <QRegularExpression>

Q_LOGGING_CATEGORY(OkularEpuDebug, "org.kde.okular.generators.epu", QtWarningMsg)
using namespace Epub;

ImageHandler::ImageHandler(QTextObjectInterface *defaultHandler, QObject *parent)
    : QObject(parent)
    , mDefaultHandler(defaultHandler)
{
}

QSizeF ImageHandler::intrinsicSize(QTextDocument *doc, int posInDocument, const QTextFormat &format)
{
    return mDefaultHandler->intrinsicSize(doc, posInDocument, format);
}

void ImageHandler::drawObject(QPainter *painter, const QRectF &rect, QTextDocument *doc, int posInDocument, const QTextFormat &format)
{
    const QImage image = doc->resource(QTextDocument::ImageResource, QUrl(format.toImageFormat().name())).value<QImage>();
    if (image.isNull() || painter->transform().type() > QTransform::TxScale) {
        mDefaultHandler->drawObject(painter, rect, doc, posInDocument, format);
        return;
    }

    const QRectF targetRect = painter->transform().mapRect(rect);
    const qreal dpr = painter->device()->devicePixelRatio();
    QImage scaled = image.scaled((targetRect.size() * dpr).toSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);

    painter->save();
    painter->resetTransform();
    painter->drawImage(targetRect.topLeft(), scaled);
    painter->restore();
}

EpubDocument::EpubDocument(const QString &fileName, const QFont &font)
    : QTextDocument()
    , padding(20)
    , mFont(font)
{
#ifdef Q_OS_WIN
    mEpub = epub_open(qUtf8Printable(fileName), 2);
#else
    mEpub = epub_open(qPrintable(fileName), 2);
#endif

    setPageSize(QSizeF(600, 800));
    setDefaultStyleSheet(QStringLiteral("a { color: %1 }").arg(QColor(Qt::blue).name()));

    QAbstractTextDocumentLayout *layout = documentLayout();
    layout->registerHandler(QTextFormat::ImageObject, new ImageHandler(layout->handlerForObject(QTextFormat::ImageObject), layout));
}

bool EpubDocument::isValid()
{
    return (mEpub ? true : false);
}

EpubDocument::~EpubDocument()
{
    if (mEpub) {
        epub_close(mEpub);
    }

    epub_cleanup();
}

struct epub *EpubDocument::getEpub()
{
    return mEpub;
}

void EpubDocument::setCurrentSubDocument(const QString &doc)
{
    mCurrentSubDocument.clear();
    int index = doc.indexOf(QLatin1Char('/'));
    if (index > 0) {
        mCurrentSubDocument = QUrl::fromLocalFile(doc.left(index + 1));
    }
}

int EpubDocument::maxContentHeight() const
{
    return pageSize().height() - (2 * padding);
}

int EpubDocument::maxContentWidth() const
{
    return pageSize().width() - (2 * padding);
}

QString EpubDocument::checkCSS(const QString &c)
{
    QString css = c;
    // remove paragraph line-heights
    static const QRegularExpression lineHeightRegex {QStringLiteral("line-height\\s*:\\s*[\\w\\.]*;")};
    css.remove(lineHeightRegex);

    // HACK transform em and rem notation to px, because QTextDocument doesn't support
    // em and rem.
    static const QRegularExpression cssSplitRegex {QStringLiteral("\\s+")};
    const QStringList cssArray = css.split(cssSplitRegex);
    QStringList cssArrayReplaced;
    std::size_t cssArrayCount = cssArray.count();
    std::size_t i = 0;
    static const QRegularExpression re(QStringLiteral("(([0-9]+)(\\.[0-9]+)?)r?em(.*)"));
    while (i < cssArrayCount) {
        const auto &item = cssArray[i];
        QRegularExpressionMatch match = re.match(item);
        if (match.hasMatch()) {
            double em = match.captured(1).toDouble();
            double px = em * mFont.pointSize();
            cssArrayReplaced.append(QStringLiteral("%1px%2").arg(px).arg(match.captured(4)));
        } else {
            cssArrayReplaced.append(item);
        }
        i++;
    }
    return cssArrayReplaced.join(QStringLiteral(" "));
}

QVariant EpubDocument::loadResource(int type, const QUrl &name)
{
    int size;
    char *data = nullptr;

    QString fileInPath = mCurrentSubDocument.resolved(name).path();

    // Get the data from the epub file
    size = epub_get_data(mEpub, fileInPath.toUtf8().constData(), &data);

    if (data == nullptr || size <= 0) {
        qCWarning(OkularEpuDebug) << "EPUB : error loading resource" << fileInPath << "size" << size << "type" << type;
        free(data);
        return QVariant();
    }

    QVariant resource;

    switch (type) {
    case QTextDocument::ImageResource: {
        QImage img = QImage::fromData(reinterpret_cast<unsigned char *>(data), size);
        // Make images that are too big fit the page by raising their device pixel ratio
        // instead of scaling them down. QTextDocument lays them out using their device
        // independent size, but still paints them from the full resolution data, so they
        // don't get blurry when zooming in or on HiDPI screens.
        const qreal ratio = qMax(img.width() / qreal(maxContentWidth()), img.height() / qreal(maxContentHeight()));
        if (ratio > 1) {
            img.setDevicePixelRatio(ratio);
        }
        resource.setValue(img);
        break;
    }
    case QTextDocument::StyleSheetResource: {
        QString css = QString::fromUtf8(data);
        resource.setValue(checkCSS(css));
        break;
    }
    case EpubDocument::MovieResource: {
        QTemporaryFile *tmp = new QTemporaryFile(QStringLiteral("%1/okrXXXXXX").arg(QDir::tempPath()), this);
        if (!tmp->open()) {
            qCWarning(OkularEpuDebug) << "EPUB : error creating temporary video file";
        }
        if (tmp->write(data, size) == -1) {
            qCWarning(OkularEpuDebug) << "EPUB : error writing data" << tmp->errorString();
        }
        tmp->flush();
        resource.setValue(tmp->fileName());
        break;
    }
    case EpubDocument::AudioResource: {
        QByteArray ba(data, size);
        resource.setValue(ba);
        break;
    }
    default:
        resource.setValue(QString::fromUtf8(data));
        break;
    }

    free(data);

    // add to cache
    addResource(type, name, resource);

    return resource;
}

#include "moc_epubdocument.cpp"
