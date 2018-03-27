#ifndef __DEVICE_BAIKAL_H__
#define __DEVICE_BAIKAL_H__

#include "miner.h"

#define BAIKAL_1751             (0x1)
#define BAIKAL_1772             (0x2)
#define BAIKAL_1791             (0x4)
#define BAIKAL_ALL              (0x7)
#define BAIKAL_TYPE             (BAIKAL_ALL)

#define BAIKAL_MAXMINERS	    (5)
#define BAIKAL_MAXUNIT          (4)
#define BAIKAL_MAXASICS	        (16)
#define BAIKAL_WORK_FIFO        (200)

#define BAIKAL_CLK_MIN  		(150)
#define BAIKAL_CLK_MAX		    (400)

#if BAIKAL_TYPE == BAIKAL_1751 // Really?
#define BAIKAL_CLK_DEF  		(200)
#elif BAIKAL_TYPE == BAIKAL_1791 // Really?
#define BAIKAL_CLK_DEF  		(400)
#else
#define BAIKAL_CLK_DEF  		(200) // This must be changed depending on your miner type. If you have Giant N set to 200, If you have X10 set to 300 and if you have B set to 400. Its left at 200 intentionally because Giant N is stock at 200.
#endif

#define BAIKAL_CUTOFF_TEMP      (55)
#define BAIKAL_FANSPEED_DEF     (100)
#define BAIKAL_FANSPEED_MAX     (100)
#define BAIKAL_RECOVER_TEMP     (40)

#define BAIKAL_RESET		    (0x01)
#define BAIKAL_GET_INFO	        (0x02)
#define BAIKAL_SET_OPTION	    (0x03)
#define BAIKAL_SEND_WORK	    (0x04)
#define BAIKAL_GET_RESULT	    (0x05)
#define BAIKAL_SET_ID		    (0x06)
#define BAIKAL_SET_IDLE		    (0x07)

#define BAIKAL_MINER_TYPE_NONE  (0x00)
#define BAIKAL_MINER_TYPE_MINI  (0x01)
#define BAIKAL_MINER_TYPE_CUBE  (0x02)

#define BAIKAL_ENABLE_SETCLK    (0)

#define BAIKAL_CHECK_STALE      (0)
#define BAIKAL_EN_HWE           (1)
#define BAIKAL_CLK_FIX          (0)

struct asic_info {
    uint32_t nonce;
    uint32_t error;
};

struct miner_info {
    int     thr_id;
    int     asic_count;  
    int     asic_count_r;  
    int     unit_count;
	int		temp;  
    int     clock;
    int     bbg;
    bool    working;
    bool    overheated;
    uint8_t fw_ver;
    uint8_t hw_ver;
    uint8_t asic_ver;    
    uint32_t nonce;
    uint32_t error;    
    double working_diff;    
    struct asic_info asics[BAIKAL_MAXUNIT][BAIKAL_MAXASICS]; 
    uint8_t work_idx;
    struct work *works[BAIKAL_WORK_FIFO];
    cgtimer_t start_time;
};


struct baikal_info {
    struct pool pool;
    int miner_count;
    int clock;
    uint8_t cutofftemp;
    uint8_t fanspeed;		// percent
    uint8_t recovertemp;
	pthread_t *process_thr;
    struct miner_info miners[BAIKAL_MAXMINERS];    
    uint8_t miner_type;
};

typedef struct {
    uint8_t     miner_id;
    uint8_t     cmd;
    uint8_t     param;
    uint8_t     dest;
    uint8_t     data[512];
    uint32_t    len;
} baikal_msg;


#endif /* __DEVICE_BAIKAL_H__ */
