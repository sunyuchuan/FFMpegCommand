#include "xm_adts_utils.h"
#include "ijksdl/ijksdl_log.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define AAC_ADTS_HEADER_SIZE 7
#define NB_SAMPLES 2048

const int avpriv_mpeg4audio_sample_rates[16] = {
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000, 7350
};

typedef struct BitContext {
    const uint8_t *buffer;
    int buffer_bit_size;
    int buffer_bit_index;
} BitContext;

typedef struct ADTSHeaderInfo {
    uint8_t buffer[7];
    int32_t sample_rate;
    uint32_t samples;
    uint32_t bit_rate;
    uint32_t pkt_size;
    uint8_t  crc_absent;
    uint8_t  object_type;
    uint8_t  sampling_index;
    uint8_t  chan_config;
    uint8_t  num_aac_frames;
} ADTSHeaderInfo;

typedef struct ADTSContext {
    ADTSHeaderInfo header;
    uint32_t nb_samples;
    int32_t sample_rate;
    uint32_t bit_rate;
    uint8_t crc_absent;
    uint8_t nb_channels;
} ADTSContext;

static uint8_t avio_r8(FILE *reader)
{
    if (!reader) {
        return 0;
    }

    uint8_t buf[1];
    int read_len = fread(buf, 1, 1, reader);
    if (read_len <= 0) {
        return 0;
    }

    return buf[0];
}

static bool read_7_byte(FILE *reader, uint8_t *buffer)
{
    if (!reader || !buffer) {
        return false;
    }

    for(int i = 0; i < 7; i++) {
        buffer[i] = avio_r8(reader);
    }

    return true;
}

static bool skip_bits(BitContext *gbc, int n) {
    if (!gbc || n <= 0) {
        return false;
    }

    gbc->buffer_bit_index += n;
    if (gbc->buffer_bit_index > gbc->buffer_bit_size) {
        ALOGE("%s buffer_bit_index overflow.\n", __func__);
        return false;
    }

    return true;
}

static uint32_t get_bits(BitContext *gbc, int n) {
    uint32_t ret = 0;
    if (!gbc || n <= 0 || n > 25) {
        return ret;
    }

    int index = gbc->buffer_bit_index >> 3;
    int s = ((index + 1) << 3) - gbc->buffer_bit_index;

    uint32_t value =
        ((uint32_t)(gbc->buffer[index]) << 24) +
        ((uint32_t)(gbc->buffer[index + 1]) << 16) +
        ((uint32_t)(gbc->buffer[index + 2]) << 8) +
        (uint32_t)(gbc->buffer[index + 3]);

    ret = (value << (8 - s)) >> (32 - n);

    skip_bits(gbc, n);
    return ret;
}

static int get_adts_size_and_nb_samples(ADTSHeaderInfo *hdr)
{
    int size, rdb;
    BitContext gbc;
    gbc.buffer = hdr->buffer;
    gbc.buffer_bit_size = sizeof(hdr->buffer) << 3;
    gbc.buffer_bit_index = 0;

    if (get_bits(&gbc, 12) != 0xFFF) {
        ALOGE("%s AAC_AC3_PARSE_ERROR_SYNC.\n", __func__);
        return -1;
    }

    skip_bits(&gbc, 1); /* id */
    skip_bits(&gbc, 2); /* layer */
    skip_bits(&gbc, 1); /* protection_absent */
    skip_bits(&gbc, 2); /* profile_objecttype */
    skip_bits(&gbc, 4); /* sample_frequency_index */
    skip_bits(&gbc, 1); /* private_bit */
    skip_bits(&gbc, 3); /* channel_configuration */
    skip_bits(&gbc, 1); /* original/copy */
    skip_bits(&gbc, 1); /* home */

    /* adts_variable_header */
    skip_bits(&gbc, 1); /* copyright_identification_bit */
    skip_bits(&gbc, 1); /* copyright_identification_start */
    size = get_bits(&gbc, 13);  /* aac_frame_length */
    if (size < AAC_ADTS_HEADER_SIZE) {
        ALOGE("%s AAC_AC3_PARSE_ERROR_FRAME_SIZE.\n", __func__);
        return -1;
    }
    skip_bits(&gbc, 11);    /* adts_buffer_fullness */
    rdb = get_bits(&gbc, 2);    /* number_of_raw_data_blocks_in_frame */

    hdr->samples  = (rdb + 1) * 1024;
    hdr->pkt_size = size;
    return size;
}

static int aac_parse_adts_header(ADTSHeaderInfo *hdr)
{
    int size, rdb, ch, sr;
    int aot, crc_abs;

    BitContext gbc;
    gbc.buffer = hdr->buffer;
    gbc.buffer_bit_size = sizeof(hdr->buffer) << 3;
    gbc.buffer_bit_index = 0;

    if (get_bits(&gbc, 12) != 0xFFF) {
        ALOGE("%s AAC_AC3_PARSE_ERROR_SYNC.\n", __func__);
        return -1;
    }

    skip_bits(&gbc, 1);             /* id */
    skip_bits(&gbc, 2);           /* layer */
    crc_abs = get_bits(&gbc, 1);    /* protection_absent */
    aot     = get_bits(&gbc, 2);  /* profile_objecttype */
    sr      = get_bits(&gbc, 4);  /* sample_frequency_index */
    skip_bits(&gbc, 1);             /* private_bit */
    ch = get_bits(&gbc, 3);       /* channel_configuration */

    skip_bits(&gbc, 1);             /* original/copy */
    skip_bits(&gbc, 1);             /* home */

    /* adts_variable_header */
    skip_bits(&gbc, 1);             /* copyright_identification_bit */
    skip_bits(&gbc, 1);             /* copyright_identification_start */
    size = get_bits(&gbc, 13);    /* aac_frame_length */
    if (size < AAC_ADTS_HEADER_SIZE) {
        ALOGE("%s AAC_AC3_PARSE_ERROR_FRAME_SIZE.\n", __func__);
        return -1;
    }
    //size = (crc_abs != 1) ? (size - 9) : (size - 7);

    skip_bits(&gbc, 11);          /* adts_buffer_fullness */
    rdb = get_bits(&gbc, 2);      /* number_of_raw_data_blocks_in_frame */

    hdr->object_type    = aot + 1;
    hdr->chan_config    = ch;
    hdr->crc_absent     = crc_abs;
    hdr->num_aac_frames = rdb + 1;
    hdr->sampling_index = sr;
    hdr->sample_rate    = avpriv_mpeg4audio_sample_rates[sr];
    hdr->samples        = (rdb + 1) * 1024;
    hdr->bit_rate       = size * 8 * hdr->sample_rate / hdr->samples;
    hdr->pkt_size       = size;

    return size;
}

static void copy_adts_packet(FILE *reader, FILE *writer,
                char *buffer, ADTSHeaderInfo *hdr) {
    if (!reader || !writer || !buffer || !hdr) {
        return;
    }

    fwrite(hdr->buffer, 1, 7, writer);
    int read_len = fread(buffer, 1, hdr->pkt_size - 7, reader);
    if (read_len > 0) {
        fwrite(buffer, 1, read_len, writer);
    }
}

static int find_first_adts_header(FILE *reader, ADTSContext *ctx) {
    int ret = -1;
    if (!reader || !ctx) return ret;

    uint32_t syncword;
    ADTSHeaderInfo *header = &ctx->header;
    while(true) {
        if (feof(reader) || ferror(reader)) {
            ALOGI("%s EOF or read file error.\n", __func__);
            ret = -1;
            break;
        }

        syncword = avio_r8(reader);
        loop:
        if (syncword == 0xff) {
            header->buffer[0] = syncword;
            syncword = avio_r8(reader);
            header->buffer[1] = syncword;
            if ((syncword & 0xf0) == 0xf0) {
                for(int i = 2; i < 7; i++) {
                    header->buffer[i] = avio_r8(reader);
                }

                if (aac_parse_adts_header(header) < 0) {
                    ALOGE("%s aac_parse_adts_header error.\n", __func__);
                    ret = -1;
                    break;
                }
                ctx->sample_rate = header->sample_rate;
                ctx->nb_channels = header->chan_config;
                ctx->bit_rate = header->bit_rate;
                ctx->crc_absent = header->crc_absent;
                ctx->nb_samples += header->samples;
                ALOGI("%s find adts syncword 0xfff.\n", __func__);
                ret = 0;
                break;
            }
            goto loop;
        }
    }

    return ret;
}

static int ae_open_file(FILE **fp, const char *file_name, const int is_write) {
    int ret = 0;
    if (*fp) {
        fclose(*fp);
        *fp = NULL;
    }

    if (is_write)
        *fp = fopen(file_name, "wb");
    else
        *fp = fopen(file_name, "rb");

    if (!*fp) {
        ret = -1;
    }

    return ret;
}

bool xm_aac_adts_crop(const char *in_aac_path,
    long crop_start_ms, long crop_end_ms, const char *out_aac_path)
{
    bool ret = false;
    if (!in_aac_path || !out_aac_path) return ret;

    uint32_t next_syncword_ofs, start_pos, end_pos;
    ADTSContext *ctx = (ADTSContext *)calloc(1, sizeof(ADTSContext));
    ADTSHeaderInfo *header = &ctx->header;
    char *buffer = (char *)calloc(1, (size_t)(2 * NB_SAMPLES));

    FILE *reader = NULL;
    FILE *writer = NULL;
    if (ae_open_file(&reader, in_aac_path, false) < 0) {
        ALOGE("%s open in_aac_path %s failed\n", __func__, in_aac_path);
        ret = false;
        goto end;
    }
    if (ae_open_file(&writer, out_aac_path, true) < 0) {
        ALOGE("%s open out_aac_path %s failed\n", __func__, out_aac_path);
        ret = false;
        goto end;
    }

    if (find_first_adts_header(reader, ctx) < 0) {
        ALOGE("%s find_first_adts_header failed.\n", __func__);
        ret = false;
        goto end;
    }

    crop_start_ms = crop_start_ms < 0 ? 0 : crop_start_ms;
    crop_end_ms = crop_end_ms < 0 ? 0 : crop_end_ms;
    start_pos = (crop_start_ms /(float) 1000) * ctx->sample_rate;
    end_pos = (crop_end_ms /(float) 1000) * ctx->sample_rate;
    if (ctx->nb_samples > start_pos
        && ctx->nb_samples < end_pos + header->samples) {
        copy_adts_packet(reader, writer, buffer, header);
    }

    next_syncword_ofs = ftell(reader) + header->pkt_size - 7;
    for (;;) {
        if (feof(reader) || ferror(reader)) {
            ALOGI("%s 2 EOF or read file error.\n", __func__);
            ret = true;
            goto end;
        }

        /* seek to next tag unless we know that we'll run into EOF */
        if (fseek(reader, (long)next_syncword_ofs, SEEK_SET) < 0) {
            ALOGE("%s seek to next sync word failed.\n", __func__);
            ret = false;
            goto end;
        }

        if(!read_7_byte(reader, header->buffer)) {
            ALOGE("%s read_7_byte error.\n", __func__);
            ret = false;
            goto end;
        }

        if (get_adts_size_and_nb_samples(header) < 0) {
            ALOGE("%s get_adts_size_and_nb_samples error.\n", __func__);
            ret = false;
            goto end;
        }
        next_syncword_ofs = ftell(reader) + header->pkt_size - 7;
        ctx->nb_samples += header->samples;

        if (ctx->nb_samples > start_pos
            && ctx->nb_samples < end_pos + header->samples) {
            copy_adts_packet(reader, writer, buffer, header);
        } else if (ctx->nb_samples >= end_pos + header->samples) {
            ALOGI("%s crop end.\n", __func__);
            ret = true;
            goto end;
        }
    }

end:
    if (reader != NULL) fclose(reader);
    if (writer != NULL) fclose(writer);
    if (ctx != NULL) free(ctx);
    if (buffer != NULL) free(buffer);
    return ret;
}

int xm_adts_get_duration_ms(const char *in_adts_path)
{
    int ret = -1;
    if (!in_adts_path) return ret;
    ALOGD("%s in_aac_path %s\n", __func__, in_adts_path);

    uint32_t next_syncword_ofs = 0;
    ADTSContext *ctx = (ADTSContext *)calloc(1, sizeof(ADTSContext));
    ADTSHeaderInfo *header = &ctx->header;

    FILE *reader = NULL;
    if (ae_open_file(&reader, in_adts_path, false) < 0) {
        ALOGE("%s open in_aac_path %s failed.\n", __func__, in_adts_path);
        ret = -1;
        goto end;
    }

    if (feof(reader) || ferror(reader)) {
        ALOGE("%s EOF or read file error.\n", __func__);
        ret = -1;
        goto end;
    }

    next_syncword_ofs = ftell(reader);
    if(!read_7_byte(reader, header->buffer)) {
        ALOGE("%s read_7_byte error.\n", __func__);
        ret = -1;
        goto end;
    }

    if (aac_parse_adts_header(header) < 0) {
        ALOGE("%s file no adts format.\n", __func__);
        ret = -1;
        goto end;
    }
    next_syncword_ofs += header->pkt_size;
    ctx->nb_samples += header->samples;
    ctx->sample_rate = header->sample_rate;
    ctx->nb_channels = header->chan_config;

    for (;;) {
        /* seek to next tag unless we know that we'll run into EOF */
        if (fseek(reader, (long)next_syncword_ofs, SEEK_SET) < 0) {
            ALOGI("%s seek to next sync word fail.\n", __func__);
            ret = 0;
            goto end;
        }

        if (feof(reader) || ferror(reader)) {
            ALOGI("%s 2 EOF or read file fail.\n", __func__);
            ret = 0;
            goto end;
        }

        next_syncword_ofs = ftell(reader);
        if(!read_7_byte(reader, header->buffer)) {
            ALOGI("%s read_7_byte fail.\n", __func__);
            ret = 0;
            goto end;
        }

        if (get_adts_size_and_nb_samples(header) < 0) {
            ALOGI("%s get_adts_size_and_nb_samples fail.\n", __func__);
            ret = 0;
            goto end;
        }
        next_syncword_ofs += header->pkt_size;
        ctx->nb_samples += header->samples;
    }

end:
    ret = ret < 0 ? ret : (1000 * (double)ctx->nb_samples / (double)ctx->sample_rate);
    if (reader != NULL) fclose(reader);
    if (ctx != NULL) free(ctx);
    return ret;
}

