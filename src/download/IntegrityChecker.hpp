#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class IntegrityChecker : public QObject {
    Q_OBJECT

public:
    explicit IntegrityChecker(QObject *parent = nullptr);
    bool verifyIntegrity(const QString &folderPath, QStringList *errors = nullptr) const;
};
