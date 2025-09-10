#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cyabs_rtos.h"
#include <FreeRTOS.h>
#include <task.h>
#include "control.h"
#include "config.h"

void control(void *arg)
{
    typedef struct
    {
        const char *cmd;
        uint8_t output_num; // 0 = all outputs, 1-4 = specific output
        bool state;         // true = ON, false = OFF
        bool is_status_cmd;
    } command_entry_t;

    // Tabla de comandos optimizada
    static const command_entry_t COMMAND_TABLE[] = {
        {"1_ON", 1, true, false},
        {"1_OFF", 1, false, false},
        {"2_ON", 2, true, false},
        {"2_OFF", 2, false, false},
        {"3_ON", 3, true, false},
        {"3_OFF", 3, false, false},
        {"4_ON", 4, true, false},
        {"4_OFF", 4, false, false},
        {"ALL_ON", 0, true, false},
        {"ALL_OFF", 0, false, false},
        {"STATUS", 0, false, true}};
    typedef struct
    {
        cyhal_gpio_t pin;
        bool state;
    } gpio_output_t;

    static gpio_output_t outputs[NUM_OUTPUTS];
    // Pin definitions - más fácil de modificar
    static const cyhal_gpio_t OUTPUT_PINS[NUM_OUTPUTS] = {OUT1, OUT2, OUT3, OUT4};
    for (int i = 0; i < NUM_OUTPUTS; i++)
    {
        outputs[i].pin = OUTPUT_PINS[i];
        outputs[i].state = false;

        cy_rslt_t result = cyhal_gpio_init(outputs[i].pin, CYHAL_GPIO_DIR_OUTPUT,
                                           CYHAL_GPIO_DRIVE_STRONG, false);

        if (result != CY_RSLT_SUCCESS)
        {
            printf("Error inicializando salida %d: 0x%lX\n", i + 1, result);
            return result;
        }
    }

    for (;;)
    {

        vTaskDelay(pdMS_TO_TICKS(5000)); // Delay for 5 secondsor 1 second
    }
}
