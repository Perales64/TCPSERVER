#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cyabs_rtos.h"
#include "cy_wcm.h"
#include "cy_nw_helper.h"
#include <string.h>
#include <float.h>
#include <math.h>

#include <models/model1audio.h>
#include "config.h"
#include "ia.h"

/*******************************************************************************
 * DEEPCRAFT compatibility defines
 *******************************************************************************/
#ifndef IPWIN_RET_SUCCESS
#define IPWIN_RET_SUCCESS (0)
#endif
#ifndef IPWIN_RET_NODATA
#define IPWIN_RET_NODATA (-1)
#endif
#ifndef IPWIN_RET_ERROR
#define IPWIN_RET_ERROR (-2)
#endif
#ifndef IMAI_DATA_OUT_SYMBOLS
#define IMAI_DATA_OUT_SYMBOLS IMAI_SYMBOL_MAP
#endif

/*******************************************************************************
 * Static Variables
 *******************************************************************************/
static bool ml_initialized = false;
static float sample_max_slow = 0;

/*******************************************************************************
 * Static Function Prototypes
 *******************************************************************************/
static cy_rslt_t configure_audio_clocks(cyhal_clock_t *audio_clock, cyhal_clock_t *pll_clock);
static void pdm_frequency_fix(void);
static float normalize_and_boost_sample(int16_t sample);

/*******************************************************************************
 * Function Name: tarea_ia
 ********************************************************************************
 * Summary:
 * Main IA task that initializes audio system, ML model and continuously
 * processes audio data for ML inference.
 *
 * Parameters:
 *  arg - Task parameter (unused)
 *
 * Return:
 *  void
 *******************************************************************************/
void tarea_ia(void *arg)
{
    cyhal_pdm_pcm_t pdm_pcm;
    ml_result_t ml_result;
    cy_rslt_t result;

    /* Initialize ML model */
    result = init_ml_model();
    if (result != CY_RSLT_SUCCESS)
    {
        printf("ERROR: No se pudo inicializar el modelo ML (0x%08lX)\n", result);
        vTaskDelete(NULL);
        return;
    }

    /* Initialize audio system */
    result = init_audio_system(&pdm_pcm);
    if (result != CY_RSLT_SUCCESS)
    {
        printf("ERROR: No se pudo inicializar el sistema de audio (0x%08lX)\n", result);
        vTaskDelete(NULL);
        return;
    }

    /* Main processing loop */
    while (true)
    {
        /* Process audio and get ML results */
        result = process_audio_buffer(&pdm_pcm, &ml_result);

        if (result == CY_RSLT_SUCCESS && ml_result.detection_active)
        {
            /* Print results */
            print_ml_results(&ml_result);

            /* Check if trigger condition is met */
            if (check_ml_trigger(&ml_result))
            {
                printf(">>> COMANDO DETECTADO: %s (Confianza: %.3f) <<<\n\n",
                       ml_result.labels[ml_result.best_label], ml_result.max_score);

                /* Here you could add code to send notification to TCP server */
                /* or perform any other action when command is detected */
            }
        }

        /* Small delay to prevent CPU saturation */
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/*******************************************************************************
 * Function Name: init_ml_model
 ********************************************************************************
 * Summary:
 * Initializes the ML model for audio processing.
 *
 * Return:
 *  cy_rslt_t - Result of initialization
 *******************************************************************************/
cy_rslt_t init_ml_model(void)
{
    cy_rslt_t result = IMAI_init();

    if (result == CY_RSLT_SUCCESS)
    {
        ml_initialized = true;
    }
    else
    {
        printf("ERROR: Fallo en inicializaciÃ³n del modelo ML\n");
    }

    return result;
}

/*******************************************************************************
 * Function Name: init_audio_system
 ********************************************************************************
 * Summary:
 * Initializes the complete audio system including clocks and PDM/PCM.
 *
 * Parameters:
 *  pdm_pcm - Pointer to PDM/PCM structure
 *
 * Return:
 *  cy_rslt_t - Result of initialization
 *******************************************************************************/
cy_rslt_t init_audio_system(cyhal_pdm_pcm_t *pdm_pcm)
{
    cy_rslt_t result;
    cyhal_clock_t audio_clock, pll_clock;

    /* Configure audio clocks */
    result = configure_audio_clocks(&audio_clock, &pll_clock);
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    /* Configure PDM/PCM */
    const cyhal_pdm_pcm_cfg_t pdm_pcm_cfg = {
        .sample_rate = SAMPLE_RATE_HZ,
        .decimation_rate = DECIMATION_RATE,
        .mode = CYHAL_PDM_PCM_MODE_LEFT,
        .word_length = AUIDO_BITS_PER_SAMPLE,
        .left_gain = MICROPHONE_GAIN,
        .right_gain = MICROPHONE_GAIN,
    };

    /* Initialize PDM/PCM */
    result = cyhal_pdm_pcm_init(pdm_pcm, PDM_DATA, PDM_CLK, &audio_clock, &pdm_pcm_cfg);
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    /* Clear PDM/PCM RX FIFO */
    result = cyhal_pdm_pcm_clear(pdm_pcm);
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    /* Apply frequency fix workaround */
    pdm_frequency_fix();

    /* Start PDM/PCM */
    result = cyhal_pdm_pcm_start(pdm_pcm);
    if (result != CY_RSLT_SUCCESS)
    {
        return result;
    }

    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
 * Function Name: process_audio_buffer
 ********************************************************************************
 * Summary:
 * Processes audio buffer through ML model and returns results.
 *
 * Parameters:
 *  pdm_pcm - Pointer to PDM/PCM structure
 *  result  - Pointer to ML result structure
 *
 * Return:
 *  cy_rslt_t - Result of processing
 *******************************************************************************/
cy_rslt_t process_audio_buffer(cyhal_pdm_pcm_t *pdm_pcm, ml_result_t *result)
{
    static int16_t audio_buffer[AUDIO_BUFFER_SIZE] = {0};
    static float label_scores[IMAI_DATA_OUT_COUNT];
    static char *label_text[] = IMAI_DATA_OUT_SYMBOLS;

    size_t audio_count;
    cy_rslt_t cy_result;
    float sample, sample_abs, sample_max = 0;
    int ml_status;

    /* Initialize result structure */
    result->scores = label_scores;
    result->labels = label_text;
    result->count = IMAI_DATA_OUT_COUNT;
    result->detection_active = false;
    result->best_label = 0;
    result->max_score = -1000.0f;

    /* Read audio data */
    audio_count = AUDIO_BUFFER_SIZE;
    memset(audio_buffer, 0, AUDIO_BUFFER_SIZE * sizeof(int16_t));
    cy_result = cyhal_pdm_pcm_read(pdm_pcm, (void *)audio_buffer, &audio_count);

    if (cy_result != CY_RSLT_SUCCESS)
    {
        return cy_result;
    }

    /* Update volume tracking */
    sample_max_slow -= 0.0005f;

    /* Process each sample */
    for (int i = 0; i < audio_count; i++)
    {
        /* Normalize and boost sample */
        sample = normalize_and_boost_sample(audio_buffer[i]);

        /* Enqueue sample to ML model */
        cy_result = IMAI_enqueue(&sample);
        if (cy_result != CY_RSLT_SUCCESS)
        {
            return cy_result;
        }

        /* Track maximum sample for volume monitoring */
        sample_abs = fabs(sample);
        if (sample_abs > sample_max)
        {
            sample_max = sample_abs;
        }

        if (sample_max > sample_max_slow)
        {
            sample_max_slow = sample_max;
        }

        /* Check for ML model output */
        ml_status = IMAI_dequeue(label_scores);

        switch (ml_status)
        {
        case IMAI_RET_SUCCESS:
            /* Find best label */
            for (int j = 0; j < IMAI_DATA_OUT_COUNT; j++)
            {
                if (label_scores[j] > result->max_score)
                {
                    result->max_score = label_scores[j];
                    result->best_label = j;
                }
            }
            result->detection_active = true;
            break;

        case IMAI_RET_NODATA:
            /* No new output, continue processing */
            break;

        case IMAI_RET_ERROR:
            return CY_RSLT_TYPE_ERROR;

        default:
            break;
        }
    }

    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
 * Function Name: check_ml_trigger
 ********************************************************************************
 * Summary:
 * Checks if ML detection meets trigger conditions.
 *
 * Parameters:
 *  result - Pointer to ML result structure
 *
 * Return:
 *  bool - true if trigger condition is met
 *******************************************************************************/
bool check_ml_trigger(ml_result_t *result)
{
    if (!result->detection_active)
    {
        return false;
    }

    /* Check if best label is the target label and confidence is above threshold */
    return (result->best_label == ML_TRIGGER_LABEL_INDEX &&
            result->max_score >= ML_TRIGGER_THRESHOLD);
}

/*******************************************************************************
 * Function Name: print_ml_results
 ********************************************************************************
 * Summary:
 * Prints ML detection results to console.
 *
 * Parameters:
 *  result - Pointer to ML result structure
 *******************************************************************************/
void print_ml_results(ml_result_t *result)
{
    static int16_t prev_best_label = 0;
    int16_t final_best_label = result->best_label;
    printf("\x1b[36m");
    /* Print all label scores */
    printf("\r--- Resultados ML ---");

    /* Post-processing for stable detection */
    if (prev_best_label != 0 && result->scores[prev_best_label] > 0.05f)
    {
        final_best_label = prev_best_label;
    }
    else if (result->best_label != 0 && result->max_score >= 0.50f)
    {
        prev_best_label = result->best_label;
    }

    printf("\nDetectado: %s %.1f%%\n",
           result->labels[final_best_label],
           result->scores[final_best_label]*100);
    printf("Volumen: %.4f (%.2f)\n", sample_max_slow * 0.8f, sample_max_slow);
    printf("------------------------\n\n");
    for (int icursor = 0; icursor < 5; icursor++)
    {
        printf("\033[1A");
        printf("\033[2K");
    }
}

/*******************************************************************************
 * Function Name: cleanup_audio_system
 ********************************************************************************
 * Summary:
 * Cleans up audio system resources.
 *
 * Parameters:
 *  pdm_pcm - Pointer to PDM/PCM structure
 *******************************************************************************/
void cleanup_audio_system(cyhal_pdm_pcm_t *pdm_pcm)
{
    cyhal_pdm_pcm_stop(pdm_pcm);
    cyhal_pdm_pcm_free(pdm_pcm);
    printf("Sistema de audio liberado\n");
}

/*******************************************************************************
 * Static Functions Implementation
 *******************************************************************************/

/*******************************************************************************
 * Function Name: configure_audio_clocks
 ********************************************************************************
 * Summary:
 * Configures PLL and audio clocks for PDM/PCM operation.
 *******************************************************************************/
static cy_rslt_t configure_audio_clocks(cyhal_clock_t *audio_clock, cyhal_clock_t *pll_clock)
{
    cy_rslt_t result;

    /* Initialize PLL */
    result = cyhal_clock_reserve(pll_clock, &CYHAL_CLOCK_PLL[1]);
    if (result != CY_RSLT_SUCCESS)
        return result;

    result = cyhal_clock_set_frequency(pll_clock, AUDIO_SYS_CLOCK_HZ, NULL);
    if (result != CY_RSLT_SUCCESS)
        return result;

    result = cyhal_clock_set_enabled(pll_clock, true, true);
    if (result != CY_RSLT_SUCCESS)
        return result;

    /* Initialize audio subsystem clock */
    result = cyhal_clock_reserve(audio_clock, &CYHAL_CLOCK_HF[1]);
    if (result != CY_RSLT_SUCCESS)
        return result;

    result = cyhal_clock_set_source(audio_clock, pll_clock);
    if (result != CY_RSLT_SUCCESS)
        return result;

    result = cyhal_clock_set_enabled(audio_clock, true, true);
    if (result != CY_RSLT_SUCCESS)
        return result;

    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
 * Function Name: pdm_frequency_fix
 ********************************************************************************
 * Summary:
 * Workaround to apply correct clock frequency to the microphone.
 *******************************************************************************/
static void pdm_frequency_fix(void)
{
    static uint32_t *pdm_reg = (uint32_t *)(0x40A00010);
    uint32_t clk_clock_div_stage_1 = 2;
    uint32_t mclkq_clock_div_stage_2 = 1;
    uint32_t cko_clock_div_stage_3 = 8;

    uint32_t needed_sinc_rate = AUDIO_SYS_CLOCK_HZ / (clk_clock_div_stage_1 *
                                                      mclkq_clock_div_stage_2 * cko_clock_div_stage_3 * 2 * SAMPLE_RATE_HZ);

    uint32_t pdm_data = (clk_clock_div_stage_1 - 1) << 0;
    pdm_data |= (mclkq_clock_div_stage_2 - 1) << 4;
    pdm_data |= (cko_clock_div_stage_3 - 1) << 8;
    pdm_data |= needed_sinc_rate << 16;

    *pdm_reg = pdm_data;
}

/*******************************************************************************
 * Function Name: normalize_and_boost_sample
 ********************************************************************************
 * Summary:
 * Normalizes and applies digital boost to audio sample.
 *******************************************************************************/
static float normalize_and_boost_sample(int16_t sample)
{
    float normalized = SAMPLE_NORMALIZE(sample) * DIGITAL_BOOST_FACTOR;

    /* Clip to valid range */
    if (normalized > 1.0f)
    {
        normalized = 1.0f;
    }
    else if (normalized < -1.0f)
    {
        normalized = -1.0f;
    }

    return normalized;
}