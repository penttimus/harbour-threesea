// SPDX-License-Identifier: AGPL-3.0-only
#ifndef MEDIAROUTE_H
#define MEDIAROUTE_H

#include <QObject>
#include <QString>

// Where the system plays sound now (`mediaRoute`), as the route manager says: on the phone itself
// (the loudspeaker, or the earpiece while the call class is ours) or on headphones, a headset or
// Bluetooth. With those a voice message stays in them and the ear changes nothing (issue 22;
// Android does the same). A device name it doesn't know counts as headphones: no earpiece then,
// which is the side to err on. Listens from `start`, Full only, where the earpiece is.
class MediaRoute : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool headphones READ headphones NOTIFY headphonesChanged)
public:
    explicit MediaRoute(QObject *parent = nullptr);

    bool headphones() const { return m_headphones; }
    void start();

signals:
    void headphonesChanged();

protected:
    // The bus, apart from the logic.
    virtual void busListen();
    virtual void busQuery();

    void answeredRoute(const QString &device);

protected slots:
    void onRouteChanged(const QString &device, uint type);

private:
    void setOutput(const QString &device);

    bool m_headphones = false;
};

#endif // MEDIAROUTE_H
