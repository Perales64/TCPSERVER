#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cyabs_rtos.h"
#include <FreeRTOS.h>
#include <task.h>
#include "control.h"

void control(void *arg)
{
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(5000)); // Delay for 5 secondsor 1 second
    }
}
