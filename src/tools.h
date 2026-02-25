// Copyright (C) 2025 Signal Slot Inc.
// SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef TOOLS_H
#define TOOLS_H

#include <QtCore/QFuture>
#include <QtCore/QObject>
#include <QtCore/QScopedPointer>
#include <QtGui/QImage>
#include <QtCore/QJsonObject>
#include <QtMcpCommon/qmcpcalltoolresultcontent.h>

class QVncClient;
class QWidget;
class VncWidget;

class Tools : public QObject
{
    Q_OBJECT
public:
    explicit Tools(QObject *parent = nullptr);
    ~Tools() override;

    QVncClient *client() const;
    void setPreviewWidget(VncWidget *widget);

    Q_INVOKABLE void connect(const QString &host, int port, const QString &password = QString());
    Q_INVOKABLE void disconnect();
    Q_INVOKABLE QFuture<QList<QMcpCallToolResultContent>> screenshot(int x = 0, int y = 0, int width = -1, int height = -1);
    Q_INVOKABLE QFuture<QList<QMcpCallToolResultContent>> save(const QString &filePath, int x = 0, int y = 0, int width = -1, int height = -1);
    Q_INVOKABLE QString status() const;
    Q_INVOKABLE void mouseMove(int x, int y, int button = 0);
    Q_INVOKABLE void mouseClick(int x, int y, int button = 1);
    Q_INVOKABLE void doubleClick(int x, int y, int button = 1);
    Q_INVOKABLE void mousePress(int x, int y, int button = 1);
    Q_INVOKABLE void mouseRelease(int x, int y, int button = 1);
    Q_INVOKABLE void longPress(int x, int y, int duration = 1000, int button = 1);
    Q_INVOKABLE void dragAndDrop(int x, int y, int button = 1);
    Q_INVOKABLE void sendKey(int keysym, bool down);
    Q_INVOKABLE void sendText(const QString &text);
    Q_INVOKABLE void setPreview(bool visible);
    Q_INVOKABLE void setInteractive(bool enabled);
    Q_INVOKABLE void setStaysOnTop(bool enabled);
    Q_INVOKABLE void setPreviewTitle(const QString &title);
    // Macro tools
    Q_INVOKABLE void setMacroDir(const QString &path);
    Q_INVOKABLE bool createMacro(const QString &name, const QString &description = QString());
    Q_INVOKABLE bool addMacroStep(const QString &name, const QString &action, const QString &params, int delay = 0);
    Q_INVOKABLE QFuture<QList<QMcpCallToolResultContent>> playMacro(const QString &name, int speedFactor = 100);
    Q_INVOKABLE QStringList listMacros();
    Q_INVOKABLE QString getMacro(const QString &name);
    Q_INVOKABLE bool deleteMacro(const QString &name);

#ifdef HAVE_MULTIMEDIA
    Q_INVOKABLE bool startRecording(const QString &filePath, int fps = 10);
    Q_INVOKABLE bool stopRecording();
#endif

private:
    void executeStep(const QString &action, const QJsonObject &params);
    class Private;
    QScopedPointer<Private> d;
};

#endif // TOOLS_H
