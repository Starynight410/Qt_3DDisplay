#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QDebug>
#include <QFileDialog>
#include <QTime>
#include <QMessageBox>
#include <QThread>
#include <QTimer>
#include <vector>
#include <atomic>
#include <unistd.h>
#include <QProcess>

#include <QVector3D>
#include <QFile>
#include <QTextStream>
#include <QVector>
#include <QtWidgets>
#include <QtDataVisualization>
#include <QSurfaceDataProxy>
#include <QSurface3DSeries>
#include <Q3DSurface>
#include <QWidget>

using namespace QtDataVisualization;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class event_thread : public QThread
{
    Q_OBJECT
public :
    volatile bool isplay;
    volatile char event_id;
    event_thread();
    ~event_thread();
    void run();

signals :
    void sig_inform_disp();
};


class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    event_thread pcie_c2h_event[8]; // Array of event threads

    void processAndDisplayData();

private slots:
    void itr_process();

    void on_Connect_clicked();
    void on_Disconnect_clicked();
    void on_RefreshPorts_clicked();
    void on_Clear_clicked();
    void on_Send_clicked();
    void Receiver();

    void on_pB_LightOn_clicked();
    void on_pB_LightOff_clicked();
    void on_pB_Trigger_clicked();
    void on_pB_Trigger_once_clicked();
    void on_pB_OpenDevice_clicked();
    void on_pB_CloseDevice_clicked();

    void onOut();
    void onCudaFinished(int, QProcess::ExitStatus);

    void updateGraph();  // 定时更新图形的槽函数

private:
    Ui::MainWindow *ui;
    QSerialPort Ports;
    QSerialPortInfo info;
    QFile DataFile;

    uint8_t Message = 0;
    uint8_t StartSample = 0;
    uint32_t DataLen = 0;

    int flag[8];    // record sample completed
    unsigned int usr_irq_ack;
    void* userBase;
    int xdma_user_fd;

    std::vector<QThread*> eventThreads;
    std::atomic<bool> running;

    int predefinedTriggerCount; // 预定义的触发次数
    int currentTriggerCount;    // 当前触发次数

    QProcess *m_process;        // 定义m_process为QProcess类型的指针

    int mode; // 用于存储模式选择

    Q3DSurface *groupBox;
    QSurface3DSeries *series;
    QSurfaceDataArray *dataArray;
    QTimer *timer;  // 定时器

    void initializeGraph();
    void generateData();
    void loadDataFromCSV(const QString &filePath);

};

#endif // MAINWINDOW_H
