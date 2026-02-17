// Copyright (C) 2025 Signal Slot Inc.
// SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "tools.h"
#include "vncwidget.h"
#include <QtVncClient/QVncClient>
#include <QtNetwork/QTcpSocket>
#include <QtCore/QPromise>
#include <QtCore/QSharedPointer>
#include <QtCore/QTimer>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
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
}

void Tools::connect(const QString &host, int port, const QString &password)
{
    if (!password.isEmpty())
        d->vncClient.setPassword(password);
    d->socket.connectToHost(host, port);
}

void Tools::disconnect()
{
    d->socket.disconnectFromHost();
}

static QImage extractRegion(const QImage &image, int x, int y, int width, int height)
{
    if (width < 0)
        width = image.width() - x;
    if (height < 0)
        height = image.height() - y;
    if (x == 0 && y == 0 && width == image.width() && height == image.height())
        return image;
    return image.copy(x, y, width, height);
}

QFuture<QList<QMcpCallToolResultContent>> Tools::screenshot(int x, int y, int width, int height)
{
    if (d->vncClient.framebufferUpdatesEnabled() || d->socket.state() != QTcpSocket::ConnectedState) {
        QPromise<QList<QMcpCallToolResultContent>> promise;
        promise.start();
        QList<QMcpCallToolResultContent> content;
        content.append(QMcpCallToolResultContent(QMcpImageContent(extractRegion(d->vncClient.image(), x, y, width, height))));
        promise.addResult(content);
        promise.finish();
        return promise.future();
    }

    auto promise = QSharedPointer<QPromise<QList<QMcpCallToolResultContent>>>::create();
    promise->start();
    d->vncClient.setFramebufferUpdatesEnabled(true);
    auto conn = QSharedPointer<QMetaObject::Connection>::create();
    *conn = QObject::connect(&d->vncClient, &QVncClient::framebufferUpdated, this,
        [this, promise, conn, x, y, width, height]() {
            QObject::disconnect(*conn);
            d->vncClient.setFramebufferUpdatesEnabled(false);
            QList<QMcpCallToolResultContent> content;
            content.append(QMcpCallToolResultContent(QMcpImageContent(extractRegion(d->vncClient.image(), x, y, width, height))));
            promise->addResult(content);
            promise->finish();
        });
    return promise->future();
}

QFuture<QList<QMcpCallToolResultContent>> Tools::save(const QString &filePath, int x, int y, int width, int height)
{
    if (d->vncClient.framebufferUpdatesEnabled() || d->socket.state() != QTcpSocket::ConnectedState) {
        QPromise<QList<QMcpCallToolResultContent>> promise;
        promise.start();
        bool ok = extractRegion(d->vncClient.image(), x, y, width, height).save(filePath);
        QList<QMcpCallToolResultContent> content;
        content.append(QMcpCallToolResultContent(QMcpTextContent(ok ? QStringLiteral("true") : QStringLiteral("false"))));
        promise.addResult(content);
        promise.finish();
        return promise.future();
    }

    auto promise = QSharedPointer<QPromise<QList<QMcpCallToolResultContent>>>::create();
    promise->start();
    d->vncClient.setFramebufferUpdatesEnabled(true);
    auto conn = QSharedPointer<QMetaObject::Connection>::create();
    *conn = QObject::connect(&d->vncClient, &QVncClient::framebufferUpdated, this,
        [this, promise, conn, filePath, x, y, width, height]() {
            QObject::disconnect(*conn);
            d->vncClient.setFramebufferUpdatesEnabled(false);
            bool ok = extractRegion(d->vncClient.image(), x, y, width, height).save(filePath);
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

void Tools::dragAndDrop(int x, int y, int button)
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

    // Move to end position with button held
    QMouseEvent moveEvent(QEvent::MouseMove, endPos, endPos, Qt::NoButton, qtButton, Qt::NoModifier);
    d->vncClient.handlePointerEvent(&moveEvent);

    // Release at end position
    QMouseEvent releaseEvent(QEvent::MouseButtonRelease, endPos, endPos, qtButton, Qt::NoButton, Qt::NoModifier);
    d->vncClient.handlePointerEvent(&releaseEvent);

    d->pos = endPos;
}

void Tools::sendKey(int keysym, bool down)
{
    QKeyEvent event(down ? QEvent::KeyPress : QEvent::KeyRelease, keysym, Qt::NoModifier);
    d->vncClient.handleKeyEvent(&event);
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
    d->vncClient.setFramebufferUpdatesEnabled(visible);
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
        QVideoFrame frame(img.convertToFormat(QImage::Format_ARGB32));
        frame.setStreamFrameRate(d->recordingFps);
        d->videoFrameInput->sendVideoFrame(frame);
    });

    d->captureSession->setRecorder(d->recorder);
    d->recorder->record();
    d->recordingTimer->start();

    d->recording = true;
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

    return true;
}
#endif
