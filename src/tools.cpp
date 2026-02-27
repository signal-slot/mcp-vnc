// Copyright (C) 2025 Signal Slot Inc.
// SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "tools.h"
#include "vncwidget.h"
#include <QtVncClient/QVncClient>
#include <QtNetwork/QTcpSocket>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QPromise>
#include <QtCore/QSharedPointer>
#include <QtCore/QTimer>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#ifdef HAVE_MULTIMEDIA
#include <QtMultimedia/QMediaCaptureSession>
#include <QtMultimedia/QMediaFormat>
#include <QtMultimedia/QMediaRecorder>
#include <QtMultimedia/QVideoFrame>
#include <QtMultimedia/QVideoFrameInput>
#endif

class Tools::Private
{
public:
    QTcpSocket socket;
    QVncClient vncClient;
    VncWidget *previewWidget = nullptr;
    bool previewEnabled = false;
    QPointF pos;

    // Macro members
    QString macroDir;
    bool macroPlaying = false;

#ifdef HAVE_MULTIMEDIA
    // Recording members
    QMediaCaptureSession *captureSession = nullptr;
    QMediaRecorder *recorder = nullptr;
    QVideoFrameInput *videoFrameInput = nullptr;
    QTimer *recordingTimer = nullptr;
    bool recording = false;
    bool readyForFrame = false;
    int recordingFps = 10;
#endif

    void updateFramebufferUpdates()
    {
        bool needed = previewEnabled;
#ifdef HAVE_MULTIMEDIA
        needed = needed || recording;
#endif
        vncClient.setFramebufferUpdatesEnabled(needed);
    }
};

Tools::Tools(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->vncClient.setSocket(&d->socket);
    d->vncClient.setFramebufferUpdatesEnabled(false);
    QObject::connect(&d->vncClient, &QVncClient::connectionStateChanged, this, [this](bool connected) {
        if (!d->previewWidget)
            return;
        if (connected && d->previewEnabled)
            d->previewWidget->show();
        else if (!connected)
            d->previewWidget->hide();
    });
    QObject::connect(&d->vncClient, &QVncClient::cursorPosChanged, this, [this](const QPoint &pos) {
        d->pos = QPointF(pos);
    });
}

Tools::~Tools()
{
#ifdef HAVE_MULTIMEDIA
    if (d->recording)
        stopRecording();
#endif
}

QVncClient *Tools::client() const
{
    return &d->vncClient;
}

void Tools::setPreviewWidget(VncWidget *widget)
{
    d->previewWidget = widget;
    if (widget) {
        QObject::connect(widget, &VncWidget::closed, this, [this]() {
            d->previewEnabled = false;
            d->updateFramebufferUpdates();
        });
    }
}

QFuture<QList<QMcpCallToolResultContent>> Tools::connect(const QString &host, int port, const QString &password)
{
    if (!password.isEmpty())
        d->vncClient.setPassword(password);

    auto promise = QSharedPointer<QPromise<QList<QMcpCallToolResultContent>>>::create();
    promise->start();

    auto connTcp = QSharedPointer<QMetaObject::Connection>::create();
    auto connFb = QSharedPointer<QMetaObject::Connection>::create();
    auto connErr = QSharedPointer<QMetaObject::Connection>::create();
    auto connDisc = QSharedPointer<QMetaObject::Connection>::create();

    auto cleanup = [connTcp, connFb, connErr, connDisc]() {
        QObject::disconnect(*connTcp);
        QObject::disconnect(*connFb);
        QObject::disconnect(*connErr);
        QObject::disconnect(*connDisc);
    };

    // Wait for TCP connection before enabling framebuffer updates.
    // Enabling before connected triggers QVncClient read() on an unconnected socket → SIGSEGV.
    *connTcp = QObject::connect(&d->socket, &QTcpSocket::connected, this,
        [this]() {
            d->vncClient.setFramebufferUpdatesEnabled(true);
        });

    // Wait for the first framebuffer update (handshake complete + pixel data received)
    *connFb = QObject::connect(&d->vncClient, &QVncClient::framebufferUpdated, this,
        [this, promise, cleanup]() {
            cleanup();
            d->updateFramebufferUpdates();
            QList<QMcpCallToolResultContent> content;
            content.append(QMcpCallToolResultContent(QMcpTextContent(status())));
            promise->addResult(content);
            promise->finish();
        });

    // Handle socket errors
    *connErr = QObject::connect(&d->socket, &QTcpSocket::errorOccurred, this,
        [this, promise, cleanup](QAbstractSocket::SocketError) {
            cleanup();
            d->updateFramebufferUpdates();
            QList<QMcpCallToolResultContent> content;
            content.append(QMcpCallToolResultContent(QMcpTextContent(
                QStringLiteral("Error: %1").arg(d->socket.errorString()))));
            promise->addResult(content);
            promise->finish();
        });

    // Handle unexpected disconnection during handshake
    *connDisc = QObject::connect(&d->socket, &QTcpSocket::disconnected, this,
        [this, promise, cleanup]() {
            cleanup();
            d->updateFramebufferUpdates();
            QList<QMcpCallToolResultContent> content;
            content.append(QMcpCallToolResultContent(QMcpTextContent(
                QStringLiteral("Error: disconnected during handshake"))));
            promise->addResult(content);
            promise->finish();
        });

    d->socket.connectToHost(host, port);
    return promise->future();
}

void Tools::disconnect()
{
    d->socket.disconnectFromHost();
}

static QImage extractRegion(const QImage &image, int x, int y, int width, int height)
{
    if (image.isNull())
        return {};
    if (width < 0)
        width = image.width() - x;
    if (height < 0)
        height = image.height() - y;
    if (x == 0 && y == 0 && width == image.width() && height == image.height())
        return image;
    return image.copy(x, y, width, height);
}

static QList<QMcpCallToolResultContent> imageOrError(const QImage &image)
{
    QList<QMcpCallToolResultContent> content;
    if (image.isNull())
        content.append(QMcpCallToolResultContent(QMcpTextContent(QStringLiteral("Error: no framebuffer available or region is out of bounds"))));
    else
        content.append(QMcpCallToolResultContent(QMcpImageContent(image)));
    return content;
}

static QImage compositeWithCursor(const QImage &framebuffer, const QVncClient *client, const QPointF &fallbackPos)
{
    if (framebuffer.isNull())
        return framebuffer;

    QImage result = framebuffer.copy();
    QPainter painter(&result);

    if (!client->cursorImage().isNull()) {
        // Server provided cursor shape via RichCursor pseudo-encoding
        const QPoint pos = client->cursorPos();
        const QPoint hotspot = client->cursorHotspot();
        painter.drawImage(pos - hotspot, client->cursorImage());
    } else {
        // Fallback: draw a simple arrow cursor at last known position
        const int x = qRound(fallbackPos.x());
        const int y = qRound(fallbackPos.y());

        static const QPointF arrowShape[] = {
            {0, 0}, {0, 12}, {3, 10}, {6, 15}, {8, 14}, {5, 9}, {9, 9}
        };
        QPainterPath path;
        path.moveTo(arrowShape[0]);
        for (int i = 1; i < 7; i++)
            path.lineTo(arrowShape[i]);
        path.closeSubpath();

        painter.translate(x, y);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Qt::black, 1));
        painter.setBrush(Qt::white);
        painter.drawPath(path);
    }

    painter.end();
    return result;
}

QFuture<QList<QMcpCallToolResultContent>> Tools::screenshot(int x, int y, int width, int height)
{
    if (d->vncClient.framebufferUpdatesEnabled() || d->socket.state() != QTcpSocket::ConnectedState) {
        QPromise<QList<QMcpCallToolResultContent>> promise;
        promise.start();
        QImage img = compositeWithCursor(d->vncClient.image(), &d->vncClient, d->pos);
        promise.addResult(imageOrError(extractRegion(img, x, y, width, height)));
        promise.finish();
        return promise.future();
    }

    auto promise = QSharedPointer<QPromise<QList<QMcpCallToolResultContent>>>::create();
    promise->start();
    d->vncClient.setFramebufferUpdatesEnabled(true);
    auto hasImageData = QSharedPointer<bool>::create(false);
    auto connImg = QSharedPointer<QMetaObject::Connection>::create();
    auto connFb = QSharedPointer<QMetaObject::Connection>::create();
    // Track when real pixel data arrives (not just cursor pseudo-encoding)
    *connImg = QObject::connect(&d->vncClient, &QVncClient::imageChanged, this,
        [hasImageData](const QRect &) {
            *hasImageData = true;
        });
    *connFb = QObject::connect(&d->vncClient, &QVncClient::framebufferUpdated, this,
        [this, promise, connImg, connFb, hasImageData, x, y, width, height]() {
            if (!*hasImageData)
                return; // cursor-only update, wait for real pixel data
            QObject::disconnect(*connImg);
            QObject::disconnect(*connFb);
            d->updateFramebufferUpdates();
            QImage img = compositeWithCursor(d->vncClient.image(), &d->vncClient, d->pos);
            promise->addResult(imageOrError(extractRegion(img, x, y, width, height)));
            promise->finish();
        });
    return promise->future();
}

QFuture<QList<QMcpCallToolResultContent>> Tools::save(const QString &filePath, int x, int y, int width, int height)
{
    if (d->vncClient.framebufferUpdatesEnabled() || d->socket.state() != QTcpSocket::ConnectedState) {
        QPromise<QList<QMcpCallToolResultContent>> promise;
        promise.start();
        QImage img = compositeWithCursor(d->vncClient.image(), &d->vncClient, d->pos);
        bool ok = extractRegion(img, x, y, width, height).save(filePath);
        QList<QMcpCallToolResultContent> content;
        content.append(QMcpCallToolResultContent(QMcpTextContent(ok ? QStringLiteral("true") : QStringLiteral("false"))));
        promise.addResult(content);
        promise.finish();
        return promise.future();
    }

    auto promise = QSharedPointer<QPromise<QList<QMcpCallToolResultContent>>>::create();
    promise->start();
    d->vncClient.setFramebufferUpdatesEnabled(true);
    auto hasImageData = QSharedPointer<bool>::create(false);
    auto connImg = QSharedPointer<QMetaObject::Connection>::create();
    auto connFb = QSharedPointer<QMetaObject::Connection>::create();
    // Track when real pixel data arrives (not just cursor pseudo-encoding)
    *connImg = QObject::connect(&d->vncClient, &QVncClient::imageChanged, this,
        [hasImageData](const QRect &) {
            *hasImageData = true;
        });
    *connFb = QObject::connect(&d->vncClient, &QVncClient::framebufferUpdated, this,
        [this, promise, connImg, connFb, hasImageData, filePath, x, y, width, height]() {
            if (!*hasImageData)
                return; // cursor-only update, wait for real pixel data
            QObject::disconnect(*connImg);
            QObject::disconnect(*connFb);
            d->updateFramebufferUpdates();
            QImage img = compositeWithCursor(d->vncClient.image(), &d->vncClient, d->pos);
            bool ok = extractRegion(img, x, y, width, height).save(filePath);
            QList<QMcpCallToolResultContent> content;
            content.append(QMcpCallToolResultContent(QMcpTextContent(ok ? QStringLiteral("true") : QStringLiteral("false"))));
            promise->addResult(content);
            promise->finish();
        });
    return promise->future();
}

QString Tools::status() const
{
    if (d->socket.state() == QTcpSocket::ConnectedState) {
        return QStringLiteral("connected to %1:%2 (%3x%4)")
            .arg(d->socket.peerName())
            .arg(d->socket.peerPort())
            .arg(d->vncClient.framebufferWidth())
            .arg(d->vncClient.framebufferHeight());
    }
    return QStringLiteral("disconnected");
}

void Tools::mouseMove(int x, int y, int button)
{
    Qt::MouseButton qtButton = Qt::NoButton;
    if (button == 1)
        qtButton = Qt::LeftButton;
    else if (button == 2)
        qtButton = Qt::MiddleButton;
    else if (button == 3)
        qtButton = Qt::RightButton;

    d->pos = QPointF(x, y);

    QMouseEvent event(QEvent::MouseMove, d->pos, d->pos, Qt::NoButton, qtButton, Qt::NoModifier);
    d->vncClient.handlePointerEvent(&event);
}

void Tools::mouseClick(int x, int y, int button)
{
    Qt::MouseButton qtButton = Qt::LeftButton;
    if (button == 2)
        qtButton = Qt::MiddleButton;
    else if (button == 3)
        qtButton = Qt::RightButton;

    d->pos = QPointF(x, y);

    // Press
    QMouseEvent pressEvent(QEvent::MouseButtonPress, d->pos, d->pos, qtButton, qtButton, Qt::NoModifier);
    d->vncClient.handlePointerEvent(&pressEvent);

    // Release
    QMouseEvent releaseEvent(QEvent::MouseButtonRelease, d->pos, d->pos, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    d->vncClient.handlePointerEvent(&releaseEvent);
}

void Tools::doubleClick(int x, int y, int button)
{
    Qt::MouseButton qtButton = Qt::LeftButton;
    if (button == 2)
        qtButton = Qt::MiddleButton;
    else if (button == 3)
        qtButton = Qt::RightButton;

    d->pos = QPointF(x, y);

    // First click: Press + Release
    QMouseEvent pressEvent(QEvent::MouseButtonPress, d->pos, d->pos, qtButton, qtButton, Qt::NoModifier);
    d->vncClient.handlePointerEvent(&pressEvent);

    QMouseEvent releaseEvent(QEvent::MouseButtonRelease, d->pos, d->pos, qtButton, Qt::NoButton, Qt::NoModifier);
    d->vncClient.handlePointerEvent(&releaseEvent);

    // Second click: DblClick + Release
    QMouseEvent dblClickEvent(QEvent::MouseButtonDblClick, d->pos, d->pos, qtButton, qtButton, Qt::NoModifier);
    d->vncClient.handlePointerEvent(&dblClickEvent);

    QMouseEvent releaseEvent2(QEvent::MouseButtonRelease, d->pos, d->pos, qtButton, Qt::NoButton, Qt::NoModifier);
    d->vncClient.handlePointerEvent(&releaseEvent2);
}

void Tools::mousePress(int x, int y, int button)
{
    Qt::MouseButton qtButton = Qt::LeftButton;
    if (button == 2)
        qtButton = Qt::MiddleButton;
    else if (button == 3)
        qtButton = Qt::RightButton;

    d->pos = QPointF(x, y);

    QMouseEvent pressEvent(QEvent::MouseButtonPress, d->pos, d->pos, qtButton, qtButton, Qt::NoModifier);
    d->vncClient.handlePointerEvent(&pressEvent);
}

void Tools::mouseRelease(int x, int y, int button)
{
    Qt::MouseButton qtButton = Qt::LeftButton;
    if (button == 2)
        qtButton = Qt::MiddleButton;
    else if (button == 3)
        qtButton = Qt::RightButton;

    d->pos = QPointF(x, y);

    QMouseEvent releaseEvent(QEvent::MouseButtonRelease, d->pos, d->pos, qtButton, Qt::NoButton, Qt::NoModifier);
    d->vncClient.handlePointerEvent(&releaseEvent);
}

void Tools::longPress(int x, int y, int duration, int button)
{
    Qt::MouseButton qtButton = Qt::LeftButton;
    if (button == 2)
        qtButton = Qt::MiddleButton;
    else if (button == 3)
        qtButton = Qt::RightButton;

    d->pos = QPointF(x, y);

    QMouseEvent pressEvent(QEvent::MouseButtonPress, d->pos, d->pos, qtButton, qtButton, Qt::NoModifier);
    d->vncClient.handlePointerEvent(&pressEvent);

    QTimer::singleShot(duration, this, [this, qtButton]() {
        QMouseEvent releaseEvent(QEvent::MouseButtonRelease, d->pos, d->pos, qtButton, Qt::NoButton, Qt::NoModifier);
        d->vncClient.handlePointerEvent(&releaseEvent);
    });
}

QFuture<QList<QMcpCallToolResultContent>> Tools::dragAndDrop(int x, int y, int button)
{
    Qt::MouseButton qtButton = Qt::LeftButton;
    if (button == 2)
        qtButton = Qt::MiddleButton;
    else if (button == 3)
        qtButton = Qt::RightButton;

    const QPointF endPos(x, y);

    // Press at current position
    QMouseEvent pressEvent(QEvent::MouseButtonPress, d->pos, d->pos, qtButton, qtButton, Qt::NoModifier);
    d->vncClient.handlePointerEvent(&pressEvent);

    auto promise = QSharedPointer<QPromise<QList<QMcpCallToolResultContent>>>::create();
    promise->start();

    // Delay between press and move so the remote app can enter drag mode
    QTimer::singleShot(100, this, [this, promise, endPos, qtButton]() {
        // Move to end position with button held
        QMouseEvent moveEvent(QEvent::MouseMove, endPos, endPos, Qt::NoButton, qtButton, Qt::NoModifier);
        d->vncClient.handlePointerEvent(&moveEvent);

        // Delay between move and release
        QTimer::singleShot(50, this, [this, promise, endPos, qtButton]() {
            // Release at end position
            QMouseEvent releaseEvent(QEvent::MouseButtonRelease, endPos, endPos, qtButton, Qt::NoButton, Qt::NoModifier);
            d->vncClient.handlePointerEvent(&releaseEvent);

            d->pos = endPos;

            QList<QMcpCallToolResultContent> content;
            promise->addResult(content);
            promise->finish();
        });
    });

    return promise->future();
}

void Tools::sendKey(int keysym, bool down)
{
    QKeyEvent event(down ? QEvent::KeyPress : QEvent::KeyRelease, keysym, Qt::NoModifier);
    d->vncClient.handleKeyEvent(&event);
}

void Tools::sendKey(const QString &keysym, bool down)
{
    bool ok;
    int value = keysym.toInt(&ok, 0);
    if (ok)
        sendKey(value, down);
}

void Tools::sendText(const QString &text)
{
    for (const QChar &ch : text) {
        int keysym = ch.unicode();
        QKeyEvent pressEvent(QEvent::KeyPress, 0, Qt::NoModifier, QString(ch));
        d->vncClient.handleKeyEvent(&pressEvent);
        QKeyEvent releaseEvent(QEvent::KeyRelease, 0, Qt::NoModifier, QString(ch));
        d->vncClient.handleKeyEvent(&releaseEvent);
    }
}

void Tools::setPreview(bool visible)
{
    d->previewEnabled = visible;
    d->updateFramebufferUpdates();
    if (!d->previewWidget)
        return;
    if (visible && d->socket.state() == QTcpSocket::ConnectedState)
        d->previewWidget->show();
    else
        d->previewWidget->hide();
}

void Tools::setInteractive(bool enabled)
{
    if (d->previewWidget)
        d->previewWidget->setInteractive(enabled);
}

void Tools::setStaysOnTop(bool enabled)
{
    if (!d->previewWidget)
        return;
    const bool wasVisible = d->previewWidget->isVisible();
    d->previewWidget->setWindowFlag(Qt::WindowStaysOnTopHint, enabled);
    if (wasVisible)
        d->previewWidget->show();
}

void Tools::setPreviewTitle(const QString &title)
{
    if (!d->previewWidget)
        return;
    d->previewWidget->setWindowTitle(title);
}

// --- Macro tools ---

static const QStringList validActions = {
    QStringLiteral("mouseMove"),
    QStringLiteral("mouseClick"),
    QStringLiteral("doubleClick"),
    QStringLiteral("mousePress"),
    QStringLiteral("mouseRelease"),
    QStringLiteral("longPress"),
    QStringLiteral("dragAndDrop"),
    QStringLiteral("sendKey"),
    QStringLiteral("sendText"),
};

void Tools::setMacroDir(const QString &path)
{
    d->macroDir = path;
    QDir().mkpath(path);
}

bool Tools::createMacro(const QString &name, const QString &description)
{
    if (d->macroDir.isEmpty())
        return false;

    const QString filePath = d->macroDir + QLatin1Char('/') + name + QStringLiteral(".json");
    if (QFile::exists(filePath))
        return false;

    QJsonObject obj;
    obj[QStringLiteral("name")] = name;
    obj[QStringLiteral("description")] = description;
    obj[QStringLiteral("steps")] = QJsonArray();

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(obj).toJson());
    return true;
}

bool Tools::addMacroStep(const QString &name, const QString &action, const QString &params, int delay)
{
    if (d->macroDir.isEmpty())
        return false;
    if (!validActions.contains(action))
        return false;

    const QString filePath = d->macroDir + QLatin1Char('/') + name + QStringLiteral(".json");
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError)
        return false;

    QJsonObject obj = doc.object();
    QJsonArray steps = obj[QStringLiteral("steps")].toArray();

    QJsonParseError paramError;
    QJsonDocument paramDoc = QJsonDocument::fromJson(params.toUtf8(), &paramError);
    if (paramError.error != QJsonParseError::NoError)
        return false;

    QJsonObject step;
    step[QStringLiteral("action")] = action;
    step[QStringLiteral("params")] = paramDoc.object();
    step[QStringLiteral("delay")] = delay;
    steps.append(step);

    obj[QStringLiteral("steps")] = steps;

    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(obj).toJson());
    return true;
}

void Tools::executeStep(const QString &action, const QJsonObject &params)
{
    if (action == QLatin1String("mouseMove")) {
        mouseMove(params[QStringLiteral("x")].toInt(), params[QStringLiteral("y")].toInt(),
                  params[QStringLiteral("button")].toInt(0));
    } else if (action == QLatin1String("mouseClick")) {
        mouseClick(params[QStringLiteral("x")].toInt(), params[QStringLiteral("y")].toInt(),
                   params[QStringLiteral("button")].toInt(1));
    } else if (action == QLatin1String("doubleClick")) {
        doubleClick(params[QStringLiteral("x")].toInt(), params[QStringLiteral("y")].toInt(),
                    params[QStringLiteral("button")].toInt(1));
    } else if (action == QLatin1String("mousePress")) {
        mousePress(params[QStringLiteral("x")].toInt(), params[QStringLiteral("y")].toInt(),
                   params[QStringLiteral("button")].toInt(1));
    } else if (action == QLatin1String("mouseRelease")) {
        mouseRelease(params[QStringLiteral("x")].toInt(), params[QStringLiteral("y")].toInt(),
                     params[QStringLiteral("button")].toInt(1));
    } else if (action == QLatin1String("longPress")) {
        longPress(params[QStringLiteral("x")].toInt(), params[QStringLiteral("y")].toInt(),
                  params[QStringLiteral("duration")].toInt(1000),
                  params[QStringLiteral("button")].toInt(1));
    } else if (action == QLatin1String("dragAndDrop")) {
        dragAndDrop(params[QStringLiteral("x")].toInt(), params[QStringLiteral("y")].toInt(),
                    params[QStringLiteral("button")].toInt(1));
    } else if (action == QLatin1String("sendKey")) {
        const auto keysymValue = params[QStringLiteral("keysym")];
        if (keysymValue.isString())
            sendKey(keysymValue.toString(), params[QStringLiteral("down")].toBool());
        else
            sendKey(keysymValue.toInt(), params[QStringLiteral("down")].toBool());
    } else if (action == QLatin1String("sendText")) {
        sendText(params[QStringLiteral("text")].toString());
    }
}

QFuture<QList<QMcpCallToolResultContent>> Tools::playMacro(const QString &name, int speedFactor)
{
    if (d->macroDir.isEmpty() || d->macroPlaying) {
        QPromise<QList<QMcpCallToolResultContent>> promise;
        promise.start();
        QList<QMcpCallToolResultContent> content;
        content.append(QMcpCallToolResultContent(QMcpTextContent(
            d->macroPlaying ? QStringLiteral("Error: another macro is already playing")
                            : QStringLiteral("Error: macro directory not set"))));
        promise.addResult(content);
        promise.finish();
        return promise.future();
    }

    const QString filePath = d->macroDir + QLatin1Char('/') + name + QStringLiteral(".json");
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QPromise<QList<QMcpCallToolResultContent>> promise;
        promise.start();
        QList<QMcpCallToolResultContent> content;
        content.append(QMcpCallToolResultContent(QMcpTextContent(QStringLiteral("Error: macro '%1' not found").arg(name))));
        promise.addResult(content);
        promise.finish();
        return promise.future();
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError) {
        QPromise<QList<QMcpCallToolResultContent>> promise;
        promise.start();
        QList<QMcpCallToolResultContent> content;
        content.append(QMcpCallToolResultContent(QMcpTextContent(QStringLiteral("Error: invalid macro JSON"))));
        promise.addResult(content);
        promise.finish();
        return promise.future();
    }

    QJsonArray steps = doc.object()[QStringLiteral("steps")].toArray();
    if (steps.isEmpty()) {
        QPromise<QList<QMcpCallToolResultContent>> promise;
        promise.start();
        QList<QMcpCallToolResultContent> content;
        content.append(QMcpCallToolResultContent(QMcpTextContent(QStringLiteral("Macro completed: 0 steps executed"))));
        promise.addResult(content);
        promise.finish();
        return promise.future();
    }

    d->macroPlaying = true;
    auto promise = QSharedPointer<QPromise<QList<QMcpCallToolResultContent>>>::create();
    promise->start();

    auto stepsPtr = QSharedPointer<QJsonArray>::create(steps);
    auto indexPtr = QSharedPointer<int>::create(0);
    auto factor = qMax(1, speedFactor);

    // Use a recursive lambda via std::function to chain steps with QTimer::singleShot
    auto executeNext = QSharedPointer<std::function<void()>>::create();
    *executeNext = [this, promise, stepsPtr, indexPtr, factor, executeNext]() {
        if (*indexPtr >= stepsPtr->size()) {
            QList<QMcpCallToolResultContent> content;
            content.append(QMcpCallToolResultContent(QMcpTextContent(
                QStringLiteral("Macro completed: %1 steps executed").arg(stepsPtr->size()))));
            promise->addResult(content);
            promise->finish();
            d->macroPlaying = false;
            return;
        }

        QJsonObject step = stepsPtr->at(*indexPtr).toObject();
        int delay = step[QStringLiteral("delay")].toInt(0) * 100 / factor;
        QString action = step[QStringLiteral("action")].toString();
        QJsonObject params = step[QStringLiteral("params")].toObject();
        (*indexPtr)++;

        QTimer::singleShot(delay, this, [this, action, params, executeNext]() {
            executeStep(action, params);
            // Yield to event loop before next step (even with delay=0)
            QTimer::singleShot(0, this, [executeNext]() {
                (*executeNext)();
            });
        });
    };

    // Kick off the first step
    (*executeNext)();

    return promise->future();
}

QStringList Tools::listMacros()
{
    if (d->macroDir.isEmpty())
        return {};

    QDir dir(d->macroDir);
    QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    QStringList names;
    for (const QString &file : files)
        names.append(file.chopped(5)); // remove ".json"
    return names;
}

QString Tools::getMacro(const QString &name)
{
    if (d->macroDir.isEmpty())
        return {};

    const QString filePath = d->macroDir + QLatin1Char('/') + name + QStringLiteral(".json");
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

bool Tools::deleteMacro(const QString &name)
{
    if (d->macroDir.isEmpty())
        return false;

    const QString filePath = d->macroDir + QLatin1Char('/') + name + QStringLiteral(".json");
    return QFile::remove(filePath);
}

#ifdef HAVE_MULTIMEDIA
bool Tools::startRecording(const QString &filePath, int fps)
{
    if (d->recording)
        return false;
    if (d->socket.state() != QTcpSocket::ConnectedState)
        return false;

    const QImage &image = d->vncClient.image();
    if (image.isNull())
        return false;

    fps = qBound(1, fps, 60);

    d->videoFrameInput = new QVideoFrameInput(this);
    d->recorder = new QMediaRecorder(this);
    d->captureSession = new QMediaCaptureSession(this);

    d->captureSession->setVideoFrameInput(d->videoFrameInput);

    QMediaFormat mediaFormat(QMediaFormat::MPEG4);
    mediaFormat.setVideoCodec(QMediaFormat::VideoCodec::H264);
    d->recorder->setMediaFormat(mediaFormat);
    d->recorder->setOutputLocation(QUrl::fromLocalFile(filePath));
    d->recorder->setVideoResolution(image.size());
    d->recorder->setVideoFrameRate(fps);
    d->recorder->setQuality(QMediaRecorder::VeryHighQuality);

    d->recordingFps = fps;
    d->readyForFrame = false;

    QObject::connect(d->videoFrameInput, &QVideoFrameInput::readyToSendVideoFrame, this, [this]() {
        d->readyForFrame = true;
    });

    d->recordingTimer = new QTimer(this);
    d->recordingTimer->setTimerType(Qt::PreciseTimer);
    d->recordingTimer->setInterval(1000 / fps);
    QObject::connect(d->recordingTimer, &QTimer::timeout, this, [this]() {
        if (!d->recording || !d->readyForFrame)
            return;
        const QImage &img = d->vncClient.image();
        if (img.isNull())
            return;
        d->readyForFrame = false;
        QImage composited = compositeWithCursor(img, &d->vncClient, d->pos);
        QVideoFrame frame(composited.convertToFormat(QImage::Format_ARGB32));
        frame.setStreamFrameRate(d->recordingFps);
        d->videoFrameInput->sendVideoFrame(frame);
    });

    d->captureSession->setRecorder(d->recorder);
    d->recorder->record();
    d->recordingTimer->start();

    d->recording = true;
    d->updateFramebufferUpdates();
    return true;
}

bool Tools::stopRecording()
{
    if (!d->recording)
        return false;

    d->recording = false;

    d->recordingTimer->stop();
    d->recordingTimer->deleteLater();
    d->recordingTimer = nullptr;

    d->recorder->stop();

    d->captureSession->deleteLater();
    d->captureSession = nullptr;
    d->recorder->deleteLater();
    d->recorder = nullptr;
    d->videoFrameInput->deleteLater();
    d->videoFrameInput = nullptr;

    d->updateFramebufferUpdates();

    return true;
}
#endif
