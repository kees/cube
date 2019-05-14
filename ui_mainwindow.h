/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 5.7.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QGraphicsView>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTableView>
#include <QtWidgets/QTreeView>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QWidget *centralWidget;
    QVBoxLayout *verticalLayout_2;
    QLabel *lblDirectory;
    QHBoxLayout *horizontalLayout;
    QVBoxLayout *verticalLayout_4;
    QGraphicsView *grThumbnail;
    QTableView *tblMetadata;
    QTreeView *lstFiles;
    QStatusBar *statusBar;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName(QStringLiteral("MainWindow"));
        MainWindow->resize(624, 444);
        centralWidget = new QWidget(MainWindow);
        centralWidget->setObjectName(QStringLiteral("centralWidget"));
        QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(centralWidget->sizePolicy().hasHeightForWidth());
        centralWidget->setSizePolicy(sizePolicy);
        verticalLayout_2 = new QVBoxLayout(centralWidget);
        verticalLayout_2->setSpacing(6);
        verticalLayout_2->setContentsMargins(11, 11, 11, 11);
        verticalLayout_2->setObjectName(QStringLiteral("verticalLayout_2"));
        verticalLayout_2->setContentsMargins(-1, 9, -1, -1);
        lblDirectory = new QLabel(centralWidget);
        lblDirectory->setObjectName(QStringLiteral("lblDirectory"));
        QFont font;
        font.setPointSize(40);
        lblDirectory->setFont(font);

        verticalLayout_2->addWidget(lblDirectory);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setSpacing(6);
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        horizontalLayout->setContentsMargins(0, 0, 0, -1);
        verticalLayout_4 = new QVBoxLayout();
        verticalLayout_4->setSpacing(6);
        verticalLayout_4->setObjectName(QStringLiteral("verticalLayout_4"));
        verticalLayout_4->setContentsMargins(0, -1, -1, -1);
        grThumbnail = new QGraphicsView(centralWidget);
        grThumbnail->setObjectName(QStringLiteral("grThumbnail"));
        grThumbnail->setEnabled(true);
        QSizePolicy sizePolicy1(QSizePolicy::Preferred, QSizePolicy::Preferred);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(grThumbnail->sizePolicy().hasHeightForWidth());
        grThumbnail->setSizePolicy(sizePolicy1);
        grThumbnail->setMinimumSize(QSize(299, 173));

        verticalLayout_4->addWidget(grThumbnail);

        tblMetadata = new QTableView(centralWidget);
        tblMetadata->setObjectName(QStringLiteral("tblMetadata"));
        sizePolicy.setHeightForWidth(tblMetadata->sizePolicy().hasHeightForWidth());
        tblMetadata->setSizePolicy(sizePolicy);
        tblMetadata->setMinimumSize(QSize(0, 0));
        tblMetadata->setTextElideMode(Qt::ElideLeft);

        verticalLayout_4->addWidget(tblMetadata);


        horizontalLayout->addLayout(verticalLayout_4);

        lstFiles = new QTreeView(centralWidget);
        lstFiles->setObjectName(QStringLiteral("lstFiles"));
        lstFiles->setFont(font);
        lstFiles->setIconSize(QSize(60, 60));
        lstFiles->setSortingEnabled(false);

        horizontalLayout->addWidget(lstFiles);


        verticalLayout_2->addLayout(horizontalLayout);

        MainWindow->setCentralWidget(centralWidget);
        statusBar = new QStatusBar(MainWindow);
        statusBar->setObjectName(QStringLiteral("statusBar"));
        MainWindow->setStatusBar(statusBar);

        retranslateUi(MainWindow);

        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QApplication::translate("MainWindow", "Hello Application", Q_NULLPTR));
        lblDirectory->setText(QApplication::translate("MainWindow", "[loading]", Q_NULLPTR));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
