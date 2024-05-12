#ifndef EVENT_SOURCE_H_
#define EVENT_SOURCE_H_

#include "esp_event.h"
#include "esp_timer.h"

#ifdef __cplusplus
extern "C" {
#endif


// Declare an event base
ESP_EVENT_DECLARE_BASE(FTM_EVENT);
enum
{
    FTM_LOOP_START_EVENT,
};

ESP_EVENT_DECLARE_BASE(CSI_EVENT);
enum
{
    CSI_LISTEN_START_EVENT, 
};


#ifdef __cplusplus
}
#endif

#endif // #ifndef EVENT_SOURCE_H_