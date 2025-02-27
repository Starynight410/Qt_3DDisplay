#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "pcie_fun.h"

#include <math.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <experimental/filesystem>
#include <iostream>
#include <fstream>
#include <QPushButton>
#include <QCoreApplication>
#include <QToolBox>
#include <QProcessEnvironment>
#include <QTimer>
#include <QMessageBox>
#include <QProcess>

using namespace std;

// Uart Controller part
#define START           "AT+START\n"
#define LED_ON          "$ w 0"
#define LED_OFF         "$ w 1"
#define TRIGGER         "$ t 1"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , predefinedTriggerCount(6) // 雷达采集的帧数
    , currentTriggerCount(0)
{
    ui->setupUi(this);

    // Initialize 3D graph
    initializeGraph();
    generateData();

    // Set up the timer to update the graph every second
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::updateGraph);
    timer->start(1000);  // Start the timer with a 1-second interval

    // Adding title for widget
    QWidget::setWindowTitle("SingleSerial");

    //Read BaudRate supported by Pc and attach to BaudRateBox
    QList<qint32> baudRates = info.standardBaudRates(); // What baudrates does my computer support
    QList<QString> stringBaudRates;
    for(int i = 0 ; i < baudRates.size() ; i++){
        stringBaudRates.append(QString::number(baudRates.at(i)));
    }
    ui->BaudRate->addItem("19200");
    ui->BaudRate->addItems(stringBaudRates);
    ui->lineEdit->setText("AT+RESET\n");

    // Read current ports
    QList<QSerialPortInfo> ports = info.availablePorts();
    QList<QString> stringPorts;
    for(int i = 0 ; i < ports.size() ; i++){
        stringPorts.append(ports.at(i).portName() + " " + ports.at(i).description());
    }
    ui->Ports->addItems(stringPorts);

    for(int i = 0; i < 8; i++){
        flag[i] = 0;
    }

    // pcie init
    int pcie_success = pcie_init();

    if(pcie_success != 1) {
      QMessageBox::information(this,"ERROR","pcie init error");
      return;
    }

    for (int i = 0; i < 8; ++i) {
        pcie_c2h_event[i].event_id = i;
        pcie_c2h_event[i].isplay = true; // Ensure threads will run
        connect(&pcie_c2h_event[i], &event_thread::sig_inform_disp, this, &MainWindow::itr_process, Qt::QueuedConnection);
        pcie_c2h_event[i].start();
    }

}

MainWindow::~MainWindow()
{
    for (int i = 0; i < 8; ++i) {
        pcie_c2h_event[i].isplay = false;
        if (pcie_c2h_event[i].isRunning()) {
            pcie_c2h_event[i].wait();
        }
    }
    usr_irq_ack = 0x00000000;
    write_device(xdma_user_base,IRQ_ACK_OFFSET, usr_irq_ack);

    pcie_deinit();

    delete ui;
    delete groupBox;
    delete timer;
}


// =================================== Uart Functions ===================================
void MainWindow::on_Connect_clicked()
{
    QString PortsName = ui->Ports->currentText();
    PortsName = PortsName.mid(0,PortsName.indexOf(" "));

    ui->textBrowser->setTextColor(Qt::black);
    Ports.close();
    Ports.setPortName(PortsName);

    if(  !Ports.open(QIODevice::ReadWrite) ){
        if(!Ports.isOpen())
        {
            ui->textBrowser->append("Ports Not Open");
            ui->textBrowser->append(Ports.errorString());
        }
    }
    else
    {
        ui->textBrowser->setTextColor(Qt::black);
        ui->textBrowser->append("Port Connected");

        //Set BaudRate
        QString stringbaudRate = ui->BaudRate->currentText();
        int intbaudRate = stringbaudRate.toInt();
        Ports.setBaudRate(intbaudRate);
        QString BaudStr = "BaudRate = ";
        BaudStr += QString:: number(intbaudRate);
        ui->textBrowser->append(BaudStr);

        //Set DataBits
        Ports.setDataBits(QSerialPort::Data8);
        //Set StopBits
        Ports.setStopBits(QSerialPort::OneStop);

        //Set Parity
        Ports.setParity(QSerialPort::NoParity);

        //Set FlowControl
        Ports.setFlowControl(QSerialPort::NoFlowControl);

        //Attach Received Signal to correspond Slot function
        connect(&Ports,SIGNAL(readyRead()),this,SLOT(Receiver()));

    }
}

void MainWindow::on_Disconnect_clicked()
{
    Ports.close();
    ui->textBrowser->setTextColor(Qt::black);
    ui->textBrowser->append("Port DisConnected");

}

void MainWindow::on_RefreshPorts_clicked()
{
    ui->Ports->clear();

    QList<QSerialPortInfo> ports = info.availablePorts();
    QList<QString> stringPorts;
    for(int i = 0 ; i < ports.size() ; i++){
        stringPorts.append(ports.at(i).portName() + " " + ports.at(i).description());
//        stringPorts.append(ports.at(i).portName());
    }
    ui->Ports->addItems(stringPorts);

}

void MainWindow::on_Clear_clicked()
{
    ui->textBrowser->clear();
}

void MainWindow::on_Send_clicked()
{
    QString message = ui->lineEdit->text();
    ui->textBrowser->setTextColor(Qt::darkGreen); // Color of message to send is green.
    ui->textBrowser->append(message);
 //   qDebug() << message;
    Message = 1;
    Ports.write(message.toUtf8());
    while(Message)
    {
        QCoreApplication::processEvents();
    }
}

void MainWindow::Receiver()
{
    //Process Regular Message respond
    if(Message)
    {
        QByteArray Rec = Ports.readAll();
        while (Ports.waitForReadyRead(500))
        {
            Rec.append(Ports.readAll());
        }
        ui->textBrowser->setTextColor(Qt::blue);        //Respond message is presented in blue
        ui->textBrowser->append("Microblaze Recived:");
        ui->textBrowser->append(QString(Rec.toHex()));
        Rec.clear();
        Message = 0;
        return;
    }
    else if(StartSample)  //Process SPI Request
    {
        QByteArray Rec = Ports.readAll();
        while (Ports.waitForReadyRead(250))
        {
            Rec.append(Ports.readAll());
        }
        qDebug() << QString(Rec);

        if(QString(Rec) == "SPI test done\n")
        {
            ui->textBrowser->setTextColor(Qt::black);        //Respond message is presented in blue
            ui->textBrowser->append("SPI test done");
            Rec.clear();
            StartSample = 0;
            return;
        }
        else
        {
            ui->textBrowser->append(QString(Rec));
            Rec.clear();
            return;
        }
    }
    return;
}

// =================================== LED function ===================================
void MainWindow::on_pB_LightOn_clicked()
{
    QString message = LED_ON;
    ui->textBrowser->setTextColor(Qt::black);

    ui->textBrowser->append("The LED has been turned on.");
    StartSample = 1;
    Ports.write(message.toUtf8());
    while(StartSample)
    {
        QCoreApplication::processEvents();
    }

}

void MainWindow::on_pB_LightOff_clicked()
{
    QString message = LED_OFF;
    ui->textBrowser->setTextColor(Qt::black);

    ui->textBrowser->append("The LED has been turned off.");
    StartSample = 1;
    Ports.write(message.toUtf8());
    while(StartSample)
    {
        QCoreApplication::processEvents();
    }
}


// =================================== PCIe Functions ===================================
// click to init pcie
void MainWindow::on_pB_OpenDevice_clicked()
{
    usr_irq_ack = 0x00000001;
    write_device(xdma_user_base,IRQ_ACK_OFFSET, usr_irq_ack);
    usleep(10);
    usr_irq_ack = 0xffff0000;
    write_device(xdma_user_base,IRQ_ACK_OFFSET, usr_irq_ack);

    ui->textBrowser->setTextColor(Qt::blue);
    ui->textBrowser->append("PCIe IRQ START");
    cout<<"!!!!!PCIe IRQ Closed!!!!!"<<endl;
}

void MainWindow::on_pB_CloseDevice_clicked()
{
    usr_irq_ack = 0x00000000;
    write_device(xdma_user_base,IRQ_ACK_OFFSET, usr_irq_ack);

    ui->textBrowser->setTextColor(Qt::blue);
    ui->textBrowser->append("PCIe IRQ Closed");
    cout<<"!!!!!PCIe IRQ Closed!!!!!"<<endl;
}

// =================================== Sample Mode Definition ===================================
// Mode 1: Automatic sampling with specified collection times
void MainWindow::on_pB_Trigger_clicked()
{
    QString message = TRIGGER;
    ui->textBrowser->setTextColor(Qt::black);
    if (currentTriggerCount == 0)
    {
        ui->textBrowser->append(QString("Mode 1: Automatic sampling Start. "));
    }

    if (currentTriggerCount < predefinedTriggerCount)
    {
        ui->textBrowser->append(QString("Trigger Count: %1/%2")
                                .arg(currentTriggerCount + 1)
                                .arg(predefinedTriggerCount));

        // Send the first TRIGGER command
        Ports.write(message.toUtf8());

        // Use QTimer to delay 1 second before sending the second TRIGGER command
        QTimer::singleShot(1000, this, [this, message]() {
            // Send the second TRIGGER command
            Ports.write(message.toUtf8());

            // First trigger: Enable xdma after a 1-second delay, but do not start CUDA and Python
            if (currentTriggerCount == 0) {
                QTimer::singleShot(1000, this, [this]() {
                    uint32_t usr_irq_ack = 0xffff0000;
                    write_device(xdma_user_base, IRQ_ACK_OFFSET, usr_irq_ack);
                });
            } else {
                // Middle triggers: Start CUDA and Python, then enable xdma
                processAndDisplayData();
            }

            currentTriggerCount++; // Increase the current trigger count

            // If there are remaining trigger counts, continue sending
            QTimer::singleShot(3000, this, &MainWindow::on_pB_Trigger_clicked); // Wait for CUDA

        });
    }
}

// Mode 2: Manually triggering a single sampling
void MainWindow::on_pB_Trigger_once_clicked()
{
    QString message = TRIGGER;
    ui->textBrowser->setTextColor(Qt::black);
    ui->textBrowser->append("Mode 2: Manual sampling start.");

    // Send the first TRIGGER command
    Ports.write(message.toUtf8());

    // Use QTimer to delay 1 second before sending the second TRIGGER command
    QTimer::singleShot(1000, this, [this, message]() {
        // Send the second TRIGGER command
        Ports.write(message.toUtf8());

        // Enable xdma after a 1-second delay, but do not start CUDA and Python
        QTimer::singleShot(1000, this, [this]() {
            uint32_t usr_irq_ack = 0xffff0000;
            write_device(xdma_user_base, IRQ_ACK_OFFSET, usr_irq_ack);
        });
    });

    // Start CUDA and Python processing after sending the command
    processAndDisplayData();
}

// xdma interrupt handler
void MainWindow::itr_process() {
    // 关闭xdma
    uint32_t usr_irq_ack = 0x00000000;
    write_device(xdma_user_base, IRQ_ACK_OFFSET, usr_irq_ack);

    // 获取当前时间
    std::time_t now = std::time(nullptr);
    std::tm* timeinfo = std::localtime(&now);

    // 创建文件路径和名称
    std::stringstream ss;
    ss << "../ResultData/fpga_result_"
       << (timeinfo->tm_year + 1900) << timeinfo->tm_mon + 1 << timeinfo->tm_mday << ".bin";    // year-month-day
    const std::string& tempStr = ss.str();
    const char* result_file_name = tempStr.c_str();

    // 定义模式选择
    int mode = 1; // 1: 覆盖模式(real-time)，2: 追加模式

    // 打开文件，根据模式选择打开方式
    std::ios_base::openmode open_mode = std::ios::out;
    if (mode == 1) {
        open_mode = open_mode | std::ios::trunc; // 覆盖模式
    } else if (mode == 2) {
        open_mode = open_mode | std::ios::app;   // 追加模式
    }

    std::ofstream ofs(result_file_name, open_mode | std::ios::binary);
    if (!ofs.is_open()) {
        std::cout << "Failed to open file for writing: " << strerror(errno) << std::endl;
        return;
    }

    // 定义要读取的数据大小和缓冲区
    const uint32_t read_length = FDMA_BUF_LEN / 2; // 每次读取一半的数据
    unsigned char result_buf[read_length];

    // 第一次读取
    unsigned int fpga_ddr_addr = FPGA_DDR_RESULT_START_ADDR; // FPGA DDR中结果数据的起始地址
    ssize_t bytes_read = read_from_fpga_ddr(xdma_c2h_fd, fpga_ddr_addr, result_buf, read_length);
    if (bytes_read == -1) {
        std::cout << "Error reading from FPGA DDR (first read): " << strerror(errno) << std::endl;
        ofs.close();
        return;
    }
    ofs.write(reinterpret_cast<char*>(result_buf), bytes_read);

    // 第二次读取
    unsigned int fpga_ddr_addr_2 = FPGA_DDR_RESULT_START_ADDR + read_length; // 更新地址
    ssize_t bytes_read_2 = read_from_fpga_ddr(xdma_c2h_fd, fpga_ddr_addr_2, result_buf, read_length);
    if (bytes_read_2 == -1) {
        std::cout << "Error reading from FPGA DDR (second read): " << strerror(errno) << std::endl;
        ofs.close();
        return;
    }
    ofs.write(reinterpret_cast<char*>(result_buf), bytes_read_2);

    ofs.close();
    std::cout << result_file_name << " write successful!!!" << std::endl;

    // Last trigger: Start CUDA and Python, but do not enable xdma
    if (currentTriggerCount == predefinedTriggerCount) {
        processAndDisplayData();
    }
}

// =================================== Real-time Display Functions ===================================
// CUDA处理函数
void MainWindow::processAndDisplayData() {
    QString cmd = "/home/nx/CUDA_RadarImaging/nvvp_workspace/Cuda3DImaging_Project/ArrayImaging_gpu";
    QStringList args;
    args << "-mode=25280" << "-usr1=pig";

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    // 设置环境变量LD_LIBRARY_PATH
    QStringList env = QProcess::systemEnvironment();
    env.append("LD_LIBRARY_PATH=/usr/local/cuda-10.2/lib64:" + env.value(env.size() - 1).section(":", 0, 0));
    m_process->setEnvironment(env);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &MainWindow::onOut);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &MainWindow::onCudaFinished);
    m_process->start(cmd, args);

    if (!m_process->waitForStarted()) {
        qDebug() << "CUDA program start failed:" << m_process->errorString();
    } else {
        qDebug() << "CUDA program started successfully.";
    }
}

void MainWindow::onCudaFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
         qDebug() << "CUDA program finished successfully.";
    } else {
        qDebug() << "CUDA program finished with errors.";
    }
    // 清理CUDA进程资源
    if (m_process) {
        delete m_process;
        m_process = nullptr;
    }
    // 使能xdma
    if (currentTriggerCount > 0 && currentTriggerCount < predefinedTriggerCount - 1) {
        uint32_t usr_irq_ack = 0xffff0000;
        write_device(xdma_user_base, IRQ_ACK_OFFSET, usr_irq_ack);
    }
}

// 处理CUDA程序输出的槽函数
void MainWindow::onOut()
{
    qDebug() << m_process->readAllStandardOutput();
}

// =================================== Graphic Display ===================================
// Draw 3D Imaging
void MainWindow::initializeGraph()
{
    // 创建3D表面图
    groupBox = new Q3DSurface;
    groupBox->axisX()->setTitle("Azimuth [deg]");
    groupBox->axisY()->setTitle("Amplitude");
    groupBox->axisZ()->setTitle("Elevation [deg]");
    groupBox->axisY()->setRange(0, 60000); // 设置Y轴范围
    groupBox->axisX()->setRange(-31, 31); // 设置X轴范围
    groupBox->axisZ()->setRange(-31, 31); // 设置Z轴范围

    // 显示坐标轴刻度和标签
    groupBox->axisX()->setLabelFormat("%i°");
    groupBox->axisZ()->setLabelFormat("%i°");
    groupBox->axisY()->setLabelFormat("%i");

    groupBox->axisX()->setSegmentCount(6); // 设置X轴刻度数量
    groupBox->axisZ()->setSegmentCount(6); // 设置Z轴刻度数量
    groupBox->axisY()->setSegmentCount(5); // 设置Y轴刻度数量

    // 创建数据代理和系列
    QSurfaceDataProxy *proxy = new QSurfaceDataProxy;
    series = new QSurface3DSeries(proxy);
    groupBox->addSeries(series);

    // 设置相机视角为俯视角
    Q3DCamera *camera = groupBox->scene()->activeCamera();
    camera->setCameraPosition(0.0f, 90.0f, 0.0f); // 设置相机位置：俯视角
    camera->setZoomLevel(140);

    // 设置颜色映射（增强对比度）
    Q3DTheme *theme = groupBox->activeTheme();
    theme->setType(Q3DTheme::ThemeQt); // 使用 Qt 默认主题

    // 创建一个线性渐变
    QLinearGradient gradient;
    gradient.setColorAt(0.0, Qt::blue); // 起始颜色
    gradient.setColorAt(0.5, Qt::yellow); // 中间颜色
    gradient.setColorAt(1.0, Qt::red); // 结束颜色

    // 设置渐变到系列
    series->setBaseGradient(gradient);
    series->setColorStyle(Q3DTheme::ColorStyleRangeGradient); // 使用范围渐变

    // Embed the 3D graph into the QWidget in the UI
    QWidget *container = QWidget::createWindowContainer(groupBox, ui->widget);
    QVBoxLayout *vLayout = new QVBoxLayout(ui->widget);
    vLayout->addWidget(container);
    ui->widget->setLayout(vLayout);
}

void MainWindow::generateData()
{
    // 加载 CSV 文件数据
    QString filePath = "/home/nx/CUDA_RadarImaging/nvvp_workspace/Cuda3DImaging_Project/ImagingResult_TwoTargets.csv";
    loadDataFromCSV(filePath);
}

void MainWindow::loadDataFromCSV(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "无法打开文件：" << filePath;
        return;
    }

    QTextStream in(&file);
    QStringList lines = in.readAll().split('\n');

    // 定义网格范围
    float azi_left = -30;
    float azi_right = 30;
    float pit_left = -30;
    float pit_right = 30;

    int Nay_net = round((azi_right - azi_left) * 2); // 方位向的网格数
    int Naz_net = round((pit_right - pit_left) * 2); // 俯仰向的网格数

    dataArray = new QSurfaceDataArray;
    dataArray->reserve(Nay_net);

    // 遍历网格，填充数据
    for (int i = 0; i < Nay_net; i++)
    {
        QSurfaceDataRow *newRow = new QSurfaceDataRow(Naz_net);
        for (int j = 0; j < Naz_net; j++)
        {
            // 计算当前网格点的坐标
            float azi = azi_left + (azi_right - azi_left) / (Nay_net - 1) * i;
            float pit = pit_left + (pit_right - pit_left) / (Naz_net - 1) * j;

            // 从 CSV 文件中获取对应的幅度值
            float amplitude = 0; // 默认值
            if (i < lines.size() && j < lines[i].split(',').size()) {
                QStringList row = lines[i].split(',');
                amplitude = row[j].toFloat();
            }

            // 设置数据点
            (*newRow)[j].setPosition(QVector3D(azi, amplitude, pit));
//            qDebug() << "行：" << azi << "列：" << pit << "幅度值：" << amplitude;
        }
        *dataArray << newRow;
    }

    series->dataProxy()->resetArray(dataArray);
}

void MainWindow::updateGraph()
{
    generateData();  // Reload or regenerate the data
}
