#ifndef IA_TASK_H_
#define IA_TASK_H_

#include "cyhal.h"
#include <stdbool.h>

/*******************************************************************************
* Structures
*******************************************************************************/
typedef struct {
    float *scores;
    char **labels;
    int count;
    int best_label;
    float max_score;
    bool detection_active;
} ml_result_t;

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
void tarea_ia(void *arg);

/* Audio initialization functions */
cy_rslt_t init_audio_system(cyhal_pdm_pcm_t* pdm_pcm);
cy_rslt_t init_ml_model(void);

/* Audio processing functions */
cy_rslt_t process_audio_buffer(cyhal_pdm_pcm_t* pdm_pcm, ml_result_t* result);
bool check_ml_trigger(ml_result_t* result);

/* Utility functions */
void print_ml_results(ml_result_t* result);
void cleanup_audio_system(cyhal_pdm_pcm_t* pdm_pcm);

#endif /* IA_TASK_H_ */