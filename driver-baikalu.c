
/*
 * Copyright 2012-2013 Andrew Smith
 * Copyright 2012 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>

#include "logging.h"
#include "miner.h"
#include "usbutils.h"
#include "util.h"
#include "config_parser.h"
#include "driver-baikal.h"
#include "compat.h"
#include "algorithm.h"

int thecounter = 0; // Added for debug 26.03.18

static int api_clock = BAIKAL_CLK_DEF;

static int baikal_sendmsg(struct cgpu_info *baikal, baikal_msg *msg)
{
    int err;
    int i, pos = 0;
    int amount;
    uint8_t buf[512] = {0, };

    buf[pos++] = ':';
    buf[pos++] = msg->miner_id;
    buf[pos++] = msg->cmd;
    buf[pos++] = msg->param;
    buf[pos++] = msg->dest;

    for (i = 0; i < msg->len; i++, pos += 2) {
        buf[pos + 1] = msg->data[i];
    }

    buf[pos++] = '\r';
    buf[pos++] = '\n';

    err = usb_write(baikal, (char *)buf, pos, &amount, C_BAIKAL_SEND);
    if (err < 0) {
        applog(LOG_ERR, "baikal_sendmsg error(%d)\n", err);
        return (err);
    }

    return (amount);
}


static int baikal_readmsg(struct cgpu_info *baikal, baikal_msg *msg, int size)
{
    int err;
    int amount;
    int len, pos = 1;
    uint8_t buf[128] = {0, };

    err = usb_read_once(baikal, (char *)buf, size, &amount, C_BAIKAL_READ);
    if (err < 0 || amount < 7) {
        return (err);
    }

    if ((buf[0] != ':') || (buf[amount - 2] != '\r') || (buf[amount - 1] != '\n')) {
        return (-1);
    }

    msg->miner_id   = buf[pos++];
    msg->cmd        = buf[pos++];
    msg->param      = buf[pos++];
    msg->dest       = buf[pos++];

    for (len = 0; pos < amount - 2; len++, pos += 2) {
        msg->data[len] = buf[pos + 1];
    }

    msg->len = len;

    return (amount);
}

static void baikal_cleanup(struct cgpu_info *baikal)
{
    int i;
    struct baikal_info *info  = baikal->device_data;
    struct miner_info *miner;
    struct cgpu_info *tmp;
    struct thr_info *thr;

    for (i = 0; i < info->miner_count; i++) {
        miner  = &info->miners[i];
        thr = mining_thr[miner->thr_id];

        if (thr) {
            tmp = thr->cgpu;
            tmp->shutdown = true;
            tmp->deven = DEV_DISABLED;
            usb_nodev(tmp);
        }
    }
}


static void baikal_clearbuffer(struct cgpu_info *baikal)
{
    int err, retries = 0;
    baikal_msg msg;

    do {
        err = baikal_readmsg(baikal, &msg, 128);
        usb_buffer_clear(baikal);
        if (err < 0)
            break;
    }
    while (retries++ < 10);
}


static bool baikal_finalize(struct cgpu_info *baikal)
{
    usb_uninit(baikal);

    if (baikal->device_data) {
        free(baikal->device_data);
        baikal->device_data = NULL;
    }

    if (baikal->mutex) {
        free(baikal->mutex);
    }

    if (baikal->name) {
        free(baikal->name);
    }

    baikal = usb_free_cgpu(baikal);

    return (true);
}


static bool baikal_reset(struct cgpu_info *baikal)
{
    int amount;
    struct baikal_info *info    = baikal->device_data;
    baikal_msg msg = {0, };

    msg.miner_id    = 0x0;
    msg.cmd         = BAIKAL_RESET;
    msg.len         = 0;
    msg.dest        = 0;

    mutex_lock(baikal->mutex);

    amount = baikal_sendmsg(baikal, &msg);
    if (amount < 0) {
        mutex_unlock(baikal->mutex);
        return (false);
    }

    amount = baikal_readmsg(baikal, &msg, 7);
    if (amount < 0) {
        mutex_unlock(baikal->mutex);
        return (false);
    }

    info->miner_count = msg.param;

    mutex_unlock(baikal->mutex);

    return (true);
}


static bool baikal_getinfo(struct cgpu_info *baikal)
{
    int amount;
    uint16_t sign;
    baikal_msg msg = {0, };
    struct baikal_info *info    = baikal->device_data;
    struct miner_info *miner    = &info->miners[baikal->miner_id];

    msg.miner_id    = baikal->miner_id;
    msg.cmd         = BAIKAL_GET_INFO;
    msg.dest        = 0;
    msg.len         = 0;

    mutex_lock(baikal->mutex);

    amount = baikal_sendmsg(baikal, &msg);
    if (amount < 0) {
        mutex_unlock(baikal->mutex);
        return (false);
    }

    amount = baikal_readmsg(baikal, &msg, 21);
    if (amount < 0) {
        mutex_unlock(baikal->mutex);
        return (false);
    }

    mutex_unlock(baikal->mutex);

    miner->fw_ver       = msg.data[0];
    miner->hw_ver       = msg.data[1];
    miner->bbg          = msg.data[2];
    miner->clock        = msg.data[3] << 1;
    miner->asic_count   = msg.data[4];
    miner->asic_count_r = msg.data[5];
    miner->asic_ver     = msg.data[6];
    miner->working_diff = 0.1;
    miner->work_idx     = 0;
    miner->working      = true;
    miner->overheated   = false;
	
	applog(LOG_ERR, "Firmware Version: %d - Hardware Version: %d - BBG?: %d - Clock: %d - Clock << 1: %d - ASIC Count: %d - ASIC Count R: %d - ASIC Ver: %d.", msg.data[0], msg.data[1], msg.data[2], msg.data[3], msg.data[3] << 1, msg.data[4], msg.data[5], msg.data[6]); // Added for debug 26.03.18
    return (true);
}


static bool baikal_setoption(struct cgpu_info *baikal, uint16_t clk, uint8_t mode, uint8_t temp, uint8_t fanspeed)
{
    int amount;
    baikal_msg msg = {0, };

    msg.miner_id    = baikal->miner_id;
    msg.cmd         = BAIKAL_SET_OPTION;
    msg.data[0] = (clk == 0) ? clk : ((clk / 10) % 20) + 2;
    msg.data[1] = mode;
    msg.data[2] = temp;
    msg.data[3] = fanspeed;
    msg.dest        = 0;
    msg.len         = 4;

    mutex_lock(baikal->mutex);

    amount = baikal_sendmsg(baikal, &msg);
    if (amount < 0) {
        mutex_unlock(baikal->mutex);
        return (false);
    }

    amount = baikal_readmsg(baikal, &msg, 7);
    if (amount < 0) {
        mutex_unlock(baikal->mutex);
        return (false);
    }

    mutex_unlock(baikal->mutex);

    return (true);
}


static bool baikal_setidle(struct cgpu_info *baikal)
{
    int amount;
    struct baikal_info *info    = baikal->device_data;
    baikal_msg msg = {0, };

    msg.miner_id    = baikal->miner_id;
    msg.cmd         = BAIKAL_SET_IDLE;
    msg.len         = 0;
    msg.dest        = 0;

    mutex_lock(baikal->mutex);

    amount = baikal_sendmsg(baikal, &msg);
    mutex_unlock(baikal->mutex);

    return (true);
}


static bool baikal_detect_remains(struct cgpu_info *baikal)
{
    int index;
    char devpath[32];
    struct baikal_info *info = baikal->device_data;
    struct miner_info *miner;

    for (index = 1; index < info->miner_count; index++) {

        struct cgpu_info *tmp = usb_copy_cgpu(baikal);

        sprintf(devpath, "%d:%d:%d",
                (int)(baikal->usbinfo.bus_number),
                (int)(baikal->usbinfo.device_address),
                index);

        tmp->device_path        = strdup(devpath);
        tmp->usbinfo.usbstat    = USB_NOSTAT;
        tmp->miner_id           = index;
        tmp->device_data        = baikal->device_data;
        tmp->mutex              = baikal->mutex;
        tmp->algorithm          = baikal->algorithm;
        tmp->threads            = 1;

        miner = &info->miners[tmp->miner_id];
        memset(miner, 0, sizeof(struct miner_info));
        cgtimer_time(&miner->start_time);
        if (baikal_getinfo(tmp) == false) {
            tmp = usb_free_cgpu(tmp);
            continue;
        }

        if (baikal_setoption(tmp, info->clock, to_baikal_algorithm(baikal->algorithm.type), info->cutofftemp, info->fanspeed) != true) {
            tmp = usb_free_cgpu(tmp);
            continue;
        }

        if (!add_cgpu(tmp)) {
            tmp = usb_free_cgpu(tmp);
            continue;
        }

        update_usb_stats(tmp);
    }

    return (true);
}


static struct cgpu_info* baikal_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
    struct cgpu_info *baikal;
    struct baikal_info *info;
    struct miner_info *miner;
    int clock           = BAIKAL_CLK_DEF;
    int cutofftemp      = BAIKAL_CUTOFF_TEMP;
    int fanspeed        = BAIKAL_FANSPEED_DEF;
    int recovertemp     = BAIKAL_RECOVER_TEMP;

#if BAIKAL_ENABLE_SETCLK
    if (opt_baikal_options != NULL) {
        sscanf(opt_baikal_options, "%d:%d:%d", &clock, &recovertemp, &cutofftemp);
        if (clock < BAIKAL_CLK_MIN) {
            clock = BAIKAL_CLK_MIN;
        }
        if (clock > BAIKAL_CLK_MAX) {
            clock = BAIKAL_CLK_MAX;
        }
    }
#else
    if (opt_baikal_options != NULL) {
        sscanf(opt_baikal_options, "%d:%d", &recovertemp, &cutofftemp);
    }
#endif

    if (opt_baikal_fan != NULL) {
        sscanf(opt_baikal_fan, "%d", &fanspeed);
        if (fanspeed > BAIKAL_FANSPEED_MAX) {
            fanspeed = BAIKAL_FANSPEED_DEF;
        }
    }

    baikal = usb_alloc_cgpu(&baikalu_drv, 1);
    baikal->mutex = calloc(1, sizeof(*(baikal->mutex)));
    mutex_init(baikal->mutex);

    info = (struct baikal_info *)calloc(1, sizeof(struct baikal_info));
    info->clock         = clock;
    info->cutofftemp    = (uint8_t)cutofftemp;
    info->fanspeed      = (uint8_t)fanspeed;
    info->recovertemp   = (uint8_t)recovertemp;

    baikal->device_data = info;
    baikal->name        = strdup("BKLU");
    baikal->miner_id    = 0;
    baikal->algorithm   = default_profile.algorithm;

    miner = &info->miners[baikal->miner_id];
    memset(miner, 0, sizeof(struct miner_info));
    cgtimer_time(&miner->start_time);

    if (!usb_init(baikal, dev, found)) {
        goto out;
    }

    baikal_clearbuffer(baikal);

    if (baikal_reset(baikal) != true) {
        goto out;
    }

    if (baikal_getinfo(baikal) != true) {
        goto out;
    }

    if (baikal_setoption(baikal, clock, to_baikal_algorithm(default_profile.algorithm.type), cutofftemp, fanspeed) != true) {
        goto out;
    }

    if (!add_cgpu(baikal)) {
        goto out;
    }

    update_usb_stats(baikal);

    baikal_detect_remains(baikal);

    return (baikal);

out:
    baikal_finalize(baikal);
    return (NULL);
}


static void baikal_detect(void)
{
    usb_detect(&baikalu_drv, baikal_detect_one);
}


static void baikal_get_statline_before(char *buf, size_t bufsiz, struct cgpu_info *baikal)
{
    struct baikal_info *info    = baikal->device_data;
    struct miner_info *miner  = &info->miners[baikal->miner_id];

#if BAIKAL_CLK_FIX
    tailsprintf(buf, bufsiz, "%s%dC %3uMHz [ASICS #%d] | ", (baikal->temp < 10) ? " " : "", (int)miner->temp, api_clock, miner->asic_count);
#else
    tailsprintf(buf, bufsiz, "%s%dC %3uMHz [ASICS #%d] | ", (baikal->temp < 10) ? " " : "", (int)miner->temp, miner->clock, miner->asic_count);
#endif
}


static struct api_data* baikal_api_stats(struct cgpu_info *cgpu)
{
    struct baikal_info *info    = cgpu->device_data;
    struct miner_info *miner    = &info->miners[cgpu->miner_id];
    struct thr_info *thr 		= mining_thr[miner->thr_id];
    struct api_data *root = NULL;

    root = api_add_int(root, "Chip Count", (int *)&miner->asic_count, false);

#if BAIKAL_CLK_FIX
    root = api_add_int(root, "Clock", &api_clock, false);
#else
    root = api_add_int(root, "Clock", &miner->clock, false);
#endif
    root = api_add_int(root, "HWV", (int *)&miner->hw_ver, false);
    root = api_add_int(root, "FWV", (int *)&miner->fw_ver, false);
    root = api_add_string(root, "Algo", (char *)algorithm_type_str[thr->cgpu->algorithm.type], false);

    return (root);
}


static void baikal_identify(struct cgpu_info *baikal)
{
    int amount;
    baikal_msg msg = {0, };

    msg.miner_id    = baikal->miner_id;
    msg.cmd         = BAIKAL_SET_ID;
    msg.dest        = 0;
    msg.len         = 0;

    mutex_lock(baikal->mutex);

    amount = baikal_sendmsg(baikal, &msg);
    if (amount < 0) {
        mutex_unlock(baikal->mutex);
        return;
    }

    amount = baikal_readmsg(baikal, &msg, 7);
    if (amount < 0) {
        mutex_unlock(baikal->mutex);
        return;
    }

    mutex_unlock(baikal->mutex);
}


static bool baikal_prepare(struct thr_info *thr)
{
    struct cgpu_info *baikal    = thr->cgpu;
    struct baikal_info *info    = baikal->device_data;

    cglock_init(&(info->pool.data_lock));

    return (true);
}


static void baikal_checknonce(struct cgpu_info *baikal, baikal_msg *msg)
{
    struct baikal_info *info = baikal->device_data;
    struct miner_info *miner = &info->miners[msg->miner_id];
    uint8_t work_idx, chip_id, unit_id;
    uint32_t nonce;

    chip_id     = msg->data[4];
    work_idx    = msg->data[5];
    unit_id     = msg->data[7];
    nonce       = *((uint32_t *)msg->data);

    if (work_idx >= BAIKAL_WORK_FIFO) {
        return;
    }

    if ((miner->works[work_idx] == NULL) || (baikal==NULL)) {
        return;
    }

#if BAIKAL_CHECK_STALE
    /* stale work */
    if (miner->works[work_idx]->devflag == false) {
        return;
    }
#endif

    /* check algorithm */
    if (miner->works[work_idx]->pool->algorithm.type != baikal->algorithm.type) {
		//applog(LOG_ERR, "? : %d[u:%d, c:%2d] : [%3d, %08x]", msg->miner_id, unit_id, chip_id, work_idx, nonce);
        return;
    }

    if (submit_nonce(mining_thr[miner->thr_id], miner->works[work_idx], nonce) == true) {
		//applog(LOG_ERR, "stale : %d[u:%d, c:%2d] : [%3d, %08x]", msg->miner_id, unit_id, chip_id, work_idx, nonce);
        miner->asics[unit_id][chip_id].nonce++;
        miner->nonce++;
    }
    else {
        applog(LOG_ERR, "hw error : %d[u:%d, c:%2d] : [%3d, %08x]", msg->miner_id, unit_id, chip_id, work_idx, nonce);
        miner->asics[unit_id][chip_id].error++;
		//miner->asics[unit_id][chip_id].nonce++;
        miner->error++;
        hw_errors_bkl++;
    }
}


static bool baikal_send_work(struct cgpu_info *baikal, int miner_id)
{
    struct baikal_info *info = baikal->device_data;
    struct miner_info *miner = &info->miners[miner_id];

    struct thr_info *thr = mining_thr[miner->thr_id];
    struct work *work;
    uint32_t target;
    baikal_msg msg;
    uint8_t algo;

    /* Do not send */
    if (miner->overheated == true) {
        return (true);
    }

	mutex_lock(baikal->mutex);

    if (miner->works[miner->work_idx] == NULL) {        
        if ((miner_id != 0) && (miner->asic_ver == 0x51)) {
            struct miner_info *miner_base = &info->miners[0];
            int idx = (miner_base->work_idx - 1) % BAIKAL_WORK_FIFO;
            work = copy_work(miner_base->works[idx]);            
            work->thr_id = miner->thr_id;            
        }
        else {
            work = get_work(thr, miner->thr_id);            
        }
        miner->works[miner->work_idx] = work;
        work->devflag = true;
    }

    if (work->pool->algorithm.type != thr->cgpu->algorithm.type) {
        thr->cgpu->algorithm.type = work->pool->algorithm.type;
    }

    work->device_diff = MAX(miner->working_diff, work->work_difficulty);
    //work->device_diff = MIN(miner->working_diff, work->work_difficulty);
    set_target(work->device_target, work->device_diff, work->pool->algorithm.diff_multiplier2, work->thr_id);

    memset(msg.data, 0x0, 512);
    msg.data[0] = to_baikal_algorithm(work->pool->algorithm.type);
    msg.data[1] = miner_id;
    memcpy(&msg.data[2], &work->device_target[24], 8);
    if (*((uint32_t *)&msg.data[6]) != 0x0) { // TripleS
        memset(&msg.data[2], 0xFF, 4);
    }

    switch (baikal->algorithm.type) {
    case ALGO_BLAKECOIN:        // blake256r8
    case ALGO_VANILLA:
        if (work->pool->algorithm.calc_midstate) {   // use midstate
            msg.data[0] += 1;

            memcpy(&msg.data[10], work->midstate, 32);
            memcpy(&msg.data[42], &work->data[64], 16);
            be32enc_vect((uint32_t *)&msg.data[42], (const uint32_t *)&msg.data[42], 4);
            *((uint32_t *)&msg.data[58]) = 0x00000080;
            *((uint32_t *)&msg.data[94]) = 0x01000000;
            *((uint32_t *)&msg.data[102]) = 0x80020000;
            msg.len = 106;
        }
        else {
            memcpy(&msg.data[10], work->data, 80);
            be32enc_vect((uint32_t *)&msg.data[10], (const uint32_t *)&msg.data[10], 20);
            msg.len = 90;
        }
        break;

    case ALGO_DECRED:           // blake256r14
        if (work->pool->algorithm.calc_midstate) {   // use midstate
            msg.data[0] += 1;

            memcpy(&msg.data[10], work->midstate, 32);
            memcpy(&msg.data[42], &work->data[128], 52);
            *((uint32_t *)&msg.data[94]) = 0x01000080UL;
            *((uint32_t *)&msg.data[98]) = 0x00000000UL;
            *((uint32_t *)&msg.data[102]) = 0xa0050000UL;
            msg.len = 106;
        }
        else {
            memcpy(&msg.data[10], work->data, 180);
            msg.len = 190;
        }
        break;
    case ALGO_SIA:              // blake2b
        memcpy(&msg.data[10], work->data, 80);
        be32enc_vect((uint32_t *)&msg.data[10], (const uint32_t *)&msg.data[10], 20);
        msg.len = 90;
        break;

    case ALGO_LBRY:             // lbry-all
        memcpy(&msg.data[10], work->data, 112);
        be32enc_vect((uint32_t *)&msg.data[10], (const uint32_t *)&msg.data[10], 27);
        msg.len = 122;
        break;

    case ALGO_PASCAL:           // lbry-sha
        memcpy(&msg.data[10], work->data, 200);
        msg.len = 210;
        break;


    case ALGO_CRYPTONIGHT:
        if ((work->pool != NULL) && (strstr(work->pool->rpc_url, "nicehash") != NULL)) {
            msg.data[0] += 1;   // cn_nice
        }

        memcpy(&msg.data[10], work->data, 80);
        msg.len = 90;
        break;

    case ALGO_CRYPTONIGHT_LITE:
    case ALGO_X11:
    case ALGO_X11GOST:
    case ALGO_SKEINCOIN:
    case ALGO_MYRIAD_GROESTL:
    case ALGO_QUARK:
    case ALGO_QUBIT:
    case ALGO_GROESTL:
    case ALGO_SKEIN2:
    case ALGO_NIST:
    case ALGO_BLAKE:
    case ALGO_VELTOR:
    default:
        memcpy(&msg.data[10], work->data, 80);
        msg.len = 90;
        break;
    }

    msg.miner_id    = miner_id;
    msg.cmd         = BAIKAL_SEND_WORK;
    msg.param       = miner->work_idx;
    msg.dest        = 0;

    if (baikal_sendmsg(baikal, &msg) < 0) {
        applog(LOG_ERR, "baikal_send_work : sendmsg error[%d]", miner_id);
        mutex_unlock(baikal->mutex);
        return (false);
    }

    if (baikal_readmsg(baikal, &msg, 7) < 0) {
        applog(LOG_ERR, "baikal_send_work : readmsg error[%d]", miner_id);
        mutex_unlock(baikal->mutex);
        return (false);
    }

    /* update clock */
    miner->clock = msg.param << 1;

    miner->work_idx++;
    if (miner->work_idx >= BAIKAL_WORK_FIFO) {
        miner->work_idx = 0;
    }

    if (miner->works[miner->work_idx] != NULL) {
        free_work(miner->works[miner->work_idx]);
        miner->works[miner->work_idx] = NULL;
    }

    /* TODO */
    //cgtimer_time(&miner->start_time);
    mutex_unlock(baikal->mutex);

    return (true);
}


static bool baikal_process_result(struct cgpu_info *baikal)
{
    struct baikal_info *info = baikal->device_data;
    struct miner_info *miner;
    baikal_msg msg = {0, };
    int i;

    for (i = 0; i < info->miner_count; i++) {
        miner = &info->miners[i];
        if (miner->working == true) {
            msg.miner_id    = i;
            msg.cmd         = BAIKAL_GET_RESULT;
            msg.dest        = 0;
            msg.len         = 0;

            mutex_lock(baikal->mutex);
            if (baikal_sendmsg(baikal, &msg) < 0) {
                applog(LOG_ERR, "baikal_process_result : sendmsg error");
                mutex_unlock(baikal->mutex);
                return (false);
            }

            if (baikal_readmsg(baikal, &msg, 23) < 0) {
                applog(LOG_ERR, "baikal_process_result : readmsg error miner_id = %d", i);
                mutex_unlock(baikal->mutex);
                return (false);
            }
            mutex_unlock(baikal->mutex);

            miner->temp = msg.data[6];

            if (msg.param & 0x01) {
                baikal_checknonce(baikal, &msg);
            }

            if (msg.param & 0x02) {
                baikal_send_work(baikal, i);
            }

            if (msg.param & 0x04) {
                return false;
            }

            if (miner->temp > info->cutofftemp) {
                miner->overheated = true;
            }
            else if (miner->temp < info->recovertemp) {
                miner->overheated = false;
            }

            cgsleep_ms(1);
        }
    }

    return (true);
}

static int64_t baikal_hash_done(struct cgpu_info *baikal, struct miner_info *miner, int elpased)
{
    int64_t hash_done = 0;

    hash_done = (int64_t)miner->clock * (int64_t)miner->asic_count * (int64_t)elpased;

    switch(baikal->algorithm.type) {
    case ALGO_CRYPTONIGHT:
        hash_done /= 2000; // hash_done = miner->clock * miner->asic_count * elpased / 1776;
        break;
    case ALGO_CRYPTONIGHT_LITE:
        hash_done /= 1000; //hash_done = miner->clock * miner->asic_count * elpased 
        break;
    case ALGO_X11:
    case ALGO_QUARK:
    case ALGO_QUBIT:
    case ALGO_NIST:
    case ALGO_MYRIAD_GROESTL:
    case ALGO_GROESTL:
		hash_done *= 120;
		//if (thecounter > 500) {  // Added for debug 26.03.18
			//applog(LOG_ERR, "Case X11/QUARK/QUBIT/NIST/MYRIAD_STROESTL/GROESTL: Miner Clock: %lld, ASICs: %lld, calculated to: %lld.", (int64_t)miner->clock, (int64_t)miner->asic_count, hash_done); // Added for debug 26.03.18
			//thecounter = 0;  // Added for debug 26.03.18
		//}
		//hash_done = miner->clock * miner->asic_count * elpased * 128;
        break;
    case ALGO_SKEINCOIN:
    case ALGO_SKEIN2:
        hash_done *= 62; 
        break;

    case ALGO_X11GOST:
    case ALGO_VELTOR:
        hash_done *= 16; // hash_done = miner->clock * miner->asic_count * elpased * 18;
        break;

    case ALGO_BLAKECOIN:
    case ALGO_DECRED:
    case ALGO_VANILLA:
    case ALGO_BLAKE:
		hash_done *= 2000; // hash_done = miner->clock * miner->asic_count * elpased * 2048;
        break;

    case ALGO_SIA:
        hash_done *= 1000; //hash_done = miner->clock * miner->asic_count * elpased * 1024; 
        break;
    case ALGO_LBRY:
    case ALGO_PASCAL:
        hash_done *= 500; // hash_done = miner->clock * miner->asic_count * elpased * 512; 
        break;
    }
	thecounter++;  // Added for debug 26.03.18
    return hash_done;
}


static int64_t baikal_scanhash(struct thr_info *thr)
{
    struct cgpu_info *baikal = thr->cgpu;
    struct baikal_info *info = baikal->device_data;
    struct miner_info *miner = &info->miners[baikal->miner_id];
    cgtimer_t now;
    int elapsed, i;

    if (baikal->usbinfo.nodev) {
        return (-1);
    }

    if (baikal->miner_id == 0) {
        if (baikal_process_result(baikal) != true) {
            baikal_cleanup(baikal);
            return (-1);
        }
    }
    else {
        cgsleep_ms(50);
    }

    baikal->temp = miner->temp;

    if (miner->work_idx == 0) {
        return 0;
    }

    cgtimer_time(&now);
    elapsed = cgtimer_to_ms(&now) - cgtimer_to_ms(&miner->start_time);
    miner->start_time = now;

    return (baikal_hash_done(baikal, miner, elapsed));
}


static void baikal_update_work(struct cgpu_info *baikal)
{
    int i, j, count;
    struct timeval now;
    struct baikal_info *info = baikal->device_data;
    struct miner_info *miner;

    if (baikal->miner_id == 0) {
        switch(baikal->algorithm.type) {
        case ALGO_CRYPTONIGHT:
        case ALGO_CRYPTONIGHT_LITE:
            count = 1;
            break;
        case ALGO_SIA:
        case ALGO_DECRED:
            count = 0;
        default:
            count = 4;
        }

        for (i = 0; i < info->miner_count; i++) {
            miner = &info->miners[i];
#if BAIKAL_CHECK_STALE
            /* set work stale */
            mutex_lock(baikal->mutex);
            for (j = 0; j < BAIKAL_WORK_FIFO; j++) {
                if (miner->works[j] != NULL) {
                    miner->works[j]->devflag = false;
                }
            }
            mutex_unlock(baikal->mutex);
#endif

            for (j = 0; j < count; j++) {
                if (baikal_send_work(baikal, i) != true) {
                    baikal_cleanup(baikal);
                    break;
                }
            }
        }
    }
}


static bool baikal_init(struct thr_info *thr)
{
    struct cgpu_info *baikal    = thr->cgpu;
    struct baikal_info *info    = baikal->device_data;
    struct miner_info *miner    = &info->miners[baikal->miner_id];

    miner->thr_id               = thr->id;
    cgtimer_time(&miner->start_time);
    return (true);
}


static void baikal_shutdown(struct thr_info *thr)
{
    int i, j;
    struct cgpu_info *baikal    = thr->cgpu;
    struct baikal_info *info    = baikal->device_data;

    for (i = 0; i < info->miner_count; i++) {
        struct miner_info *miner    = &info->miners[i];
        struct thr_info *thr        = mining_thr[miner->thr_id];
        struct cgpu_info *baikal    = thr->cgpu;
        baikal_setidle(baikal);

#if 0   /* TODO : ???*/
        for (j = 0; j < BAIKAL_WORK_FIFO; j++) {
            if(miner->works[j] != NULL) {
                free_work(miner->works[j]);
            }
        }
#endif
        baikal->shutdown = true;
    }
}


struct device_drv baikalu_drv = {
    .drv_id					= DRIVER_baikalu,
    .dname					= "Baikal",
    .name					= "BKLU",
    .drv_detect				= baikal_detect,
    .get_statline_before	= baikal_get_statline_before,
    .get_api_stats			= baikal_api_stats,
    .identify_device		= baikal_identify,
    .thread_prepare			= baikal_prepare,
    .thread_init			= baikal_init,
    .hash_work              = hash_driver_work,
    .update_work            = baikal_update_work,
    //.flush_work             = baikal_update_work,
    .scanwork				= baikal_scanhash,
    .thread_shutdown        = baikal_shutdown,
};
