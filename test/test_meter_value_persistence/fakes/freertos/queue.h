#pragma once
#include <deque>
#include <vector>
#include <cstring>
struct FakeQueue {size_t capacity,size;std::deque<std::vector<unsigned char>> items;};
using QueueHandle_t=FakeQueue*;
inline QueueHandle_t xQueueCreate(size_t n,size_t size){return new FakeQueue{n,size,{}};}
inline int xQueueSend(QueueHandle_t q,const void *in,unsigned){if(q->items.size()==q->capacity)return 0;const auto b=(const unsigned char*)in;q->items.push_back(std::vector<unsigned char>(b,b+q->size));return pdTRUE;}
inline int xQueueReceive(QueueHandle_t q,void *out,unsigned){if(q->items.empty())return 0;memcpy(out,q->items.front().data(),q->size);q->items.pop_front();return pdTRUE;}
inline unsigned uxQueueMessagesWaiting(QueueHandle_t q){return q->items.size();}
