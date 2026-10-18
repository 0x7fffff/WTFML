#pragma once
#include <functional>
class QObject;
class QNetworkAccessManager;
// Optional instrumentation/transport policy. Returned managers must be parented to owner.
using NetworkManagerFactory = std::function<QNetworkAccessManager *(QObject *owner)>;
