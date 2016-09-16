#define IP_SIZE 16 // Bytes
#define NUM_MAX_SLAVES 5
#define NUM_SLAVES NUM_MAX_SLAVES

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdnet.h>
#include <stdbool.h>
#include <unistd.h>
#include <inttypes.h>

#ifndef CLK_CNT
#error "Define CLK_CNT Macro for Time Stamping"
#endif // CLK_CNT


#ifndef PTP_PORT_NUMBER
#define PTP_PORT_NUMBER 8101
#endif // PTP_PORT_NUMBER

enum MASTER_SLAVE {MASTER,SLAVE};

typedef struct slaveList
{
    char ip[IP_SIZE];
    uint32_t offset;
    bool isSync;
} slaveList;

typedef struct networkConfig
{
    int network_socket;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
    struct timeval read_timeout;
    socklen_t size;
} networkConfig;

/**************** Public Functions ***************/
void setNumSlaves(uint8_t num);
uint8_t getNumSlaves(void);
bool addSlave(char *ip, uint8_t size); 
uint32_t getOffset(char *ip, uint8_t size);
void startPtp(void);
void initPtp(bool masterSlave);

