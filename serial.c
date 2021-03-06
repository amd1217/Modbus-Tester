#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include "serial.h"
#include "crc_checksum.h"
#include "modbus.h"


enum REMOTE_CONTROL_CMD{
    PRE_CONTROL_CMD = 1,
    ACT_CONTROL_CMD = 0,
};

enum PARITY_TYPE{
    NO_PARITY = 'n',
    ODD_PARITY = 'o',
    EVEN_PARITY = 'e',
    SPACE_PARITY = 's',
};

// Debug Macro
// #define CRC_DEBUG

#define APP_TCP
//#define APP_UDP
#define DEVICE_PORT 1024
#define DEVICE_SERVER "192.168.1.126"
//#define RS485_INF
#define ETHER_INF

#define RAW_LINE_INPUT
#define BUF_SIZE 200
char buffer[BUF_SIZE];

#define MS_DELAY(x) (x*1000)
#define DEVICE_ID 126
#define DEBUG

void read_telemetering(int fd);
void read_teleindication(int fd);
int recv_data(int fd, char* out);
int send_data(int fd, char *buf, unsigned int size);
void send_soe(int fd);
void change_vendor(int fd, int type);
void print_senddata(char *buf, unsigned int len);
void set_serial_options(int, int);
void telecontrol(int fd, unsigned int flag);
unsigned short crc16(unsigned short crc, unsigned char const *buffer, size_t len);
void usage_info();
void read_config_value(int dev_fd);
void read_protect_value(int dev_fd);
void send_manual_data(int fd, char *buf, unsigned int size);
void send_timetick(int fd);

int device_id = 0;
int main(int argc, char *argv[])
{
    char *serial_dev = NULL;
    char *device_ip = NULL;
    char *tmp = NULL;
    unsigned int register_addr;
    unsigned int register_cnt;
    unsigned int comm_port = 0;
    int serial_fd, socket_isenable = FALSE, serial_isenable = FALSE;
    int i;
    unsigned int crc_checksum;
    int arg;
    int soe_flag = FALSE;
    int vendor_flag = FALSE;
    int rconfig_flag = FALSE;
    int yx_flag = FALSE;
    int yk_flag = FALSE;
    int tcp_flag = FALSE;
    int udp_flag = FALSE;
    int loop_mode = FALSE;
    int timetick_flag = FALSE;
    int telemetering_flag = FALSE;
    struct sockaddr_in device_addr;
    int sockfd = 0;
    int device_fd = 0;
    char m_data[20] = {0};
    int manual_flag = FALSE;

    if(argc < 2){
        usage_info();
        exit(ERR_WRONG_ARG);
    }
    
    opterr = 0;

    while((arg = getopt(argc, argv, "a:i:l:p:d:z:svrytuhkmcx")) != -1){
        switch(arg){
            case 'a':
                tmp = optarg;
                strcpy(m_data, tmp);
                manual_flag = TRUE;
                //register_addr = atoi(tmp);
                printf("manual data=%s\n", m_data);
                break;
            case 'l':
                loop_mode = TRUE;
                break;
            case 'i':
                serial_dev = optarg;
                serial_isenable = TRUE;
                break;
            case 'd':
                device_ip = optarg;
                socket_isenable = TRUE;
                break;
            case 's':
                soe_flag = TRUE;
                break;
            case 'v':
                vendor_flag = TRUE;
                break;
            case 'r':
                rconfig_flag = TRUE;
                break;
            case 'y':
                yx_flag = TRUE;
                break;
            case 'c':
                telemetering_flag = TRUE;
                break;
            case 't':
                tcp_flag = TRUE;
                break;
            case 'u':
                udp_flag = TRUE;
                break;
            case 'k':
                yk_flag = TRUE;
                break;
            case 'p':
                tmp = optarg;
                comm_port = atoi(tmp);
                break;
            case 'z':
                tmp = optarg;
                device_id = atoi(tmp);
                break;
            case 'x':
                timetick_flag = TRUE;
                break;
            case 'h':
                usage_info();
                goto FIN;
                break;
            default:
                abort();
        }
    }

    if(serial_isenable && socket_isenable){
        printf("You could choose communicate device from Serial port or Network interface. Don't use both!!!\n");
        exit(ERR_WRONG_DEV);
    }

    if(socket_isenable && 0 == comm_port){
        printf("If you want to use socket interface, you must give me a port\n");
        exit(ERR_WRONG_PORT);

    }
        
    if(manual_flag == FALSE && 0 == device_id){
        printf("You must give a device id.It will used by first byte of packet.\n");
        exit(ERR_WRONG_DEVICEID);
    }

    
    if(socket_isenable){

        printf("Network IP:%s, Port:%d\n", device_ip, comm_port);
        if(tcp_flag){
            sockfd = socket(AF_INET, SOCK_STREAM, 0);
        }

        if(udp_flag){
            sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        }

        if(sockfd <= 0){
            perror("creat socket failed\n");
            exit(ERR_WRONG_SOCK);
        }

        memset(&device_addr, 0, sizeof(device_addr));
        device_addr.sin_family = AF_INET;
        device_addr.sin_port = htons(comm_port);
        device_addr.sin_addr.s_addr = inet_addr(device_ip);
        device_fd = sockfd;

        if(tcp_flag){
            if(connect(device_fd, (struct sockaddr*)&device_addr, sizeof(device_addr)) < 0){
                printf("can not connect\n");
                exit(ERR_SOCK_CONCT);
            }
        }
    }
    
    if(serial_isenable){
        printf("Serial Port is %s\n", serial_dev);
        serial_fd = open(serial_dev, O_RDWR | O_NOCTTY | O_NDELAY | O_NONBLOCK);
        if(serial_fd < 0){
            perror("Could not open serial prot\n");
            exit(ERR_SERIAL_DEV);
        }
        set_serial_options(serial_fd, EVEN_PARITY);
        device_fd = serial_fd;
    }

    do{
        if(manual_flag){
            int j = 0, i = 0;
            char *p = NULL;
            char data[50] = {0};
            int data_sz;
            
            p = strtok(m_data, ".");
            do{
                //printf("%s\n", p);
                data[i++] = atoi(p);
            }while(p = strtok(NULL, "."));

            data_sz = i;
            for(j = 0; j < data_sz; j++)
                printf("%d\t", data[j]);
            printf("\n");

            send_manual_data(device_fd, data, data_sz);
            recv_data(device_fd, data);
        }

    if(yx_flag){
        read_teleindication(device_fd);
        usleep(MS_DELAY(90));
        recv_data(device_fd, buffer);
    }
 
    if(soe_flag){
        send_soe(device_fd);
        usleep(MS_DELAY(90));
        recv_data(device_fd, buffer);
    }
   
    if(telemetering_flag){
        read_telemetering(device_fd);
        usleep(MS_DELAY(90));
        recv_data(device_fd, buffer);
    }


    if(yk_flag){
        telecontrol(device_fd, PRE_CONTROL_CMD);
        usleep(MS_DELAY(90));
        recv_data(device_fd, buffer);
        usleep(MS_DELAY(90));
        telecontrol(device_fd, ACT_CONTROL_CMD);
        usleep(MS_DELAY(90));
        recv_data(device_fd, buffer);
    }

    if(vendor_flag){
        change_vendor(device_fd, 1);
        usleep(MS_DELAY(90));
        recv_data(device_fd, buffer);
        change_vendor(device_fd, 2);
        usleep(MS_DELAY(90));
        recv_data(device_fd, buffer);
    }

    if(rconfig_flag){
        read_protect_value(device_fd);
        //read_config_value(device_fd);
    }
    
        if(timetick_flag){
            send_timetick(device_fd);
            usleep(MS_DELAY(90));
        }


        usleep(MS_DELAY(100));
    }while(loop_mode);

    if(device_fd)
        close(device_fd);

FIN:
    return 0;
}

void send_manual_data(int fd, char *buf, unsigned int size)
{
    unsigned int crc_checksum; 

    crc_checksum = crc16(0xffff, buf, size);
    //printf("%x\n", crc_checksum);
    buf[size++] = (char)(crc_checksum & 0xff);
    buf[size++] = (char)((crc_checksum >> 8) & 0xff);
    
    send_data(fd, buf, size);
}

void read_config_value(int dev_fd)
{
        unsigned int config_cnt;
        float config_value[30];
        int i, j, value_tmp;
        
        memset(buffer, 0, BUF_SIZE);
        read_config(dev_fd, 0x40c0, 10, buffer);
        sleep(2);
        config_cnt = recv_data(dev_fd, buffer);
        printf("Recv %d bytes, it has %d config item\n", config_cnt, config_cnt/2);

        for(i = 0, j = 0; i < config_cnt; j++){
            value_tmp = (buffer[i] << 8) | (buffer[i + 1] & 0xff);
            config_value[j] = value_tmp / 100;
            //printf("item%d = 0x%x\n", j, config_value[j]);

            i += 2;
        }

#if 0
        for(i = 0; i < config_cnt / 2; i++){
            printf("%d = %0.2f\n", i, config_value[i]);
        }
#else
        printf("SD_I = %0.2fA\n", config_value[0]);
        printf("SD_T = %0.2fS\n", config_value[1]);
        printf("XSSD_I = %0.2fA\n", config_value[2]);
        printf("XSSD_T = %0.2fS\n", config_value[3]);
        printf("GL_I = %0.2fA\n", config_value[4]);
        printf("GL_T = %0.2fS\n", config_value[5]);
        printf("Z_I = %0.2fA\n", config_value[6]);
        printf("Z_T = %0.2fS\n", config_value[7]);
        printf("FSX_I = %0.2fA\n", config_value[8]);
        printf("FSX_T = %0.2fS\n", config_value[9]);
        printf("FSX_Curve = %0.2f\n", config_value[10]);
        printf("GFH_I = %0.2fA\n", config_value[11]);
        printf("GFH_T = %0.2fS\n", config_value[12]);
        printf("SD1_I = %0.2fA\n", config_value[13]);
        printf("SD1_T = %0.2fS\n", config_value[14]);
        printf("XSSD1_I = %0.2fA\n", config_value[15]);
        printf("XSSD1_T = %0.2fS\n", config_value[16]);
        printf("GL1_I = %0.2fA\n", config_value[17]);
        printf("GL1_T = %0.2fS\n", config_value[18]);
#endif

}

void read_protect_value(int dev_fd)
{
        unsigned int config_cnt;
        float config_value[30];
        int i, j, value_tmp;

        memset(buffer, 0, BUF_SIZE);
        read_config(dev_fd, 0x4080, 26, buffer);
        sleep(2);
        config_cnt = recv_data(dev_fd, buffer);
        printf("Recv %d bytes, it has %d config item\n", config_cnt, config_cnt/2);

        for(i = 0, j = 0; i < config_cnt; j++){
            value_tmp = (buffer[i] << 8) | (buffer[i + 1] & 0xff);
            config_value[j] = value_tmp / 100;
            //printf("item%d = 0x%x\n", j, config_value[j]);

            i += 2;
        }

#if 0
        for(i = 0; i < config_cnt / 2; i++){
            printf("%d = %0.2f\n", i, config_value[i]);
        }
        printf("SD_I = %0.2fA\n", config_value[0]);
        printf("SD_T = %0.2fS\n", config_value[1]);
        printf("XSSD_I = %0.2fA\n", config_value[2]);
        printf("XSSD_T = %0.2fS\n", config_value[3]);
        printf("GL_I = %0.2fA\n", config_value[4]);
        printf("GL_T = %0.2fS\n", config_value[5]);
        printf("Z_I = %0.2fA\n", config_value[6]);
        printf("Z_T = %0.2fS\n", config_value[7]);
        printf("FSX_I = %0.2fA\n", config_value[8]);
        printf("FSX_T = %0.2fS\n", config_value[9]);
        printf("FSX_Curve = %0.2f\n", config_value[10]);
        printf("GFH_I = %0.2fA\n", config_value[11]);
        printf("GFH_T = %0.2fS\n", config_value[12]);
        printf("SD1_I = %0.2fA\n", config_value[13]);
        printf("SD1_T = %0.2fS\n", config_value[14]);
        printf("XSSD1_I = %0.2fA\n", config_value[15]);
        printf("XSSD1_T = %0.2fS\n", config_value[16]);
        printf("GL1_I = %0.2fA\n", config_value[17]);
        printf("GL1_T = %0.2fS\n", config_value[18]);
#endif

}

void set_serial_options(int fd, int parity)
{
    struct termios serial_opts;

    printf("Config the serial port\n");
    
    tcflush(fd, TCIOFLUSH);
    tcgetattr(fd, &serial_opts);

    bzero(&serial_opts, sizeof(serial_opts));

    cfsetispeed(&serial_opts, B4800);
    cfsetospeed(&serial_opts, B4800);

    serial_opts.c_cflag |= (CLOCAL | CREAD);
    serial_opts.c_cflag &= ~CSIZE;
    serial_opts.c_cflag |= (CS8);
    
    // One stop bit 
    serial_opts.c_cflag &= ~CSTOPB;

    switch(parity){
        case 'n':
        case 'N':
            // no parity
            serial_opts.c_cflag &= ~PARENB;
            serial_opts.c_iflag &= ~INPCK;
            break;
        case 'o':
        case 'O':
            // odd parity
            serial_opts.c_cflag |= PARENB;
            serial_opts.c_cflag |= PARODD;
            serial_opts.c_iflag |= INPCK;
            break;
        case 'e':
        case 'E':
            // even parity
            serial_opts.c_cflag |= PARENB;
            serial_opts.c_cflag &= ~PARODD;
            serial_opts.c_iflag |= INPCK;
            break;
        case 's':
        case 'S':
            // space parity
            serial_opts.c_cflag &= ~PARENB;
            serial_opts.c_cflag &= ~CSTOPB;
            serial_opts.c_iflag |= INPCK;
            break;
        default:
            fprintf(stderr, "you must setting parity\n");
    }

    serial_opts.c_cc[VTIME] = 150;
    serial_opts.c_cc[VMIN] = 0;

#ifdef RAW_LINE_INPUT 
    // RAW input
    serial_opts.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
#else
    // Classic input
    serial_opts.c_lflag |= (ICANON | ECHO | ECHOE | ISIG);
#endif
    
    // disable software follow control
    serial_opts.c_iflag &= ~(IXON | IXOFF | IXANY);

    // Output RAW type
    //serial_opts.c_oflag &= ~OPOST;
    
    // disable parity checking
    serial_opts.c_iflag |= INPCK;

    tcsetattr(fd, TCSANOW, &serial_opts);

    sleep(2);
}

void send_soe(int fd)
{
    unsigned int crc_checksum;
    
    memset(buffer, 0, BUF_SIZE);
    
    buffer[0] = device_id;
    buffer[1] = 0x04;
    buffer[2] = 0x41;
    buffer[3] = 0x20;
    buffer[4] = 0x00;
    buffer[5] = 0x08;

    crc_checksum = crc16(0xffff, buffer, 6);
    buffer[6] = (char)(crc_checksum & 0xff);
    buffer[7] = (char)((crc_checksum >> 8) & 0xff);
   
    //print_senddata(8);
    //write(fd, buffer, 8);
    send_data(fd, buffer, 8);
 
}

void send_timetick(int fd)
{
    unsigned int crc_checksum;
    
    memset(buffer, 0, BUF_SIZE);
    
    buffer[0] = device_id;
    buffer[1] = 0x10;
    buffer[2] = 0x43;
    buffer[3] = 0x00;
    buffer[4] = 0x00;
    buffer[5] = 0x06;

    buffer[6] = 0x0e;
    buffer[7] = 0x08;
    buffer[8] = 0x08;
    buffer[9] = 0x08;
    buffer[10] = 0x14;
    buffer[11] = 0x32;

    crc_checksum = crc16(0xffff, buffer, 12);
    buffer[12] = (char)(crc_checksum & 0xff);
    buffer[13] = (char)((crc_checksum >> 8) & 0xff);
   
    //print_senddata(8);
    //write(fd, buffer, 8);
    send_data(fd, buffer, 14);
 
}

void read_teleindication(int fd)
{
    unsigned int crc_checksum;
    
    memset(buffer, 0, BUF_SIZE);
    
    buffer[0] = device_id;
    buffer[1] = 0x03;
    buffer[2] = 0x40;
    buffer[3] = 0x00;
    buffer[4] = 0x00;
    buffer[5] = 0x03;
    
    crc_checksum = crc16(0xffff, buffer, 6);
    buffer[6] = (char)(crc_checksum & 0xff);
    buffer[7] = (char)((crc_checksum >> 8) & 0xff);
    
    //print_senddata(8);
    //write(fd, buffer, 8);
    send_data(fd, buffer, 8);
 
}

void read_telemetering(int fd)
{
    unsigned int crc_checksum;
    
    memset(buffer, 0, BUF_SIZE);
    
    buffer[0] = DEVICE_ID;
    buffer[1] = 0x03;
    buffer[2] = 0x40;
    buffer[3] = 0x1f;
    buffer[4] = 0x00;
    buffer[5] = 0x01;
    
    crc_checksum = crc16(0xffff, buffer, 6);
    buffer[6] = (char)(crc_checksum & 0xff);
    buffer[7] = (char)((crc_checksum >> 8) & 0xff);
    
    //print_senddata(8);
    //write(fd, buffer, 8);
    send_data(fd, buffer, 8);
 
}

/* --------------------------------*/
/**
 * @Synopsis : send remote control packet to device
 *
 * @Param fd
 * @Param flag : if flag is lager then zero, it means send pre-control packet.
 * if flag is equal zero, it means send act-control packet.
 */
/* --------------------------------*/
void telecontrol(int fd, unsigned int flag)
{
    unsigned int crc_checksum;
    
    memset(buffer, 0, BUF_SIZE);
    
    buffer[0] = device_id;
    buffer[1] = 0x05;
    if(flag)
        buffer[2] = 0x51;
    else
        buffer[2] = 0x50;
    buffer[3] = 0x0c;
    buffer[4] = 0xa5;
    buffer[5] = 0x00;
    
    crc_checksum = crc16(0xffff, buffer, 6);
    buffer[6] = (char)(crc_checksum & 0xff);
    buffer[7] = (char)((crc_checksum >> 8) & 0xff);
    
    //print_senddata(8);
    //write(fd, buffer, 8);
    send_data(fd, buffer, 8);
 
}

void change_vendor(int fd, int type)
{
    unsigned int crc_checksum;
    
    memset(buffer, 0, BUF_SIZE);
    
    buffer[0] = device_id;
    buffer[1] = 0x10;
    if(type == 1)
        buffer[2] = 0x42;
    else if(type == 2)
        buffer[2] = 0x40;
    buffer[3] = 0x78;
    buffer[4] = 0x00;
    buffer[5] = 0x01;
    buffer[6] = 0x02;
    buffer[7] = 0x00;
    buffer[8] = VENDOR_KT3310;
    
    crc_checksum = crc16(0xffff, buffer, 9);
    buffer[9] = (char)(crc_checksum & 0xff);
    buffer[10] = (char)((crc_checksum >> 8) & 0xff);
    
    //print_senddata(11);
    //write(fd, buffer, 11);
    send_data(fd, buffer, 11);
 
}

int send_data(int fd, char *buf, unsigned int size)
{
    time_t crt_time;
    struct tm *time_p;
    struct timeval tv;

    write(fd, buf, size);

    time(&crt_time);
    time_p = gmtime(&crt_time);
    gettimeofday(&tv, NULL);
    printf("%d-%02d-%d %02d:%02d:%02d:%03d", time_p->tm_year + 1900, time_p->tm_mon+1, 
                time_p->tm_mday, time_p->tm_hour+8, time_p->tm_min, time_p->tm_sec, tv.tv_usec/1000);
    print_senddata(buf, size);
}

int recv_data(int fd, char* out)
{
    int read_sz, i;
    unsigned int crc_checksum;
    time_t crt_time;
    struct tm *time_p;
    struct timeval tv;

    memset(buffer, 0, BUF_SIZE);
    
    read_sz = read(fd, buffer, BUF_SIZE);
    if(read_sz > 0){
        crc_checksum = crc16(0xffff, buffer, read_sz - 2);
        if(((crc_checksum & 0xff) != (buffer[read_sz - 2] & 0xff)) || ((crc_checksum >> 8 & 0xff) != (buffer[read_sz - 1] & 0xff))){
            printf("The packet is invalid\n");
            printf("Recv Origin, 0x%02x, 0x%02x\t", buffer[read_sz - 2] & 0xff, buffer[read_sz -1] & 0xff);
            printf("Recv Valid, 0x%02x, 0x%02x\n", crc_checksum & 0xff, (crc_checksum >> 8) & 0xff);
#ifdef DEBUG
            printf("****invalid data, size=%d\n", read_sz);
            for(i = 0; i < read_sz; i++)
                printf("0x%02x ", (buffer[i] & 0xff));
            printf("\n");
#endif
            return 0;
        }

        time(&crt_time);
        time_p = gmtime(&crt_time);
        gettimeofday(&tv, NULL);
        printf("%d-%02d-%d %02d:%02d:%02d:%03d", time_p->tm_year + 1900, time_p->tm_mon+1, 
                time_p->tm_mday, time_p->tm_hour+8, time_p->tm_min, time_p->tm_sec, tv.tv_usec/1000);
        printf("--Recv[%d]:", read_sz);
        for(i = 0; i < read_sz; i++)
            printf("0x%02x ", (buffer[i] & 0xff));
        printf("\n");
    
        memcpy(out, buffer+3, read_sz - 2);
    
        return read_sz - 5;
    }

    return 0;
}

void print_senddata(char *buf, unsigned int len)
{
    int i;

    printf("--Sending[%d]:", len);
    for(i = 0; i < len; i++)
        printf("0x%02x ", (buf[i] & 0xff));
    printf("\n");

}

char* usage_array = 
    "-t use tcp socket type\n"
    "-u use udp socket type. Choose only one type from TCP and UDP.\n"
    "-d IP address which you want connect\n"
    "-p fill server port number to connect\n"
    "-i Serial port device.On Linux likes /dev/ttyS0 or /dev/ttyUSB0. On Mac OS X likes /dev/cu.usbserial.\n"
    "-a use manual interactive send data, you must fill it use decimal digital.eg: -a 1.4.65.0.0.7\n"
    "-s send soe packet\n"
    "-y send YX packet\n"
    "-r send read config value packet\n"
    "-l use loop mode to query device\n"
    "-c send Telemetering packet\n"
    "-k send RemoteControl packet\n"
    "Use serial port sample:\t-i /dev/ttyUSB0 -y\n"
    "Use socket sample:\t-d 192.168.1.19 -p 8964 -t -y\n"
;
void usage_info()
{
    printf("%s\tver:%s\n", PROG_NAME, PROG_VERSION);
    printf("%s", usage_array);
}
