#ifndef QLIGHTDM_CONFIG_H
#define QLIGHTDM_CONFIG_H

#include <QtCore/QObject>
#include <QtCore/QStringList>

/** A wrapper around the lightDM config file
    Returns sensible default values if not in the config file.
*/

class ConfigPrivate;

//Logic for loading file name should be here.
//For bonus points it should be async.

namespace QLightDM
{

class Q_DECL_EXPORT Config : public QObject
{
    Q_OBJECT
public:
    explicit Config(QString filePath, QObject *parent = 0);
    ~Config();

    int minimumUid() const;
    QStringList hiddenUsers() const;
    QStringList hiddenShells() const;
    bool loadUsers() const;

signals:

public slots:

private:
    ConfigPrivate *d;
};

};

#endif // CONFIG_H
