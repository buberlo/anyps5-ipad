#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include <ctime>
#if CANDIDATE
#include "AudioOutFramePacing.hpp"
#endif
#define APS5_VABI
using Uint8=std::uint8_t;using Uint16=std::uint16_t;using SDL_AudioFormat=std::uint16_t;using SDL_AudioDeviceID=std::uint32_t;
constexpr SDL_AudioFormat AUDIO_F32SYS=33056,AUDIO_S16SYS=32784;
constexpr int SDL_INIT_AUDIO=16,SDL_AUDIO_ALLOW_ANY_CHANGE=15;
#define SDL_AUDIO_BITSIZE(x) ((x)&255)
#define SDL_AUDIO_ISFLOAT(x) (((x)&256)!=0)
struct SDL_AudioSpec {int freq=0;SDL_AudioFormat format=0;Uint8 channels=0;Uint16 samples=0;void(*callback)(void*,Uint8*,int)=nullptr;};
struct SDL_AudioCVT {int len_mult=2,len_cvt=0,len=0;Uint8*buf=nullptr;};
struct AudioOutOutputParam {int handle;const void*ptr;};
struct Device {std::deque<Uint8>bytes;std::uint64_t next=21333,remainder=16000,drains=0;SDL_AudioSpec spec{};};
static std::map<unsigned,Device> devices;
static std::uint64_t clockUs=0,sleepExtra=0;static int interruptedSleeps=0,shortSleeps=0,cvtMode=0,convertFail=0,queueFail=0,openFail=0;static bool drainEnabled=true;
static unsigned nextDevice=1;static std::uint64_t clockReads=0;
static std::vector<std::uint64_t> queuedTimes,rawTimes,sleeps;static std::vector<std::vector<Uint8>> queuedBytes;static std::vector<std::string> events,actions;
static void updateDrains() {
 for(auto&kv:devices){auto&d=kv.second;if(!drainEnabled)continue;while(clockUs>=d.next){++d.drains;auto n=std::min<std::size_t>(d.bytes.size(),std::size_t(d.spec.samples)*d.spec.channels*(SDL_AUDIO_BITSIZE(d.spec.format)/8));while(n--)d.bytes.pop_front();const auto numerator=std::uint64_t(d.spec.samples)*1000000;auto step=numerator/d.spec.freq;d.remainder+=numerator%d.spec.freq;if(d.remainder>=unsigned(d.spec.freq)){++step;d.remainder-=d.spec.freq;}d.next+=step;}}
}
static std::uint64_t sceKernelGetProcessTime(){++clockReads;return clockUs;}
static int fixture_sleep(const timespec*r,timespec*) {const auto us=std::uint64_t(r->tv_sec)*1000000+std::uint64_t(r->tv_nsec)/1000;sleeps.push_back(us);if(interruptedSleeps>0){--interruptedSleeps;return -1;}auto advance=us;if(shortSleeps>0){--shortSleeps;advance/=2;}clockUs+=advance+sleepExtra;updateDrains();return 0;}
#define nanosleep fixture_sleep
static int SDL_InitSubSystem(int){actions.push_back("init");return 0;}
static int SDL_WasInit(int){return SDL_INIT_AUDIO;}
static SDL_AudioDeviceID SDL_OpenAudioDevice(const char*,int,const SDL_AudioSpec*want,SDL_AudioSpec*out,int){if(openFail)return 0;*out=*want;out->samples=1024;auto id=nextDevice++;Device d;d.spec=*out;const auto num=std::uint64_t(out->samples)*1000000;d.next=clockUs+num/out->freq;d.remainder=num%out->freq;devices[id]=d;actions.push_back("open");return id;}
static void SDL_PauseAudioDevice(unsigned,int){actions.push_back("unpause");}
static void SDL_ClearQueuedAudio(unsigned id){devices[id].bytes.clear();actions.push_back("clear");}
static void SDL_CloseAudioDevice(unsigned id){devices.erase(id);actions.push_back("close");}
static std::uint32_t SDL_GetQueuedAudioSize(unsigned id){actions.push_back("size");updateDrains();return devices[id].bytes.size();}
static int SDL_BuildAudioCVT(SDL_AudioCVT*c,SDL_AudioFormat,Uint8,int,SDL_AudioFormat,Uint8,int){actions.push_back("build-cvt");c->len_mult=2;return cvtMode;}
static int SDL_ConvertAudio(SDL_AudioCVT*c){actions.push_back("convert");if(convertFail)return -1;c->len_cvt=c->len;return 0;}
static int SDL_QueueAudio(unsigned id,const void*p,std::uint32_t n){actions.push_back("queue");if(queueFail)return -1;const auto*b=static_cast<const Uint8*>(p);devices[id].bytes.insert(devices[id].bytes.end(),b,b+n);queuedTimes.push_back(clockUs);queuedBytes.emplace_back(b,b+n);return 0;}
static const char* SDL_GetError(){return "fixture-error";}
namespace AudioOutIngressTrace {
struct Stream{};struct Token{};struct Shape{std::uint32_t rate,channels,bits,code;bool floating;};struct Metadata{int port,type,gameFormat;std::uint32_t requestedFrames,device,obtainedSamples;Shape raw,queued;};
static bool Enabled(){return false;}static Stream*Register(const Metadata&){return nullptr;}
static Token Raw(Stream*,const void*,std::uint64_t){rawTimes.push_back(clockUs);return {};}
static void Queued(Token&,const void*,std::uint32_t,int,bool,bool){}static void Result(Token&,int){}
static void Event(Stream*,const char*reason,bool=false,bool=false,bool=false){events.push_back(reason);}
}
