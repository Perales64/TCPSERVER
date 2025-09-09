#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cyabs_rtos.h"
#include "cy_wcm.h"
#include "cy_nw_helper.h"
#include <string.h>

#include <models/model.h>
#include "config.h"
#include "ia.h"


void tarea_ia(void *arg)
{
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Espera 1 segundo
    }
    
}