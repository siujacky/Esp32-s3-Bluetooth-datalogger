#pragma once
#include "FreeRTOS.h"
using QueueHandle_t=void *;
QueueHandle_t xQueueCreate(unsigned,size_t);
int xQueueSend(QueueHandle_t,const void *,unsigned);
int xQueueReceive(QueueHandle_t,void *,unsigned);
int xQueueOverwrite(QueueHandle_t,const void *);
void xQueueReset(QueueHandle_t);
void vQueueDelete(QueueHandle_t);
