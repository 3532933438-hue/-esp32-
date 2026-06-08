#pragma once

void audio_fft_init(void);
void audio_fft_task(void *arg);

extern float spectrum_display[32];     // 32柱频谱数据
extern SemaphoreHandle_t spectrum_ready_sem;
