#pragma once
#include <QObject>
class MainWindow;
class ConnectionsController final : public QObject {
 Q_OBJECT
public:
 explicit ConnectionsController(MainWindow *owner);
 void setup();
private:
 MainWindow *m_owner = nullptr;
};

