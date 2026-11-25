#include "data_driver.h"
#include <obs.h>
#include <util/platform.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <locale>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
static bool validUuid(const char *id){
    if(!id||std::strlen(id)!=36)return false;
    for(int i=0;i<36;++i){
        char c=id[i];
        if(i==8||i==13||i==18||i==23){if(c!='-')return false;}
        else if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return false;
    }
    return true;
}
static urlsource_snapshot_v1_fn resolveReader(){
    auto *module=obs_get_module("obs-urlsource");
    return module?reinterpret_cast<urlsource_snapshot_v1_fn>(os_dlsym(obs_get_module_lib(module),"urlsource_snapshot_v1")):nullptr;
}
static bool parseScalar(const char *data,size_t n,float &value){
    if(n<2||data[n-1]!='\0'||std::memchr(data,'\0',n-1))return false;
    if(!std::strcmp(data,"true")){value=1;return true;}
    if(!std::strcmp(data,"false")){value=0;return true;}
    if(data[0]!='-'&&(data[0]<'0'||data[0]>'9'))return false;
    // Provider emits valid JSON. Reject nonnumeric tokens and overflow;
    // use the classic locale regardless of the OBS UI locale.
    std::istringstream stream(std::string(data,n-1));
    stream.imbue(std::locale::classic());
    stream>>std::noskipws>>value;
    return !stream.fail()&&stream.eof()&&std::isfinite(value);
}
struct hs_data_driver {
    std::mutex mutex;
    std::condition_variable cv;
    std::thread thread;
    bool stop=false,valid=false;
    std::string uuid;
    int age=5000;
    uint64_t generation=0;
    float value=0;
    Clock::time_point deadline;
    urlsource_snapshot_v1_fn fixture=nullptr;
    void run(){
        std::unique_lock<std::mutex> lock(mutex);
        while(!stop){
            cv.wait(lock,[&]{return stop||!uuid.empty();});
            if(stop)break;
            const auto id=uuid;const int limit=age;const auto revision=generation;
            lock.unlock();
            const auto start=Clock::now();
            char buffer[128]={};uint64_t seq=0,elapsed=0;float next=0;
            bool ok=false;
            try{
                auto reader=fixture?fixture:resolveReader();
                const auto n=reader?reader(id.c_str(),buffer,sizeof buffer,&seq,&elapsed):0;
                ok=n&&n<=sizeof buffer&&seq&&elapsed<static_cast<uint64_t>(limit)&&parseScalar(buffer,n,next);
            }catch(...){ok=false;}
            lock.lock();
            if(generation==revision&&!stop){
                valid=ok;value=next;
                deadline=ok?start+std::chrono::milliseconds(limit-elapsed):start;
            }
            cv.wait_for(lock,100ms,[&]{return stop||generation!=revision;});
        }
    }
};
extern "C" hs_data_driver *hs_data_driver_create(void){
    try{return new hs_data_driver;}catch(...){return nullptr;}
}
#ifdef HS_DATA_DRIVER_TEST
extern "C" hs_data_driver *hs_data_driver_create_for_test(urlsource_snapshot_v1_fn reader){
    auto *d=hs_data_driver_create();if(d)d->fixture=reader;return d;
}
#endif
extern "C" void hs_data_driver_destroy(hs_data_driver *d){
    if(!d)return;
    {std::lock_guard<std::mutex> lock(d->mutex);d->stop=true;d->cv.notify_all();}
    if(d->thread.joinable())d->thread.join();
    delete d;
}
extern "C" void hs_data_driver_configure(hs_data_driver *d,const char *id,int age){
    if(!d)return;
    try{
        const std::string next=validUuid(id)?id:"";
        age=std::clamp(age,100,60000);
        std::lock_guard<std::mutex> lock(d->mutex);
        if(d->uuid==next&&d->age==age)return;
        d->uuid=next;d->age=age;d->valid=false;++d->generation;
        if(!next.empty()&&!d->thread.joinable()){
            try{d->thread=std::thread([d]{d->run();});}
            catch(...){d->uuid.clear();}
        }
        d->cv.notify_all();
    }catch(...){}
}
extern "C" bool hs_data_driver_read(hs_data_driver *d,const char *id,int age,float *value){
    if(!d||!id||!value)return false;
    std::unique_lock<std::mutex> lock(d->mutex,std::try_to_lock);
    if(!lock||!d->valid||d->uuid!=id||d->age!=std::clamp(age,100,60000)||Clock::now()>=d->deadline)return false;
    *value=d->value;return true;
}
