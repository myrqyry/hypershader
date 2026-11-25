#include "interop/data_driver.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
using namespace std::chrono_literals;
static std::atomic<int> mode{0},calls{0};
static const char *uuid="aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa";
static const char *other="bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb";
static size_t reader(const char *id,char *buf,size_t cap,uint64_t *seq,uint64_t *age){
 ++calls;
 if(strcmp(id,uuid))return 0;
 const char *value=mode==0?"0.75":mode==1?"\"0.75\"":mode==2?"true":mode==3?"null":mode==4?"1e999":"-2.5";
 size_t n=strlen(value)+1;
 if(cap>=n)memcpy(buf,value,n);
 *seq=1;*age=mode==5?1000:0;return n;
}
int main(){
 auto *d=hs_data_driver_create_for_test(reader);
 assert(d);
 float value=99;
 assert(!hs_data_driver_read(d,uuid,500,&value));
 assert(calls==0);
 hs_data_driver_configure(d,uuid,500);
 auto settle=[](){std::this_thread::sleep_for(260ms);};
 settle();assert(hs_data_driver_read(d,uuid,500,&value)&&value==.75f);
 assert(!hs_data_driver_read(d,other,500,&value));
 assert(!hs_data_driver_read(d,uuid,600,&value));
 mode=1;settle();assert(!hs_data_driver_read(d,uuid,500,&value));
 mode=2;settle();assert(hs_data_driver_read(d,uuid,500,&value)&&value==1);
 mode=3;settle();assert(!hs_data_driver_read(d,uuid,500,&value));
 mode=4;settle();assert(!hs_data_driver_read(d,uuid,500,&value));
 mode=5;settle();assert(!hs_data_driver_read(d,uuid,500,&value));
 hs_data_driver_configure(d,other,500);assert(!hs_data_driver_read(d,uuid,500,&value));
 settle();assert(!hs_data_driver_read(d,other,500,&value));
 hs_data_driver_configure(d,"",500);settle();auto n=calls.load();settle();assert(calls==n);
 hs_data_driver_destroy(d);
 std::cout<<"DATA_DRIVER_PASS=1\n";
}
