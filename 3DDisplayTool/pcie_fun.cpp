#include "pcie_fun.h"
#include <thread>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include "byte_order_functions.h"

using namespace std;

int xdma_user_fd;
int xdma_c2h_fd;
int xdma_h2c_fd;
void* xdma_user_base;
uint32_t* base_address;

int pcie_init() {
    // 打开用户设备
    xdma_user_fd = open(XDMA_USER_DEVICE, O_RDWR);
    if (xdma_user_fd == -1) {
        std::cerr << "Error opening " << XDMA_USER_DEVICE << ": " << strerror(errno) << std::endl;
        return -1;
    }

    // 内存映射用户设备
    xdma_user_base = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, xdma_user_fd, 0);
    if (xdma_user_base == MAP_FAILED) {
        std::cerr << "Memory mapping failed: " << strerror(errno) << std::endl;
        close(xdma_user_fd);
        return -1;
    }
    std::cout << "Memory mapped at address: " << xdma_user_base << std::endl;

    // 打开C2H设备
    xdma_c2h_fd = open(XDMA_C2H_DEVICE, O_RDONLY);
    if (xdma_c2h_fd == -1) {
        std::cerr << "Error opening " << XDMA_C2H_DEVICE << ": " << strerror(errno) << std::endl;
        munmap(xdma_user_base, MAP_SIZE);
        close(xdma_user_fd);
        return -1;
    }

    // 打开H2C设备
    xdma_h2c_fd = open(XDMA_H2C_DEVICE, O_RDONLY);
    if (xdma_h2c_fd == -1) {
        std::cerr << "Error opening " << XDMA_H2C_DEVICE << ": " << strerror(errno) << std::endl;
        munmap(xdma_user_base, MAP_SIZE);
        close(xdma_user_fd);
        return -1;
    }

    // 计算基地址
    base_address = reinterpret_cast<uint32_t*>(static_cast<char*>(xdma_user_base) + 0x20000);   //0x44A20000

    // 初始化并清除中断
    write_device(base_address, IRQ_ACK_OFFSET, 0x00000000); // 清除中断
//    std::this_thread::sleep_for(std::chrono::microseconds(10));
//    write_device(base_address, IRQ_ACK_OFFSET, 0xffff0000); // 设置中断掩码

    return 1; // 成功
}

void pcie_deinit() {
    if (base_address) {
        // No need to unmap base_address, as it's a pointer to xdma_user_base
    }
    if (xdma_user_base) {
        munmap(xdma_user_base, MAP_SIZE);
    }
    if (xdma_user_fd != -1) {
        close(xdma_user_fd);
    }
    if (xdma_c2h_fd != -1) {
        close(xdma_c2h_fd);
    }
    if (xdma_h2c_fd != -1) {
        close(xdma_h2c_fd);
    }
}

int open_control(const char* filename) {
    int fd = open(filename, O_RDWR | O_SYNC);
    if (fd == -1) {
        perror("open");
        return -1;
    }
    return fd;
}

void* mmap_control(int fd, size_t mapsize) {
    void* vir_addr = mmap(NULL, mapsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (vir_addr == MAP_FAILED) {
        perror("mmap");
        return nullptr;
    }
    return vir_addr;
}

void write_device(void* base_address, uint32_t offset, uint32_t val) {
    uint32_t write_val = htoll(val);
    *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(base_address) + offset) = write_val;
}

uint32_t read_device(void* base_address, uint32_t offset) {
    uint32_t read_result = *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(base_address) + offset);
    return ltohl(read_result);
}

int trs(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    } else {
        return ch - 'a' + 10;
    }
}

uint32_t str2uint32(const string& str) {
    uint32_t dec = 0;
    for (char ch : str) {
        dec = dec * 16 + trs(ch);
    }
    return dec;
}

uint8_t str2uint8(const string& str) {
    uint8_t dec = 0;
    for (char ch : str) {
        dec = dec * 16 + trs(ch);
    }
    return dec;
}

// 从FPGA DDR读取数据
ssize_t read_from_fpga_ddr(int xdma_c2h_fd, unsigned int fpga_ddr_addr, unsigned char* buffer, size_t size) {
    // 定位到FPGA DDR的指定地址
    if (lseek(xdma_c2h_fd, fpga_ddr_addr, SEEK_SET) == -1) {
        std::perror("lseek error");
        return -1;
    }
    // 读取数据
    ssize_t bytes_read = read(xdma_c2h_fd, buffer, size);
    if (bytes_read == -1) {
        std::perror("Error reading from FPGA DDR");
        return -1;
    }
    return bytes_read;
}

// 向FPGA DDR写入数据
ssize_t write_to_fpga_ddr(int xdma_h2c_fd, unsigned int fpga_ddr_addr, uint8_t* buffer, size_t size) {
    // 定位到FPGA DDR的指定地址
    if (lseek(xdma_h2c_fd, fpga_ddr_addr, SEEK_SET) == -1) {
        std::perror("lseek error");
        return -1;
    }
    // 写入数据
    ssize_t bytes_written = write(xdma_h2c_fd, buffer, size);
    if (bytes_written == -1) {
        std::perror("Error writing to FPGA DDR");
        return -1;
    }
    return bytes_written;
}

// PCIe events
int event0_process() {
    int val;
    int event_fd = open("/dev/xdma0_events_0", O_RDWR | O_SYNC);
    if(event_fd < 0) {
        cerr << "open event0 error" << endl;
        return 0;
    } else {
        if(read(event_fd, &val, sizeof(val)) == sizeof(val)) {
            if(val == 1) {
                cout << "event_0 done!" << endl;
                close(event_fd);
                return 1;
            }
        }
        close(event_fd);
    }
    return 0;
}
int event1_process() {
    int val;
    int event_fd = open("/dev/xdma0_events_1", O_RDWR | O_SYNC);
    if(event_fd < 0) {
        cerr << "open event1 error" << endl;
        return 0;
    } else {
        if(read(event_fd, &val, sizeof(val)) == sizeof(val)) {
            if(val == 1) {
                cout << "event_1 done!" << endl;
                close(event_fd);
                return 1;
            }
        }
        close(event_fd);
    }
    return 0;
}

int event2_process() {
    int val;
    int event_fd = open("/dev/xdma0_events_2", O_RDWR | O_SYNC);
    if(event_fd < 0) {
        cerr << "open event2 error" << endl;
        return 0;
    } else {
        if(read(event_fd, &val, sizeof(val)) == sizeof(val)) {
            if(val == 1) {
                cout << "event_2 done!" << endl;
                close(event_fd);
                return 1;
            }
        }
        close(event_fd);
    }
    return 0;
}

int event3_process() {
    int val;
    int event_fd = open("/dev/xdma0_events_3", O_RDWR | O_SYNC);
    if(event_fd < 0) {
        cerr << "open event3 error" << endl;
        return 0;
    } else {
        if(read(event_fd, &val, sizeof(val)) == sizeof(val)) {
            if(val == 1) {
                cout << "event_3 done!" << endl;
                close(event_fd);
                return 1;
            }
        }
        close(event_fd);
    }
    return 0;
}

int event4_process() {
    int val;
    int event_fd = open("/dev/xdma0_events_4", O_RDWR | O_SYNC);
    if(event_fd < 0) {
        cerr << "open event4 error" << endl;
        return 0;
    } else {
        if(read(event_fd, &val, sizeof(val)) == sizeof(val)) {
            if(val == 1) {
                cout << "event_4 done!" << endl;
                close(event_fd);
                return 1;
            }
        }
        close(event_fd);
    }
    return 0;
}

int event5_process() {
    int val;
    int event_fd = open("/dev/xdma0_events_5", O_RDWR | O_SYNC);
    if(event_fd < 0) {
        cerr << "open event5 error" << endl;
        return 0;
    } else {
        if(read(event_fd, &val, sizeof(val)) == sizeof(val)) {
            if(val == 1) {
                cout << "event_5 done!" << endl;
                close(event_fd);
                return 1;
            }
        }
        close(event_fd);
    }
    return 0;
}

int event6_process() {
    int val;
    int event_fd = open("/dev/xdma0_events_6", O_RDWR | O_SYNC);
    if(event_fd < 0) {
        cerr << "open event6 error" << endl;
        return 0;
    } else {
        if(read(event_fd, &val, sizeof(val)) == sizeof(val)) {
            if(val == 1) {
                cout << "event_6 done!" << endl;
                close(event_fd);
                return 1;
            }
        }
        close(event_fd);
    }
    return 0;
}

int event7_process() {
    int val;
    int event_fd = open("/dev/xdma0_events_7", O_RDWR | O_SYNC);
    if(event_fd < 0) {
        cerr << "open event7 error" << endl;
        return 0;
    } else {
        if(read(event_fd, &val, sizeof(val)) == sizeof(val)) {
            if(val == 1) {
                cout << "event_7 done!" << endl;
                close(event_fd);
                return 1;
            }
        }
        close(event_fd);
    }
    return 0;
}
