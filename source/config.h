#ifndef CONFIG_H
#define CONFIG_H
// Servidor config
#define WIFI_SSID "Infinitum9609"
#define WIFI_PASSWORD "ct5QY42dR9"
#define TCP_PORT 50007
#define BUFFER_SIZE 256
#define MAX_RETRIES 5
#define MAX_CLIENTS 3
#define CLIENT_TASK_STACK_SIZE (1024 * 4)
#define CLIENT_TASK_PRIORITY 2
#define SERVER_RECOVERY_DELAY_MS 5000
#define CLIENT_TIMEOUT_MS 80000  // 80 segundos timeout por cliente
#define FAST_QUEUE_TIMEOUT   pdMS_TO_TICKS(25)   // Para operaciones críticas
#define NORMAL_QUEUE_TIMEOUT pdMS_TO_TICKS(100)  // Para operaciones normales
// Pines
/* PDM/PCM Pins */
#define PDM_DATA P10_5
#define PDM_CLK P10_4
#define NUM_OUTPUTS (4)
#define OUT1 P9_0
#define OUT2 P9_1
#define OUT3 P9_2
#define OUT4 P9_3
// Microfono
#define SAMPLE_RATE_HZ 16000
#define AUDIO_SYS_CLOCK_HZ 24576000
#define DECIMATION_RATE 64
#define MICROPHONE_GAIN 20
#define DIGITAL_BOOST_FACTOR 10.0f
#define AUIDO_BITS_PER_SAMPLE 16
#define AUDIO_BUFFER_SIZE 512
#define SAMPLE_NORMALIZE(sample) (((float)(sample)) / (float)(1 << (AUIDO_BITS_PER_SAMPLE - 1)))
// Disparador ML
#define ML_TRIGGER_THRESHOLD 0.95f
#define ML_TRIGGER_LABEL_INDEX 1

#endif /* CONFIG_H */