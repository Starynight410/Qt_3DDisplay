#ifndef PCIe_FUN_H
#define PCIe_FUN_H

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <iostream>
#include <cstring>
#include <string>

#define MAP_SIZE            (1024*1024UL)       // 1MB
#define IRQ_ACK_OFFSET      0x00004             // 定义中断清除寄存器的偏移地址
#define XDMA_USER_DEVICE    "/dev/xdma0_user"
#define XDMA_C2H_DEVICE     "/dev/xdma0_c2h_0"
#define XDMA_H2C_DEVICE     "/dev/xdma0_h2c_0"
#define FPGA_DDR_RESULT_START_ADDR 0x00A00000
#define FDMA_BUF_LEN        1048576*8           // QSPIx8

// Global variables
extern int xdma_user_fd;
extern int xdma_c2h_fd;
extern int xdma_h2c_fd;
extern void* xdma_user_base;
extern uint32_t* base_address;

// Function declarations
int pcie_init();
void pcie_deinit();
int open_control(const char* filename);
void* mmap_control(int fd, size_t mapsize);
void write_device(void* base_address, uint32_t offset, uint32_t val);
uint32_t read_device(void* base_address, uint32_t offset);
int trs(char ch);
uint32_t str2uint32(const std::string& str);
uint8_t str2uint8(const std::string& str);
ssize_t read_from_fpga_ddr(int xdma_c2h_fd, unsigned int fpga_ddr_addr, unsigned char* buffer, size_t size); // 从FPGA DDR读取数据
ssize_t write_to_fpga_ddr(int h2c_fd, unsigned int fpga_ddr_addr, uint8_t* buffer, size_t size); // 向FPGA DDR写入数据

int event0_process();
int event1_process();
int event2_process();
int event3_process();
int event4_process();
int event5_process();
int event6_process();
int event7_process();

#endif // PCIe_FUN_H
