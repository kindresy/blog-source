/* SPDX-License-Identifier: Apache-2.0 */
#include "chip_endpoint.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static uint64_t now(void *p) { (void)p; return 1; }
static uint8_t out[69];
static size_t outlen;
static int request(const uint8_t *msg, size_t len)
{
    assert(len <= 64);
    uint8_t wire[69] = {1, 9, 8, 0xcb}; /* SOM/EOM=1, TO=1, tag=3 */
    memcpy(wire + 4, msg, len);
    wire[len+4] = chip_endpoint_pec(0x88, wire, len+4);
    return chip_endpoint_rx(0x44, wire, len+5);
}
static void peek(void)
{
    assert(chip_endpoint_tx_peek(0x44,out,sizeof(out),&outlen)==0);
    assert(out[0]==1 && out[1]==8 && out[2]==9);
    assert((out[3]&0xcf)==0xc3); /* TO=0, tag preserved */
    assert(out[outlen-1]==chip_endpoint_pec(0x89,out,outlen-1));
}
static void done(void) { assert(chip_endpoint_tx_complete(outlen,true)==0); }
int main(void)
{
    assert(chip_endpoint_init(9,1,now,NULL)==0);
    /* 标准 CRC-8 check vector，包括地址参数后的所有字节。 */
    assert(chip_endpoint_pec('1',(const uint8_t *)"23456789",8)==0xf4);
    uint8_t geteid[]={0,0x81,2};
    assert(request(geteid,sizeof(geteid))==0); peek();
    assert(outlen==12 && out[7]==0 && out[8]==9); done();
    uint8_t gettypes[]={1,0x87,0,4};
    assert(request(gettypes,sizeof(gettypes))==0); peek();
    assert(outlen==18 && out[4]==1 && out[5]==7 && out[6]==0 && out[7]==4);
    assert(out[8]==0 && out[9]==1);
    /* 短读保留原包，下次 peek 必须逐字节相同。 */
    uint8_t saved[69]; memcpy(saved,out,outlen);
    assert(chip_endpoint_tx_complete(outlen-1,false)==-EIO);
    peek(); assert(!memcmp(saved,out,outlen)); done();
    uint8_t bad[]={1,0,8,0xcb,1,0x80,0,2,0};
    assert(chip_endpoint_rx(0x44,bad,sizeof(bad))==-EBADMSG);
    assert(chip_endpoint_tx_pending()==0);
    uint8_t tid[]={1,0x83,0,1,42};
    assert(request(tid,sizeof(tid))==0); peek(); assert(out[8]==0); done();
    tid[3]=2;
    assert(request(tid,4)==0); peek(); assert(out[9]==42); done();
    uint8_t version[]={1,0x82,0,3,0,0,0,0,1,0};
    assert(request(version,sizeof(version))==0); peek();
    assert(outlen==23 && out[8]==0 && out[13]==5);
    const uint8_t v[]={0,0xf0,0xf1,0xf1,0xba,0xbe,0x9d,0x53};
    assert(!memcmp(out+14,v,8)); done();
    uint8_t commands[]={1,0x84,0,5,0,0,0xf0,0xf1,0xf1};
    assert(request(commands,sizeof(commands))==0); peek();
    assert(outlen==42 && out[8]==0 && out[9]==0x3e); done();
    commands[8]=0xf2;
    assert(request(commands,sizeof(commands))==0); peek(); assert(out[8]==0x84); done();
    uint8_t unknown[]={1,0x82,2,0xff};
    assert(request(unknown,sizeof(unknown))==0); peek();
    assert(out[6]==2 && out[8]==0x20); done();
    uint8_t longmsg[65]={1,0x82,0,4};
    uint8_t fragment[69]={1,9,8,0x8b}; /* 首包 64B，SOM=1 EOM=0 */
    memcpy(fragment+4,longmsg,64);
    fragment[68]=chip_endpoint_pec(0x88,fragment,68);
    assert(chip_endpoint_rx(0x44,fragment,69)==0);
    assert(chip_endpoint_tx_pending()==0);
    uint8_t last[]={1,9,8,0x5b,0,0}; /* seq=1 EOM=1 */
    last[5]=chip_endpoint_pec(0x88,last,5);
    assert(chip_endpoint_rx(0x44,last,6)==0); peek();
    assert(out[8]==3); done(); /* 重组后发现 PLDM payload 长度错误 */
    uint8_t seteid[]={0,0x81,1,0,10};
    assert(request(seteid,sizeof(seteid))==0);
    assert(chip_endpoint_tx_peek(0x44,out,sizeof(out),&outlen)==0);
    assert(out[2]==10 && out[8]==0 && out[9]==10); done();
    chip_endpoint_deinit();
    assert(chip_endpoint_init(0,1,now,NULL)==0); chip_endpoint_deinit();
    puts("PASS: PEC, MCTP Control, PLDM Base, version CRC, errors, short-read retry, reassembly, EID change");
}
