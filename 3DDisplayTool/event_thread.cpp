#include "pcie_fun.h"
#include "mainwindow.h"
#include <QDebug>

event_thread::event_thread()
{

}
event_thread::~event_thread()
{
    // close event
    //close_event(pcie_event_fd);
}

void event_thread::run()
{

    while(this->isplay == true)
    {

        if(this->event_id==0){
            if (event0_process() == 1)
            {
                emit sig_inform_disp();
                qDebug()<<"中断0触发"<<endl;

            }


        }
        else if(this->event_id==1){
            if (event1_process() == 1)
            {
                emit sig_inform_disp();
                qDebug()<<"中断1触发"<<endl;
               // qDebug()<<inter_transfer()<<endl;

               // qDebug()<<wait_for_event1()<<endl;
            }
        }
        else if(this->event_id==2){
            if (event2_process() == 1)
            {
                emit sig_inform_disp();
                qDebug()<<"中断2触发"<<endl;

             //qDebug()<<c2h_axi4_addr[2]<<endl;
            }
        }
        else if(this->event_id==3){
            if (event3_process() == 1)
            {
                emit sig_inform_disp();
                qDebug()<<"中断3触发"<<endl;

             //   qDebug()<<wait_for_event1()<<endl;
            }
        }
        else if(this->event_id==4){
            if (event4_process() == 1)
            {
                emit sig_inform_disp();
                qDebug()<<"中断4触发"<<endl;

             //   qDebug()<<wait_for_event1()<<endl;
            }
        }
        else if(this->event_id==5){
            if (event5_process() == 1)
            {
                emit sig_inform_disp();
                qDebug()<<"中断5触发"<<endl;

             //   qDebug()<<wait_for_event1()<<endl;
            }
        }
        else if(this->event_id==6){
            if (event6_process() == 1)
            {
                emit sig_inform_disp();
                qDebug()<<"中断6触发"<<endl;

             //   qDebug()<<wait_for_event1()<<endl;
            }
        }
        else if(this->event_id==7){
            if (event7_process() == 1)
            {
                emit sig_inform_disp();
                qDebug()<<"中断7触发"<<endl;

             //   qDebug()<<wait_for_event1()<<endl;
            }
        }
    }

}
