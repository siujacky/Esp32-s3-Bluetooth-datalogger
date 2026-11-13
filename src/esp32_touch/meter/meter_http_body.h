#pragma once
#include <cstddef>
#include <cstdint>
#include <algorithm>
namespace meter_http {
enum class BodyResult {Complete,TooLarge,Timeout,Disconnected,NoMemory};
// Read exactly the advertised body. In particular, never ask Stream::readBytes
// for a full1436-byte raw buffer when only a short JSON body remains.
template<class Client,class Clock,class Pause,class Append>
BodyResult readBody(Client &client,size_t expected,size_t maximum,uint32_t timeoutMs,Clock now,Pause pause,Append append,size_t &received){
    received=0;if(expected>maximum)return BodyResult::TooLarge;
    const uint32_t started=now();uint8_t buffer[129];
    while(received<expected){
        if(uint32_t(now()-started)>=timeoutMs)return BodyResult::Timeout;
        const int available=client.available();
        if(available<=0){if(!client.connected())return BodyResult::Disconnected;pause();continue;}
        const size_t wanted=std::min(sizeof(buffer)-1,std::min(expected-received,size_t(available)));
        const int count=client.read(buffer,wanted);
        if(count<=0){if(!client.connected())return BodyResult::Disconnected;pause();continue;}
        // Arduino String::concat(ptr,n) also copies ptr[n]. Supply a defined
        // NUL terminator without changing the advertised byte count.
        buffer[size_t(count)]=0;
        if(!append(buffer,size_t(count)))return BodyResult::NoMemory;
        received+=size_t(count);
    }
    return BodyResult::Complete;
}
}
