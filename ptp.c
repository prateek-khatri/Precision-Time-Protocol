/**********************************************************************
This File Defines API functions for Precision Time Protocol.
***********************************************************************
The protocol is based on the IEEE-1588 standard and tries to implement
a stripped down version of the same. The major difference being, we do
not do this cyclically, but just do this once at the start and forget.
However, the function calls can be easily changed to implement a cyclic
time synchronization.
***********************************************************************/
#include "ptp.h"

/******************************************************
                    Global Definitions 
*******************************************************/
static bool isMaster            = false;
static bool isSlave             = false;
static uint8_t numSlaves        = 0;

static int thetaN              = 0; // Theta(N)

static int32_t syncTimeSlave    = 0;
static int32_t delayTimeSlave   = 0;
static int32_t syncTimeMaster   = 0;
static int32_t delayTimeMaster  = 0;
static int32_t slaveOffset      = 0;
static int32_t slaveDelay       = 0;

static slaveList head[NUM_SLAVES];
static networkConfig netSock;

#define TIME_STAMP (CLK_CNT - slaveOffset)
/******************************************************
                    Helper Functions 
*******************************************************/

static
bool createUdpSocket(void)
{
    netSock.network_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(netSock.network_socket < 0)
    {
        return false;
    }
    return true;
}

static
bool createServerStructure(char * ip)
{
    memset((char *)(&netSock.server_address), 0, sizeof(netSock.server_address));

    netSock.server_address.sin_family = AF_INET;
    netSock.server_address.sin_port = htons(PTP_PORT_NUMBER);
    if (isSlave)
    {
        netSock.server_address.sin_addr.s_addr = htonl(INADDR_ANY);
        return true;
    }

    if (inet_aton(ip, &netSock.server_address.sin_addr) == 0)
    {
        return false;
    }
    return true;
}

static
void setSocketTimeout(int sec, int usec)
{
    netSock.read_timeout.tv_sec = sec;
    netSock.read_timeout.tv_usec = usec;

    setsockopt(netSock.network_socket, SOL_SOCKET, SO_RCVTIMEO,
                 &netSock.read_timeout, sizeof netSock.read_timeout);

}

static
void clearSocketTimeout(void)
{
    setSocketTimeout(0,0);
}

static
bool setAsMaster(void)
{
    isMaster = true;
    isSlave = !isMaster;
    return isMaster;
}

static
bool setAsSlave(void)
{
    isSlave = true;
    isMaster = !isSlave;
    return isSlave;
}

static inline
bool slaveSend(char * msg)
{
    int tB = 0;
    tB = sendto(netSock.network_socket, msg, sizeof(char), 0,
                                (struct sockaddr*) &netSock.client_address, 
                                sizeof(netSock.client_address));
    if (tB < 1) return false;
    return true;

}

static inline
bool slaveReceive(char * msg)
{
    int tB = 0;
    socklen_t size = sizeof(netSock.client_address);
    tB = recvfrom(netSock.network_socket, msg, sizeof(char), 0,
                  (struct sockaddr*) &netSock.client_address, &size);
    if (tB < 1) return false;
    return true;
}

static inline
bool masterSend(char * msg)
{
    int tB = 0;
    tB = sendto(netSock.network_socket, msg, sizeof(char), 
                0, (struct sockaddr*) &netSock.server_address,
                sizeof(netSock.server_address));

    if (tB < 1) return false;
    return true;
}

static inline
bool masterReceieve(char * msg)
{
    int tB = 0;
    socklen_t size = sizeof(netSock.server_address);
    tB =  recvfrom(netSock.network_socket, msg, sizeof(char), 0,
                   (struct sockaddr*) &netSock.server_address, &size);
    
    if (tB < 1) return false;
    return true;

}

/******************************************************
                    Slave Functions 
*******************************************************/

static
bool bindSocket_SLAVE(void)
{
    if (bind(netSock.network_socket, (struct sockaddr*) &netSock.server_address,
                sizeof(netSock.server_address)) < 0)
    {
        return false;
    }
    return true;
}


static
bool waitForACK_SLAVE(int sec, int usec)
{
    setSocketTimeout(sec,usec);
    char ack = 0;
    
    if(!slaveReceive(&ack))
    {
        clearSocketTimeout();
        return false;
    }

    clearSocketTimeout();

    if (ack == 'A')
    {
        return true;
    }
    else
    {
        // Means we did't receive ACK, request retransmission
        return false;
    }
}

static
bool sendACK_SLAVE(void)
{
    char ack = 'A';
    
    if (!slaveSend(&ack))
    {
        return false;
    }

    return true;
}

static
bool sendNCK_SLAVE(void)
{
    char ack = 'N';

    if (!slaveSend(&ack))
    {
        return false;
    }

    return true;
}

static
bool getSyncMessage_SLAVE(void)
{
    // Don't timeout for this
    clearSocketTimeout();

    char msg = 0;

    if(!slaveReceive(&msg))
    {   
        sendNCK_SLAVE();
        return false;
    }

    syncTimeSlave = TIME_STAMP;

    if (msg == 'S')
    {
        sendACK_SLAVE();
        return true;
    }
    sendNCK_SLAVE();
    return false;
}

static
bool getFollowUpMessage_SLAVE(void)
{
    // We handle time transmission here
    setSocketTimeout(1,0);

    uint32_t Tm = 0;
    socklen_t size = sizeof(netSock.client_address);
    int transmitBytes = recvfrom(netSock.network_socket, &Tm, sizeof(uint32_t), 0,
                                (struct sockaddr*) &netSock.client_address, 
                                &size);
    syncTimeMaster = Tm;
    clearSocketTimeout();
    if (transmitBytes > 0)
    {
        sendACK_SLAVE();
        return true;
    }
    sendNCK_SLAVE();
    return false;
}

static
bool sendDelayRequest_SLAVE(void)
{
    char msg = 'D';
    delayTimeSlave = TIME_STAMP;

    if (!slaveSend(&msg))
    {
        return false;
    }
    
    if (!waitForACK_SLAVE(1,0))
    {
        return false;
    }
    return true;
}

static
bool waitForDelayResp_SLAVE(void)
{
    setSocketTimeout(1,0);
    uint32_t Tm = 0;
    socklen_t size = sizeof(netSock.client_address);
    int transmitBytes = recvfrom(netSock.network_socket, &Tm, sizeof(uint32_t), 0,
                                (struct sockaddr*) &netSock.client_address, 
                                &size);
    clearSocketTimeout();
    if (transmitBytes > 0)
    {
        delayTimeMaster = Tm;
        sendACK_SLAVE();
        return true;
    }
    sendNCK_SLAVE();
    return false;

}

static
bool calcNextTheta_SLAVE(void)
{
    thetaN = syncTimeSlave - syncTimeMaster - slaveDelay;

    if (thetaN != 0)
    {
        slaveOffset += thetaN; 
        return false;
    }
    return true;
}

static
bool calcDelay_SLAVE()
{
    slaveDelay = (syncTimeSlave - syncTimeMaster) + 
                 (delayTimeMaster - delayTimeSlave)/2;  
   return true;
}

static
bool startOffsetPhase_SLAVE(void)
{
    printf("Starting Offset Phase\n");
    while (!getSyncMessage_SLAVE());
    while (!getFollowUpMessage_SLAVE());
    if (!calcNextTheta_SLAVE())
    {
        printf("STM:%d, STS:%d, SLO:%d, SLD:%d\n", syncTimeMaster, syncTimeSlave, slaveOffset, slaveDelay);
        printf("NCK - Offset\n");
        sendNCK_SLAVE();
        return false;
    }
    printf("STM:%d, STS:%d, SLO:%d, SLD:%d\n", syncTimeMaster, syncTimeSlave, slaveOffset, slaveDelay);
    printf("ACK - Offset\n");
    sendACK_SLAVE();
    return true;

}

static
bool startDelayPhase_SLAVE(void)
{
    while (!sendDelayRequest_SLAVE());
    while (!waitForDelayResp_SLAVE());
    if (!calcDelay_SLAVE()) return true;

    return true;
}

static
bool sendOffset_SLAVE(void)
{
    slaveOffset +=slaveDelay;
    int transmitBytes = sendto(netSock.network_socket, &slaveOffset, sizeof(int32_t), 0,
                                (struct sockaddr*) &netSock.client_address, 
                                sizeof(netSock.client_address));

    if (transmitBytes < 1) return false;
    if (!waitForACK_SLAVE(1,0)) return false;

    return true;
}

static
void initAsSlave(void)
{
    while (!startOffsetPhase_SLAVE());
    while (!startDelayPhase_SLAVE());
    while (!startOffsetPhase_SLAVE());
    while (!sendOffset_SLAVE());
}

/******************************************************
                    Master Functions 
*******************************************************/

static
bool isSlaveListEmpty_MASTER(void)
{
    if (numSlaves == 0)  return true;

    return false;
}

static
bool waitForACK_MASTER(int sec, int usec)
{
    setSocketTimeout(sec,usec);
    char ack = 0;

    if(!masterReceieve(&ack))
    {
        clearSocketTimeout();
        return false;
    }

    clearSocketTimeout();
    
    if (ack != 'A') return false;

    return true;
}

static
bool sendACK_MASTER(void)
{
    char ack = 'A';
    if(!masterSend(&ack))
    {
        return false;
    }
    return true;
}

static
bool sendNCK_MASTER(void)
{
    char nck = 'N';
    if(!masterSend(&nck))
    {
        return false;
    }
    return true;
}

static 
bool sendSyncMessage_MASTER(void)
{
    char msg = 'S';
    syncTimeMaster = TIME_STAMP;

    if(!masterSend(&msg)) return false;
    if (!waitForACK_MASTER(1,0)) return false;

    return true;
}

static
bool sendFollowUpMessage_MASTER(void)
{
    int tB = 0;
    tB = sendto(netSock.network_socket, &syncTimeMaster, sizeof(int32_t), 
                0, (struct sockaddr*) &netSock.server_address,
                sizeof(netSock.server_address));
    if (tB < 1) return false;
    if (!waitForACK_MASTER(1,0)) return false;

    return true;

}

static
bool waitForDelayReq_MASTER(void)
{
    char msg = 0;
    if (!masterReceieve(&msg)) return false;
    delayTimeMaster = TIME_STAMP;

    if (msg != 'D')
    {
        sendNCK_MASTER();
        return false;
    }

    sendACK_MASTER();
    return true;
}

static
bool sendDelayResponse_MASTER(void)
{
    int tB = 0;
    tB = sendto(netSock.network_socket, &delayTimeMaster, sizeof(int32_t), 
                0, (struct sockaddr*) &netSock.server_address,
                sizeof(netSock.server_address));
    if (tB < 1) return false;
    if (!waitForACK_MASTER(1,0)) return false;

    return true;
}

static
bool waitForOffset_MASTER(void)
{
    slaveOffset = 0;
    int tB = 0;
    socklen_t size = sizeof(netSock.server_address);
    tB =  recvfrom(netSock.network_socket, &slaveOffset, sizeof(int32_t), 0,
                   (struct sockaddr*) &netSock.server_address, &size);

    if (tB < 1)
    {
        sendNCK_MASTER();
        return false;
    }
    sendACK_MASTER();
    return true;
}


static
bool startOffsetPhase_MASTER(void)
{
    printf("Starting Offset Phase\n");
    while (!sendSyncMessage_MASTER());
    while (!sendFollowUpMessage_MASTER());
    // If we receive ACK - good , NCK  - restart phase
    return waitForACK_MASTER(1,0);
}

static
bool startDelayPhase_MASTER(void)
{
    while (!waitForDelayReq_MASTER());
    while (!sendDelayResponse_MASTER());
    return true;
}

static
void printfAllOffsets_MASTER(void)
{
    uint8_t i;
    for (i = 0; i < numSlaves; i++)
    {
        printf("Slave IP: %s  Offset: %d\n",head[i].ip,head[i].offset);
    }

}

static
void initAsMaster(void)
{
    while (!startOffsetPhase_MASTER());
    while (!startDelayPhase_MASTER());
    while (!startOffsetPhase_MASTER());
    while (!waitForOffset_MASTER());
}
/******************************************************
                    Middleware Functions 
*******************************************************/
static inline
bool initSlaveNetworking()
{
    if (!createUdpSocket()) return false;
    if (!createServerStructure(NULL)) return false;
    if (!bindSocket_SLAVE()) return false;
    return true;
}

static inline
bool initMasterNetworking(char * slaveIP)
{
    if (!createUdpSocket()) return false;
    if (!createServerStructure(slaveIP)) return false;
    return true;
}

static
bool initMaster(char * slaveIP)
{
    if(!initMasterNetworking(slaveIP))
    {
        return false;
    }
    initAsMaster();
    return true;
}

static
bool initSlave()
{
    if(!initSlaveNetworking())
    {
        return false;
    }
    initAsSlave();
    return true;
}             

/******************************************************
                    Public API Functions 
*******************************************************/
uint8_t getNumSlaves(void)
{
    return numSlaves;
}

bool addSlave(char *ip, uint8_t size) //IP = 16 bytes
{
    
    if (!isMaster)
    {
        printf("Master State not set\n");
        return false;
    }
    if (size < 1)
    {
        printf("Size < 1\n");
    }

    memcpy(head[numSlaves].ip, ip, size);
    head[numSlaves].isSync = false;
    head[numSlaves].offset = 0;

    printf("Slave IP Recvd: %s\n",head[numSlaves].ip);
    numSlaves++;
    return true;
}

uint32_t getOffset(char *ip, uint8_t size) // size = IP_SIZE
{
    if(!isMaster) return false;
    if (!ip || size < IP_SIZE) return -1;

    if (numSlaves < 1) return -1;

    int i;
    for (i = 0; i < numSlaves; i++)
    {
        if (strcmp(ip,head[i].ip) == 0)
        {
            return head[i].offset;
        }
    }
    
    return -1;

}

void startPtp(void)
{
    printf("Starting PTP Iteration\n");
    if (isSlave)
    {
        initSlave();
    }
    else if (isMaster)
    {
        uint8_t i;
        for (i = 0; i < numSlaves; i++)
        {
            initMaster(head[i].ip);
            head[i].isSync = true;
            head[i].offset = slaveOffset;
        }
    }
    else
    {
        printf("Master/Slave Undefined\n");
    }
    printfAllOffsets_MASTER();
    printf("--PTP END--\n");
}

void initPtp(bool master_slave)
{
    if (master_slave == MASTER)
    {
        setAsMaster();
    }
    else if (master_slave == SLAVE)
    {
        setAsSlave();
    }
    else
    {
        printf("Master/Slave Init Error\n");
    }
    
}



