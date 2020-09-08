#ifndef XM_ADTS_UTILS_H
#define XM_ADTS_UTILS_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

bool xm_aac_adts_crop(const char *in_aac_path,
    long crop_start_ms, long crop_end_ms, const char *out_aac_path);

int xm_adts_get_duration_ms(const char *in_adts_path);

#ifdef __cplusplus
}
#endif

#endif // XM_AAC_ADTS_UTILS_H
