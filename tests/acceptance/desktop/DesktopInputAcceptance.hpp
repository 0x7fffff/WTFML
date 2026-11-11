#pragma once
#include <QKeyEvent>
#include <QSaveFile>
#include <QAbstractButton>

class DesktopInputAcceptance final : public QObject {
public:
    DesktopInputAcceptance(MainWindow &window, QString root) : QObject(&window), m_window(window), m_root(std::move(root)) {
        qApp->installEventFilter(this);
        QTimer::singleShot(100,&window,[this]{snapshot("initial",false,0);});
        QTimer::singleShot(90000,&window,[this]{qApp->exit(m_tabs>0 && m_focusFailures==0?0:1);});
    }
protected:
    bool eventFilter(QObject *watched,QEvent *event) override {
        if (qobject_cast<QWidget *>(watched) && event->type()==QEvent::KeyPress) {
            auto *key=static_cast<QKeyEvent *>(event);
            if(key->key()==Qt::Key_Tab || key->key()==Qt::Key_Backtab) ++m_tabs;
            const bool spontaneous=event->spontaneous(); const int code=key->key();
            m_receiver=watched->objectName()+" / "+watched->metaObject()->className();
            auto *focus=QApplication::focusWidget();
            m_focusAtEvent=focus?QString(focus->metaObject()->className())+" / "+focus->objectName():"none";
            QTimer::singleShot(60,this,[this,spontaneous,code]{snapshot("key",spontaneous,code);});
        }
        return false;
    }
private:
    void snapshot(const QString &reason,bool spontaneous,int key) {
        auto *focus=QApplication::focusWidget();
        int enabledTabControls=0;
        for(auto *button:m_window.findChildren<QAbstractButton *>())
            if(button->isVisible() && button->isEnabled() && (button->focusPolicy()&Qt::TabFocus)) ++enabledTabControls;
        const QString name=QString("input-%1.png").arg(m_samples.size());
        const bool saved=m_window.grab().save(m_root+'/'+name);
        if(reason=="key" && (!focus || !focus->isVisible() || !focus->isEnabled() || !saved)) ++m_focusFailures;
        m_samples.append(QJsonObject{{"reason",reason},{"spontaneous",spontaneous},{"key",key},
            {"focusObject",focus?focus->objectName():QString()},{"receiver",m_receiver},{"focusAtEvent",m_focusAtEvent},
            {"activeWindow",QApplication::activeWindow()?QApplication::activeWindow()->windowTitle():QString()},
            {"windowActive",m_window.isActiveWindow()},{"focusClass",focus?focus->metaObject()->className():""},
            {"keyboardRing",focus && focus->property("wtfmlKeyboardFocus").toBool()},
            {"enabledTabControls",enabledTabControls},{"saved",saved},{"file",name}});
        QSaveFile file(m_root+"/input-report.json");file.setDirectWriteFallback(false);
        if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(QJsonObject{{"samples",m_samples},{"tabEvents",m_tabs},{"focusFailures",m_focusFailures}}).toJson());file.commit();}
    }
    MainWindow &m_window;
    QString m_root, m_receiver, m_focusAtEvent;
    QJsonArray m_samples;
    int m_tabs=0, m_focusFailures=0;
};
