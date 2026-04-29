#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QMessageBox>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QKeyEvent>

void MainWindow::comPortListRfsh() {
    QString t = ui->comboBox_Port->currentText();
    ui->comboBox_Port->clear();
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info: infos ) {
        // Filter out virtual tty's
        if(!info.portName().contains("ttyS")) {
            ui->comboBox_Port->addItem(info.portName());
        }
    }
    ui->comboBox_Port->setCurrentText(t);
}

void MainWindow::loadFile() {
    if(sendState_e != SEND_STATE_IDLE) {
        return;
    }
    QFile f(ui->label_FilePath->text());
    if(!f.open(QIODevice::ReadOnly)) {
        QMessageBox messageBox;
        messageBox.critical(0,"Error","Can't open file:\r" + f.errorString());
        return;
    }
    ui->progressBar->setMaximum(f.size());
    ui->progressBar->setValue(0);
    dataToSend = f.readAll();
    dataToSendPtr = 0;
    f.close();
}

int MainWindow::sendData() {
    QByteArray tmp;
    int chunkLen = 16;
    if(dataToSend.length() - dataToSendPtr < chunkLen) {
        chunkLen = dataToSend.length() - dataToSendPtr;
    }
    if(chunkLen <= 0) {
        serial->write("DONE\n");
        sendState_e = SEND_STATE_IDLE;
        return 0;
    }
    tmp = dataToSend.mid(dataToSendPtr, chunkLen);
    uint16_t chk = 0;
    for (int i = 0; i <= tmp.size(); i++) {
        chk += (uint8_t)tmp.at(i);
    }
    tmp.append((chk >> 8) & 0xFF);
    tmp.append(chk & 0xFF);
    chk = 0 - chk;
    tmp.append((chk >> 8));
    tmp.append(chk);
    QString arr(tmp.toHex());
    arr = arr.remove('\0');
    serial->write(arr.toLocal8Bit() + "\r");
    dataToSendPtr += chunkLen;
    ui->progressBar->setValue(dataToSendPtr);
    qDebug() << arr.toLocal8Bit();
    return chunkLen;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow) {
    ui->setupUi(this);

    ui->plainTextEdit->installEventFilter(this);
    ui->comboBox_Port->installEventFilter(this);

    comPortListRfsh();

    serialTimeoutTimer = new QTimer;
    serialTimeoutTimer->setInterval(10);
    rcvDataTimer = new QTimer;
    rcvDataTimer->setInterval(5);
    serial = new QSerialPort();
    connect(serialTimeoutTimer, &QTimer::timeout, this, [this]() {
        serialTimeoutTimer->stop();
        ui->plainTextEdit->setPlainText(ui->plainTextEdit->toPlainText() + receiveArray);
        if(SEND_STATE_RESET_WAIT == sendState_e) {
            if(receiveArray.contains("BOOT_LOADER_STARTED")) {
                serial->write(command.toLocal8Bit() + "\n");
                sendState_e = SEND_STATE_BOOT_WAIT;
            }
        } else if(SEND_STATE_BOOT_WAIT == sendState_e) {
            if(receiveArray.contains(command.toLocal8Bit() + "-OK")) {
                sendState_e = SEND_STATE_SEND_DATA;
                dataSentCnt = sendData();
            }
        } else if(SEND_STATE_SEND_DATA == sendState_e) {
            if(receiveArray.contains("k")) {
                dataSentCnt = sendData();
            }
            if(receiveArray.contains("e")) {
                dataToSendPtr -= dataSentCnt;
                dataSentCnt = sendData();
            }
            serialTimeoutTimer->start();
        }
        receiveArray.clear();
    });
    connect(serial, &QSerialPort::readyRead, this, [this]() {
        receiveArray.append(serial->readAll());
        serialTimeoutTimer->stop();
        serialTimeoutTimer->start();
    });
    connect(ui->pushButton_Connect, &QPushButton::clicked, this, [this]() {
        if(!ui->pushButton_Connect->text().compare("Open")) {
            ui->pushButton_Connect->setText("Close");
            serial->setPortName(ui->comboBox_Port->currentText());
            serial->setBaudRate(QSerialPort::Baud115200);
            serial->setDataBits(QSerialPort::Data8);
            serial->setParity(QSerialPort::NoParity);
            serial->setStopBits(QSerialPort::OneStop);
            serial->setFlowControl(QSerialPort::HardwareControl);
            if(serial->isOpen() || !serial->open(QIODevice::ReadWrite)) {
                ui->pushButton_Connect->setText("Open");
                QMessageBox messageBox;
                messageBox.critical(0,"Error","Can't open " + serial->portName() + ", error code: " + serial->errorString());
                return;
            }
            ui->plainTextEdit->clear();
            ui->comboBox_Port->setEnabled(false);
        } else {
            ui->pushButton_Connect->setText("Open");
            serial->close();
            ui->comboBox_Port->setEnabled(true);
        }
    });

    connect(ui->pushButton_OpenFile, &QPushButton::clicked, this, [this]() {
        QString filePath(QFileDialog::getOpenFileName(this,
        tr("Open Binary file"), "", tr("Binary Files (*.bin)")));
        if(filePath.length() != 0) {
            ui->label_FilePath->setText(filePath);
        }
    });

    connect(ui->pushButton_Clear, &QPushButton::clicked, this, [this]() {
        ui->plainTextEdit->clear();
    });

    connect(ui->pushButton_UploadDirect, &QPushButton::clicked, this, [this]() {
        loadFile();
        sendState_e = SEND_STATE_RESET_WAIT;
        command = "ENTER-BOOT-LOADER-DIRECT";
        dataToSendPtr = 0;
    });
    connect(ui->pushButton_UploadFlash, &QPushButton::clicked, this, [this]() {
        loadFile();
        sendState_e = SEND_STATE_RESET_WAIT;
        command = "ENTER-BOOT-LOADER-FLASH";
        dataToSendPtr = 0;
    });
}

MainWindow::~MainWindow() {
    if (serial->isOpen())
        serial->close();
    delete ui;
}

bool MainWindow::eventFilter( QObject *o, QEvent *e ) {
    if (o == ui->plainTextEdit && e->type() == QEvent::KeyPress ) {
        // special processing for key press
        QKeyEvent *k = (QKeyEvent *)e;
        if(serial->isOpen()) {
            serial->write(QString(k->text().toUtf8().at(0)).toUtf8());
        }
        return true;
    } else if (o == ui->comboBox_Port) {
        if(e->type() == QEvent::MouseButtonPress) {
            comPortListRfsh();
        }
    } else {
        // standard event processing
        return false;
    }
    return QMainWindow::eventFilter(o, e);
}
