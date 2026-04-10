/********************************************************************************
** Form generated from reading UI file 'MainWindow.ui'
**
** Created by: Qt User Interface Compiler version 6.10.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow {
public:
    QWidget *centralwidget;
    QVBoxLayout *verticalLayout;
    QHBoxLayout *topButtonLayout;
    QPushButton *startButton;
    QPushButton *downloadButton;
    QPushButton *loginButton;
    QCheckBox *offlineModeCheckBox;
    QPushButton *addFolderButton;
    QPushButton *removeFolderButton;
    QGroupBox *groupBox;
    QVBoxLayout *groupBoxLayout;
    QListWidget *versionList;
    QGroupBox *groupBox_2;
    QVBoxLayout *groupBox_2Layout;
    QTextEdit *logTextEdit;
    QMenuBar *menubar;
    QStatusBar *statusbar;

    void setupUi(QMainWindow *MainWindow) {
        if (MainWindow->objectName().isEmpty()) MainWindow->setObjectName("MainWindow");
        MainWindow->resize(900, 620);
        centralwidget = new QWidget(MainWindow);
        centralwidget->setObjectName("centralwidget");
        verticalLayout = new QVBoxLayout(centralwidget);
        verticalLayout->setObjectName("verticalLayout");
        topButtonLayout = new QHBoxLayout();
        topButtonLayout->setObjectName("topButtonLayout");
        startButton = new QPushButton(centralwidget);
        startButton->setObjectName("startButton");

        topButtonLayout->addWidget(startButton);

        downloadButton = new QPushButton(centralwidget);
        downloadButton->setObjectName("downloadButton");

        topButtonLayout->addWidget(downloadButton);

        loginButton = new QPushButton(centralwidget);
        loginButton->setObjectName("loginButton");

        topButtonLayout->addWidget(loginButton);

        offlineModeCheckBox = new QCheckBox(centralwidget);
        offlineModeCheckBox->setObjectName("offlineModeCheckBox");

        topButtonLayout->addWidget(offlineModeCheckBox);

        addFolderButton = new QPushButton(centralwidget);
        addFolderButton->setObjectName("addFolderButton");

        topButtonLayout->addWidget(addFolderButton);

        removeFolderButton = new QPushButton(centralwidget);
        removeFolderButton->setObjectName("removeFolderButton");

        topButtonLayout->addWidget(removeFolderButton);

        verticalLayout->addLayout(topButtonLayout);

        groupBox = new QGroupBox(centralwidget);
        groupBox->setObjectName("groupBox");
        groupBoxLayout = new QVBoxLayout(groupBox);
        groupBoxLayout->setObjectName("groupBoxLayout");
        versionList = new QListWidget(groupBox);
        versionList->setObjectName("versionList");

        groupBoxLayout->addWidget(versionList);

        verticalLayout->addWidget(groupBox);

        groupBox_2 = new QGroupBox(centralwidget);
        groupBox_2->setObjectName("groupBox_2");
        groupBox_2Layout = new QVBoxLayout(groupBox_2);
        groupBox_2Layout->setObjectName("groupBox_2Layout");
        logTextEdit = new QTextEdit(groupBox_2);
        logTextEdit->setObjectName("logTextEdit");
        logTextEdit->setReadOnly(true);

        groupBox_2Layout->addWidget(logTextEdit);

        verticalLayout->addWidget(groupBox_2);

        MainWindow->setCentralWidget(centralwidget);
        menubar = new QMenuBar(MainWindow);
        menubar->setObjectName("menubar");
        MainWindow->setMenuBar(menubar);
        statusbar = new QStatusBar(MainWindow);
        statusbar->setObjectName("statusbar");
        MainWindow->setStatusBar(statusbar);

        retranslateUi(MainWindow);

        QMetaObject::connectSlotsByName(MainWindow);
    }  // setupUi

    void retranslateUi(QMainWindow *MainWindow) {
        MainWindow->setWindowTitle(QCoreApplication::translate(
            "MainWindow", "WTFML \345\220\257\345\212\250\345\231\250", nullptr));
        startButton->setText(QCoreApplication::translate(
            "MainWindow", "\345\220\257\345\212\250\346\270\270\346\210\217", nullptr));
        downloadButton->setText(QCoreApplication::translate(
            "MainWindow", "\344\270\213\350\275\275\347\211\210\346\234\254", nullptr));
        loginButton->setText(QCoreApplication::translate(
            "MainWindow", "\345\276\256\350\275\257\347\231\273\345\275\225", nullptr));
        offlineModeCheckBox->setText(QCoreApplication::translate(
            "MainWindow", "\347\246\273\347\272\277\346\250\241\345\274\217", nullptr));
        addFolderButton->setText(QCoreApplication::translate(
            "MainWindow", "\346\267\273\345\212\240\346\226\207\344\273\266\345\244\271", nullptr));
        removeFolderButton->setText(QCoreApplication::translate(
            "MainWindow", "\345\210\240\351\231\244\346\226\207\344\273\266\345\244\271", nullptr));
        groupBox->setTitle(QCoreApplication::translate(
            "MainWindow", "\347\211\210\346\234\254\347\256\241\347\220\206", nullptr));
        groupBox_2->setTitle(QCoreApplication::translate(
            "MainWindow", "\346\227\245\345\277\227\350\276\223\345\207\272", nullptr));
    }  // retranslateUi
};

namespace Ui {
class MainWindow : public Ui_MainWindow {};
}  // namespace Ui

QT_END_NAMESPACE

#endif  // UI_MAINWINDOW_H
