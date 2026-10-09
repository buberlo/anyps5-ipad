static int assertions=0;
static void check(bool value,const char*label){++assertions;if(!value)throw std::runtime_error(label);}
static void reset(){clockUs=0;sleepExtra=0;interruptedSleeps=shortSleeps=cvtMode=convertFail=queueFail=openFail=0;drainEnabled=true;nextDevice=1;clockReads=0;devices.clear();queuedTimes.clear();rawTimes.clear();sleeps.clear();queuedBytes.clear();events.clear();actions.clear();g_sdlInitialized=false;for(auto&p:g_ports)p=Port{};}
static std::array<float,512> pcm(){std::array<float,512> a{};for(unsigned i=0;i<a.size();++i)a[i]=i%2?-.5f:.25f;return a;}
static int openPort(unsigned rate=48000){auto h=sceAudioOutOpen(0,0,0,256,rate,4);check(h>0,"open");return h;}
static void output(int h,const void*p){check(sceAudioOutOutput(h,p)==256,"positive256");}
static bool hasEvent(const char*s){return std::find(events.begin(),events.end(),s)!=events.end();}
static void ordinary(bool paced){
 reset();auto data=pcm();int h=openPort();for(int i=0;i<100;++i)output(h,data.data());check(queuedTimes.size()==100,"hundred enqueues");check(g_ports[h-1].lastDataOutputTime==clockUs,"output time");check(queuedBytes[0].size()==2048,"queue size");check(std::memcmp(queuedBytes[0].data(),data.data(),2048)==0,"unchangedPCM");
 for(unsigned i=21;i<queuedTimes.size();++i){auto delta=queuedTimes[i]-queuedTimes[i-1];if(paced)check(delta>=4000&&delta<=7000,"steady no burst under existing1ms guard");}
 if(paced){for(int i=0;i<9;++i)check(queuedTimes[i]==0,"bounded nine startup");for(unsigned i=21;i<rawTimes.size();++i)check(rawTimes[i]-queuedTimes[i-1]>=4000,"steady cadence wait before raw");}
 std::vector<unsigned> bursts;unsigned burst=1;for(unsigned i=21;i<queuedTimes.size();++i){if(queuedTimes[i]-queuedTimes[i-1]>=1000){bursts.push_back(burst);burst=1;}else ++burst;}bursts.push_back(burst);if(paced)check(std::all_of(bursts.begin(),bursts.end(),[](unsigned v){return v==1;}),"paced four-to-one");else check(std::count(bursts.begin(),bursts.end(),4)>=15,"legacy fourpacket bursts demonstrated");
 if(paced)check((queuedTimes.back()-queuedTimes[20])/double(queuedTimes.size()-21)>5200&&(queuedTimes.back()-queuedTimes[20])/double(queuedTimes.size()-21)<5500,"rational steady mean");
 // Serialize all baseline-observable mock operations for byte/SDL/wait/return parity.
 std::cout<<"PARITY ";for(auto v:queuedTimes)std::cout<<v<<',';std::cout<<" clocks="<<clockReads<<" sleeps=";for(auto v:sleeps)std::cout<<v<<',';std::cout<<" actions=";for(auto&v:actions)std::cout<<v<<',';std::cout<<" bytes=";for(auto v:queuedBytes[0])std::cout<<int(v)<<',';std::cout<<'\n';
}
static void fixtures(bool paced){
 reset();auto data=pcm();int h=openPort();g_ports[h-1].volume[0]=16384;output(h,data.data());float first=0;std::memcpy(&first,queuedBytes.back().data(),4);check(first==.125f,"actual volume preparation");
 cvtMode=1;output(h,data.data());check(std::find(actions.begin(),actions.end(),"convert")!=actions.end(),"CVT retained");
 cvtMode=-1;bool threw=false;try{output(h,data.data());}catch(const std::runtime_error&){threw=true;}check(threw&&hasEvent("cvt-build-failure"),"CVT build failure");cvtMode=1;convertFail=1;threw=false;try{output(h,data.data());}catch(const std::runtime_error&){threw=true;}check(threw&&hasEvent("cvt-convert-failure"),"CVT convert failure");convertFail=0;cvtMode=0;queueFail=1;threw=false;try{output(h,data.data());}catch(const std::runtime_error&){threw=true;}check(threw,"queue failure throws");queueFail=0;
#if CANDIDATE
 if(paced){check(g_ports[h-1].framePacing.startupPackets==7,"failure does not consume or refill startup");check(!g_ports[h-1].framePacing.armed,"failure resets timeline");}
#endif
 output(h,nullptr);check(hasEvent("null-drain"),"null drain retained");
#if CANDIDATE
 if(paced)check(g_ports[h-1].framePacing.startupPackets==7,"drain keeps residual startup only");
#endif
 check(sceAudioOutClose(h)==0,"close");check(!g_ports[h-1].used,"port closed");
 reset();h=openPort();for(int i=0;i<25;++i)output(h,data.data());clockUs+=80000;updateDrains();output(h,data.data());auto afterStall=clockUs;output(h,data.data());if(paced)check(clockUs-afterStall>=5333,"stall no catchup");
 interruptedSleeps=2;shortSleeps=1;output(h,data.data());if(paced)check(interruptedSleeps==0,"interrupted sleeps retried");interruptedSleeps=shortSleeps=0;
 drainEnabled=false;for(int i=0;i<20;++i)output(h,data.data());check(hasEvent("pacing-timeout-clear"),"old timeout clear retained");
 reset();int a=openPort(),b=openPort();AudioOutOutputParam two[]={{a,data.data()},{b,data.data()}};for(int i=0;i<30;++i)check(sceAudioOutOutputs(two,2)==256,"multiport positive256");auto begin=clockUs;check(sceAudioOutOutputs(two,2)==256,"multiport steady");if(paced)check(clockUs-begin<=5334,"same-rate batch no doublegrain");
 AudioOutOutputParam duplicate[]={{a,data.data()},{a,data.data()}};begin=clockUs;check(sceAudioOutOutputs(duplicate,2)==256,"duplicate handles");if(paced)check(clockUs-begin>=10666,"duplicate consumes two grains");check(sceAudioOutOutputs(nullptr,1)<0,"null params");AudioOutOutputParam invalid[]={{99,data.data()}};auto queues=queuedTimes.size();check(sceAudioOutOutputs(invalid,1)<0&&queuedTimes.size()==queues,"invalid handle no work");
 reset();a=openPort(48000);b=openPort(24000);for(int i=0;i<25;++i)check(sceAudioOutOutputs((two[0]={a,data.data()},two[1]={b,data.data()},two),2)==256,"different rate batch");if(paced){auto before=clockUs;output(b,data.data());check(clockUs-before>=10666,"different rate period");}
 reset();a=openPort();openFail=1;b=openPort();openFail=0;AudioOutOutputParam mixed[]={{a,data.data()},{b,data.data()}};for(int i=0;i<20;++i)check(sceAudioOutOutputs(mixed,2)==256,"device plus virtual batch");check(queuedTimes.size()==20,"one real enqueue per mixed batch");auto beforeNullTime=g_ports[a-1].lastDataOutputTime;AudioOutOutputParam nullData[]={{a,nullptr},{b,data.data()}};check(sceAudioOutOutputs(nullData,2)==256,"null plus data batch");check(g_ports[a-1].lastDataOutputTime==beforeNullTime&&g_ports[b-1].lastDataOutputTime==clockUs,"null timestamp unchanged data timestamp updated");
 reset();openFail=1;h=openPort();check(g_ports[h-1].device==0,"virtual fallback");for(int i=0;i<30;++i)output(h,data.data());check(queuedTimes.empty()&&rawTimes.empty(),"virtual never real enqueue");check(clockUs>0,"virtual old pace");output(h,nullptr);check(sceAudioOutClose(h)==0,"virtual close");
}
#if CANDIDATE
static void helperFixtures(){
 using AudioOutFramePacing::Cadence;using AudioOutFramePacing::EnabledValue;
 check(!EnabledValue(nullptr)&&!EnabledValue("")&&!EnabledValue("0")&&!EnabledValue("01")&&!EnabledValue("true")&&!EnabledValue("1 ")&&!EnabledValue(" 1")&&EnabledValue("1"),"exact gate parsing");
 Cadence c;c.Configure(256,48000,40000);check(c.startupPackets==9,"original9packet headroom");for(int i=0;i<9;++i)c.Commit(0);check(c.deadlineUs==5333,"first rational step");c.Commit(5333);check(c.deadlineUs==10666,"second rational step");c.Commit(10666);check(c.deadlineUs==16000,"third rational step");for(int i=0;i<3000;++i)c.Commit(c.deadlineUs);check(c.deadlineUs==16016000,"zero floor drift");
 auto previous=c.deadlineUs;c.Commit(previous+75);check(c.deadlineUs==previous+5333,"minor oversleep keeps phase");c.Commit(c.deadlineUs+5334);auto anchor=c.lastCommitUs;check(c.deadlineUs-anchor>=5333&&c.deadlineUs-anchor<=5334,"one missed grain reanchor");check(c.Remaining(anchor)>=5333,"no catchup after reanchor");check(c.Remaining(anchor-1)==0,"backward clock no huge wait");c.Commit(anchor-1);check(c.deadlineUs>anchor-1,"rollback reanchor");
 c.ResetTimeline();check(!c.armed&&c.startupPackets==0,"reset no new warmup");c.Commit(123);check(c.Remaining(123)==5333,"reset next onegrain");c.Configure(256,48000,40000);c.Commit(0);c.ResetTimeline();check(c.startupPackets==8,"early reset remaining budget");
 c.Configure(0,48000,40000);check(!c.Configured(),"zero frames");c.Configure(256,0,40000);check(!c.Configured(),"zero rate");c.Configure(1,std::numeric_limits<std::uint32_t>::max(),40000);check(!c.Configured(),"submicrosecond legacy fallback");c.Configure(std::numeric_limits<std::uint32_t>::max(),1,std::numeric_limits<std::uint64_t>::max());check(c.wholeUs==4294967295000000ULL&&c.startupPackets>=3&&c.startupPackets<=17,"wide checked arithmetic");for(int i=0;i<17;++i)c.Commit(std::numeric_limits<std::uint64_t>::max()-1);check(!c.armed&&c.Remaining(0)==0,"deadline overflow failclosed");
}
static std::uint64_t exactPeriod(unsigned rate,std::uint64_t grains,std::uint32_t frames=256){
 // Independent closed-form wide reference, not the production carry loop.
 return static_cast<std::uint64_t>((static_cast<__uint128_t>(grains)*frames*1000000)/rate);
}
static void cachedGateFixture(bool paced){
 check(setenv("APS5_AUDIOOUT_FRAME_PACING",paced?"0":"1",1)==0,"setenv opposite");
 check(AudioOutFramePacing::Enabled()==paced,"actual cached gate survives opposite env");
 reset();int h=openPort();check(g_ports[h-1].framePacing.Configured()==paced,"actual open uses cached gate");check(sceAudioOutClose(h)==0,"cache fixture close");
 check(setenv("APS5_AUDIOOUT_FRAME_PACING","true",1)==0,"setenv malformed");check(AudioOutFramePacing::Enabled()==paced,"cache survives malformed env");
 check(unsetenv("APS5_AUDIOOUT_FRAME_PACING")==0,"unsetenv");check(AudioOutFramePacing::Enabled()==paced,"cache survives unset env");
 check(setenv("APS5_AUDIOOUT_FRAME_PACING",paced?"1":"0",1)==0,"restore env");
}
static void rational44100Fixture(std::uint32_t frames){
 AudioOutFramePacing::Cadence c;c.Configure(frames,44100,40000);check(c.Configured(),"44k1 helper configured");
 const auto startup=c.startupPackets;for(unsigned i=0;i<startup;++i)c.Commit(0);
 constexpr std::uint64_t grains=5000;
 for(std::uint64_t k=1;k<=grains;++k){
  const auto expected=exactPeriod(44100,k,frames);
  check(c.deadlineUs==expected,"44k1 exact closed-form deadline");
  const auto referenceRemainder=static_cast<std::uint64_t>((static_cast<__uint128_t>(k)*frames*1000000)%44100);
  check(c.remainder==referenceRemainder,"44k1 exact closed-form remainder");
  if(k<grains)c.Commit(c.deadlineUs);
 }
 check(c.deadlineUs>grains*c.wholeUs,"44k1 accumulated fraction retained");
 std::cout<<"RATIONAL rate=44100 frames="<<frames<<" grains="<<grains<<" exactUs="<<c.deadlineUs<<" floorDriftPreventedUs="<<c.deadlineUs-grains*c.wholeUs<<'\n';
}
static void longRunFixture(unsigned rate){
 reset();auto data=pcm();int h=openPort(rate);auto&cadence=g_ports[h-1].framePacing;const auto startup=cadence.startupPackets;
 check(startup>=3&&startup<=17,"longrun bounded startup");
 constexpr unsigned outputs=2000;std::uint64_t anchor=0,maxJitter=0,minDelta=std::numeric_limits<std::uint64_t>::max(),maxDelta=0;unsigned subMillisecond=0;
 for(unsigned i=0;i<outputs;++i){
  output(h,data.data());
  if(i<startup){check(queuedTimes.back()==0,"longrun finite startup only");continue;}
  if(i==startup)anchor=queuedTimes.back();
  const auto grains=i-startup;const auto expected=anchor+exactPeriod(rate,grains);
  check(queuedTimes.back()>=expected,"longrun no early admission relative to rational timeline");
  const auto jitter=queuedTimes.back()-expected;check(jitter<=1000,"longrun cumulative jitter bounded by retained1ms guard");maxJitter=std::max(maxJitter,jitter);
  check(cadence.deadlineUs==anchor+exactPeriod(rate,grains+1),"longrun actual port exact rational deadline no floor drift");
  check(cadence.remainder==static_cast<std::uint64_t>((static_cast<__uint128_t>(grains+1)*256000000)%rate),"longrun actual port rational remainder");
  if(i>startup){const auto delta=queuedTimes[i]-queuedTimes[i-1];if(delta<1000)++subMillisecond;minDelta=std::min(minDelta,delta);maxDelta=std::max(maxDelta,delta);check(delta>=4000&&delta<=7000,"longrun steady4..7ms no catchup or fourpacket bursts");}
 }
 check(queuedTimes.size()==outputs&&rawTimes.size()==outputs,"two thousand actual Output calls");check(clockUs>=10000000,"at least ten seconds fakeclock");
 check(subMillisecond==0,"longrun no steady sub1ms bursts");check(devices[g_ports[h-1].device].spec.samples==1024&&devices[g_ports[h-1].device].drains>=400,"periodic actual mock SDL1024 drain");
 check(!hasEvent("pacing-timeout-clear"),"longrun no timeout/reset masking drift");
 const auto referenceEnd=anchor+exactPeriod(rate,outputs-startup-1);check(clockUs>=referenceEnd&&clockUs-referenceEnd<=1000,"longrun final cumulative timing bound");
 const auto accumulatedFraction=exactPeriod(rate,outputs-startup)-std::uint64_t(outputs-startup)*cadence.wholeUs;check(accumulatedFraction>500,"reference distinguishes floor-only progression");
 std::cout<<"LONGRUN rate="<<rate<<" outputs="<<outputs<<" fakeUs="<<clockUs<<" startup="<<startup<<" anchorUs="<<anchor<<" steadyMinUs="<<minDelta<<" steadyMaxUs="<<maxDelta<<" maxJitterUs="<<maxJitter<<" sub1ms="<<subMillisecond<<" drains="<<devices[g_ports[h-1].device].drains<<" floorDriftPreventedUs="<<accumulatedFraction<<'\n';
 check(sceAudioOutClose(h)==0,"longrun close");
}
#endif
int main(int argc,char**argv){try{const bool paced=argc==2&&std::string(argv[1])=="1";
#if !CANDIDATE
 check(!paced,"baseline only gateoff");
#else
 helperFixtures();check(AudioOutFramePacing::Enabled()==paced,"actual cached gate");cachedGateFixture(paced);
#endif
 ordinary(paced);fixtures(paced);
#if CANDIDATE
 rational44100Fixture(256);rational44100Fixture(std::numeric_limits<std::uint32_t>::max());if(paced){longRunFixture(48000);longRunFixture(44100);}
#endif
 std::cout<<"PASS assertions="<<assertions<<" paced="<<paced<<'\n';return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
