#include "source/src/ui/lvgl/d211/d211_lvgl_mpp_decoder.h"

#if HMI_NEXUS_HAS_D211_MPP_DECODER && HMI_NEXUS_HAS_LVGL

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <dma_allocator.h>
#include <frame_allocator.h>
#include <mpp_decoder.h>

#include "lvgl.h"
#include "source/src/ui/lvgl/d211/d211_lvgl_fake_image.h"
#include "source/src/ui/lvgl/d211/d211_lvgl_ge2d.h"
#include "third_party/lvgl/src/core/lv_global.h"
#include "third_party/lvgl/src/draw/lv_image_decoder_private.h"
#include "third_party/lvgl/src/misc/cache/lv_cache.h"

#ifdef LV_CACHE_IMG_NUM
#define HMI_NEXUS_D211_MPP_DECODER_CACHE_LIMIT ((uint32_t)LV_CACHE_IMG_NUM)
#else
#define HMI_NEXUS_D211_MPP_DECODER_CACHE_LIMIT 8U
#endif
#define HMI_NEXUS_D211_PNG_HEADER_SIZE (8U + 12U + 13U)
#define HMI_NEXUS_D211_PNG_SIG 0x89504e470d0a1a0aULL
#define HMI_NEXUS_D211_MNG_SIG 0x8a4d4e470d0a1a0aULL
#define HMI_NEXUS_D211_JPEG_SOI 0xffd8U
#define HMI_NEXUS_D211_JPEG_SOF0 0xffc0U
#define HMI_NEXUS_D211_JPEG_SOF1 0xffc1U
#define HMI_NEXUS_D211_JPEG_SOF2 0xffc2U
#define HMI_NEXUS_D211_JPEG_SOF3 0xffc3U
#define HMI_NEXUS_D211_ALIGN_UP(value, align) (((value) + ((align) - 1)) & ~((align) - 1))
#define HMI_NEXUS_D211_JPEG_DEC_MAX_OUT_WIDTH 2048
#define HMI_NEXUS_D211_JPEG_DEC_MAX_OUT_HEIGHT 2048

typedef struct hmi_nexus_d211_lvgl_mpp_stream_t {
    lv_fs_file_t file;
    const uint8_t * data;
    uint32_t data_size;
    uint32_t offset;
    bool is_file;
} hmi_nexus_d211_lvgl_mpp_stream_t;

typedef struct hmi_nexus_d211_lvgl_mpp_decoded_image_t {
    lv_draw_buf_t draw_buf;
    struct mpp_buf mpp_buf;
    uint8_t * planes[3];
    int plane_sizes[3];
    int dma_fds[3];
    bool cached;
    bool ge2d_registered;
} hmi_nexus_d211_lvgl_mpp_decoded_image_t;

typedef struct hmi_nexus_d211_lvgl_mpp_cache_state_t {
    lv_mutex_t lock;
    uint32_t live_entries;
    uint32_t max_entries;
} hmi_nexus_d211_lvgl_mpp_cache_state_t;

typedef struct hmi_nexus_d211_lvgl_mpp_allocator_t {
    struct frame_allocator base;
    struct mpp_frame * frame;
} hmi_nexus_d211_lvgl_mpp_allocator_t;

static lv_image_decoder_t * g_decoder;
static hmi_nexus_d211_lvgl_mpp_cache_state_t * g_cache_state;

static uint64_t ReadU64Be(const uint8_t * data)
{
    return ((uint64_t)data[0] << 56) |
           ((uint64_t)data[1] << 48) |
           ((uint64_t)data[2] << 40) |
           ((uint64_t)data[3] << 32) |
           ((uint64_t)data[4] << 24) |
           ((uint64_t)data[5] << 16) |
           ((uint64_t)data[6] << 8) |
           (uint64_t)data[7];
}

static uint32_t ReadU32Be(const uint8_t * data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static uint16_t ReadU16Be(const uint8_t * data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static bool EndsWithIgnoreCase(const char * value, const char * suffix)
{
    size_t value_len;
    size_t suffix_len;
    size_t i;

    if(value == NULL || suffix == NULL) {
        return false;
    }

    value_len = strlen(value);
    suffix_len = strlen(suffix);
    if(value_len < suffix_len) {
        return false;
    }

    value += value_len - suffix_len;
    for(i = 0; i < suffix_len; ++i) {
        char lhs = value[i];
        char rhs = suffix[i];
        if(lhs >= 'A' && lhs <= 'Z') lhs = (char)(lhs - 'A' + 'a');
        if(rhs >= 'A' && rhs <= 'Z') rhs = (char)(rhs - 'A' + 'a');
        if(lhs != rhs) {
            return false;
        }
    }

    return true;
}

static bool PathLooksLikeJpeg(const char * path)
{
    return EndsWithIgnoreCase(path, ".jpg") || EndsWithIgnoreCase(path, ".jpeg");
}

static bool PathLooksLikePng(const char * path)
{
    return EndsWithIgnoreCase(path, ".png");
}

static lv_result_t ReadFakeImageInfo(const char * path, lv_image_header_t * header)
{
    uint16_t width;
    uint16_t height;
    bool blend;
    uint32_t color;

    if(path == NULL || header == NULL ||
       !hmi_nexus_d211_parse_fake_image_path(path, &width, &height, &blend, &color)) {
        return LV_RESULT_INVALID;
    }

    LV_UNUSED(blend);
    LV_UNUSED(color);

    header->w = width;
    header->h = height;
    header->cf = LV_COLOR_FORMAT_RAW;
    header->flags |= LV_IMAGE_FLAGS_USER8;
    header->stride = (uint16_t)HMI_NEXUS_D211_ALIGN_UP((uint32_t)width * 4U, 8U);
    return LV_RESULT_OK;
}

static lv_color_format_t ToLvglColorFormat(enum mpp_pixel_format pixel_format)
{
    switch(pixel_format) {
        case MPP_FMT_RGB_565:
            return LV_COLOR_FORMAT_RGB565;
        case MPP_FMT_RGB_888:
            return LV_COLOR_FORMAT_RGB888;
        case MPP_FMT_ARGB_8888:
            return LV_COLOR_FORMAT_ARGB8888;
        case MPP_FMT_XRGB_8888:
            return LV_COLOR_FORMAT_XRGB8888;
        case MPP_FMT_YUV420P:
            return LV_COLOR_FORMAT_I420;
        case MPP_FMT_YUV422P:
            return LV_COLOR_FORMAT_I422;
        case MPP_FMT_YUV444P:
            return LV_COLOR_FORMAT_I444;
        case MPP_FMT_YUV400:
            return LV_COLOR_FORMAT_I400;
        default:
            break;
    }

    return LV_COLOR_FORMAT_UNKNOWN;
}

static bool IsYuvColorFormat(lv_color_format_t color_format)
{
    return color_format == LV_COLOR_FORMAT_I420 ||
           color_format == LV_COLOR_FORMAT_I422 ||
           color_format == LV_COLOR_FORMAT_I444 ||
           color_format == LV_COLOR_FORMAT_I400;
}

static enum mpp_pixel_format ToMppPixelFormat(lv_color_format_t color_format)
{
    switch(color_format) {
        case LV_COLOR_FORMAT_RGB565:
            return MPP_FMT_RGB_565;
        case LV_COLOR_FORMAT_RGB888:
            return MPP_FMT_RGB_888;
        case LV_COLOR_FORMAT_ARGB8888:
            return MPP_FMT_ARGB_8888;
        case LV_COLOR_FORMAT_XRGB8888:
            return MPP_FMT_XRGB_8888;
        case LV_COLOR_FORMAT_I420:
            return MPP_FMT_YUV420P;
        case LV_COLOR_FORMAT_I422:
            return MPP_FMT_YUV422P;
        case LV_COLOR_FORMAT_I444:
            return MPP_FMT_YUV444P;
        case LV_COLOR_FORMAT_I400:
            return MPP_FMT_YUV400;
        default:
            break;
    }

    return MPP_FMT_MAX;
}

static uint32_t Align16(uint32_t value)
{
    return HMI_NEXUS_D211_ALIGN_UP(value, 16U);
}

static uint32_t ComputeStride(uint32_t width, enum mpp_pixel_format pixel_format)
{
    switch(pixel_format) {
        case MPP_FMT_RGB_565:
            return Align16(width) * 2U;
        case MPP_FMT_RGB_888:
            return Align16(width) * 3U;
        case MPP_FMT_ARGB_8888:
        case MPP_FMT_XRGB_8888:
            return Align16(width * 4U);
        case MPP_FMT_YUV420P:
        case MPP_FMT_YUV422P:
        case MPP_FMT_YUV444P:
        case MPP_FMT_YUV400:
            return Align16(width);
        default:
            break;
    }

    return 0U;
}

static int JpegWidthLimit(int width)
{
    int scale = 0;

    while(width > HMI_NEXUS_D211_JPEG_DEC_MAX_OUT_WIDTH) {
        width >>= 1;
        scale++;
        if(scale == 3) {
            break;
        }
    }

    return scale;
}

static int JpegHeightLimit(int height)
{
    int scale = 0;

    while(height > HMI_NEXUS_D211_JPEG_DEC_MAX_OUT_HEIGHT) {
        height >>= 1;
        scale++;
        if(scale == 3) {
            break;
        }
    }

    return scale;
}

static int JpegSizeLimit(int width, int height)
{
    int width_scale = JpegWidthLimit(width);
    int height_scale = JpegHeightLimit(height);
    return width_scale > height_scale ? width_scale : height_scale;
}

static lv_fs_res_t StreamOpen(hmi_nexus_d211_lvgl_mpp_stream_t * stream,
                              const lv_image_decoder_dsc_t * dsc)
{
    if(stream == NULL || dsc == NULL || dsc->src == NULL) {
        return LV_FS_RES_UNKNOWN;
    }

    memset(stream, 0, sizeof(*stream));

    if(dsc->src_type == LV_IMAGE_SRC_FILE) {
        stream->is_file = true;
        return lv_fs_open(&stream->file, dsc->src, LV_FS_MODE_RD);
    }

    if(dsc->src_type == LV_IMAGE_SRC_VARIABLE) {
        const lv_image_dsc_t * image = (const lv_image_dsc_t *)dsc->src;
        stream->data = image->data;
        stream->data_size = image->data_size;
        stream->offset = 0U;
        stream->is_file = false;
        return LV_FS_RES_OK;
    }

    return LV_FS_RES_UNKNOWN;
}

static void StreamClose(hmi_nexus_d211_lvgl_mpp_stream_t * stream)
{
    if(stream == NULL) {
        return;
    }

    if(stream->is_file) {
        lv_fs_close(&stream->file);
    }
}

static lv_fs_res_t StreamSeek(hmi_nexus_d211_lvgl_mpp_stream_t * stream,
                              uint32_t position,
                              lv_fs_whence_t whence)
{
    if(stream == NULL) {
        return LV_FS_RES_UNKNOWN;
    }

    if(stream->is_file) {
        return lv_fs_seek(&stream->file, position, whence);
    }

    if(whence == LV_FS_SEEK_SET) {
        if(position > stream->data_size) {
            return LV_FS_RES_UNKNOWN;
        }
        stream->offset = position;
        return LV_FS_RES_OK;
    }
    if(whence == LV_FS_SEEK_CUR) {
        if(stream->offset + position > stream->data_size) {
            return LV_FS_RES_UNKNOWN;
        }
        stream->offset += position;
        return LV_FS_RES_OK;
    }
    if(whence == LV_FS_SEEK_END) {
        if(position > stream->data_size) {
            return LV_FS_RES_UNKNOWN;
        }
        stream->offset = stream->data_size - position;
        return LV_FS_RES_OK;
    }

    return LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t StreamRead(hmi_nexus_d211_lvgl_mpp_stream_t * stream,
                              void * buffer,
                              uint32_t bytes_to_read,
                              uint32_t * bytes_read)
{
    if(stream == NULL || buffer == NULL || bytes_read == NULL) {
        return LV_FS_RES_UNKNOWN;
    }

    if(stream->is_file) {
        return lv_fs_read(&stream->file, buffer, bytes_to_read, bytes_read);
    }

    if(stream->offset + bytes_to_read > stream->data_size) {
        return LV_FS_RES_UNKNOWN;
    }

    memcpy(buffer, stream->data + stream->offset, bytes_to_read);
    stream->offset += bytes_to_read;
    *bytes_read = bytes_to_read;
    return LV_FS_RES_OK;
}

static lv_result_t StreamGetSize(hmi_nexus_d211_lvgl_mpp_stream_t * stream,
                                 uint32_t * size_out)
{
    lv_fs_res_t fs_res;

    if(stream == NULL || size_out == NULL) {
        return LV_RESULT_INVALID;
    }

    if(stream->is_file) {
        fs_res = lv_fs_seek(&stream->file, 0U, LV_FS_SEEK_END);
        if(fs_res != LV_FS_RES_OK) {
            return LV_RESULT_INVALID;
        }
        fs_res = lv_fs_tell(&stream->file, size_out);
        if(fs_res != LV_FS_RES_OK) {
            return LV_RESULT_INVALID;
        }
        fs_res = lv_fs_seek(&stream->file, 0U, LV_FS_SEEK_SET);
        return fs_res == LV_FS_RES_OK ? LV_RESULT_OK : LV_RESULT_INVALID;
    }

    *size_out = stream->data_size;
    return LV_RESULT_OK;
}

static lv_result_t CheckPngSignature(hmi_nexus_d211_lvgl_mpp_stream_t * stream)
{
    uint8_t signature[8];
    uint32_t read_size = 0;
    uint64_t value;

    if(StreamRead(stream, signature, sizeof(signature), &read_size) != LV_FS_RES_OK ||
       read_size != sizeof(signature)) {
        return LV_RESULT_INVALID;
    }

    value = ReadU64Be(signature);
    if(value != HMI_NEXUS_D211_PNG_SIG && value != HMI_NEXUS_D211_MNG_SIG) {
        return LV_RESULT_INVALID;
    }

    return LV_RESULT_OK;
}

static lv_result_t ReadPngInfo(const lv_image_decoder_dsc_t * dsc,
                               lv_image_header_t * header)
{
    hmi_nexus_d211_lvgl_mpp_stream_t stream;
    uint8_t raw_header[HMI_NEXUS_D211_PNG_HEADER_SIZE];
    uint32_t read_size = 0;
    uint64_t signature;
    lv_fs_res_t fs_res;
    uint8_t color_type;

    if(StreamOpen(&stream, dsc) != LV_FS_RES_OK) {
        return LV_RESULT_INVALID;
    }

    fs_res = StreamRead(&stream, raw_header, sizeof(raw_header), &read_size);
    if(fs_res != LV_FS_RES_OK || read_size != sizeof(raw_header)) {
        StreamClose(&stream);
        return LV_RESULT_INVALID;
    }

    signature = ReadU64Be(raw_header);
    if(signature != HMI_NEXUS_D211_PNG_SIG && signature != HMI_NEXUS_D211_MNG_SIG) {
        StreamClose(&stream);
        return LV_RESULT_INVALID;
    }

    color_type = raw_header[8U + 8U + 8U + 1U];
    header->w = (uint16_t)ReadU32Be(raw_header + 16U);
    header->h = (uint16_t)ReadU32Be(raw_header + 20U);
    header->cf = color_type == 2U ? LV_COLOR_FORMAT_RGB888 : LV_COLOR_FORMAT_ARGB8888;
    header->flags |= LV_IMAGE_FLAGS_USER8;
    header->stride = (uint16_t)ComputeStride(header->w, ToMppPixelFormat(header->cf));

    StreamClose(&stream);
    return header->cf == LV_COLOR_FORMAT_UNKNOWN ? LV_RESULT_INVALID : LV_RESULT_OK;
}

static lv_result_t CheckJpegSoi(hmi_nexus_d211_lvgl_mpp_stream_t * stream)
{
    uint8_t bytes[2];
    uint32_t read_size = 0;

    if(StreamRead(stream, bytes, sizeof(bytes), &read_size) != LV_FS_RES_OK ||
       read_size != sizeof(bytes)) {
        return LV_RESULT_INVALID;
    }

    return ReadU16Be(bytes) == HMI_NEXUS_D211_JPEG_SOI ? LV_RESULT_OK : LV_RESULT_INVALID;
}

static bool IsJpegStartOfFrame(uint16_t marker)
{
    return marker == HMI_NEXUS_D211_JPEG_SOF0 ||
           marker == HMI_NEXUS_D211_JPEG_SOF1 ||
           marker == HMI_NEXUS_D211_JPEG_SOF2 ||
           marker == HMI_NEXUS_D211_JPEG_SOF3;
}

static lv_result_t GetJpegPixelFormat(const uint8_t * payload,
                                      enum mpp_pixel_format * pixel_format)
{
    uint8_t h_count[3] = {0U, 0U, 0U};
    uint8_t v_count[3] = {0U, 0U, 0U};
    uint32_t h_count_flag;
    uint32_t v_count_flag;
    uint8_t component_count;
    int i;

    if(payload == NULL || pixel_format == NULL) {
        return LV_RESULT_INVALID;
    }

    component_count = *payload++;
    if(component_count == 0U || component_count > 3U) {
        return LV_RESULT_INVALID;
    }

    for(i = 0; i < component_count; ++i) {
        uint8_t hv_count;

        payload++;
        hv_count = *payload++;
        h_count[i] = (uint8_t)(hv_count >> 4);
        v_count[i] = (uint8_t)(hv_count & 0x0FU);
        payload++;
    }

    h_count_flag = (uint32_t)h_count[2] |
                   ((uint32_t)h_count[1] << 4) |
                   ((uint32_t)h_count[0] << 8);
    v_count_flag = (uint32_t)v_count[2] |
                   ((uint32_t)v_count[1] << 4) |
                   ((uint32_t)v_count[0] << 8);

    if(h_count_flag == 0x211U && v_count_flag == 0x211U) {
        *pixel_format = MPP_FMT_YUV420P;
        return LV_RESULT_OK;
    }
    if(h_count_flag == 0x211U && v_count_flag == 0x111U) {
        *pixel_format = MPP_FMT_YUV422P;
        return LV_RESULT_OK;
    }
    if(h_count_flag == 0x111U && v_count_flag == 0x111U) {
        *pixel_format = MPP_FMT_YUV444P;
        return LV_RESULT_OK;
    }
    if(h_count_flag == 0x111U && v_count_flag == 0x222U) {
        *pixel_format = MPP_FMT_YUV444P;
        return LV_RESULT_OK;
    }
    if(h_count[1] == 0U && v_count[1] == 0U &&
       h_count[2] == 0U && v_count[2] == 0U) {
        *pixel_format = MPP_FMT_YUV400;
        return LV_RESULT_OK;
    }

    return LV_RESULT_INVALID;
}

static lv_result_t ReadJpegInfo(const lv_image_decoder_dsc_t * dsc,
                                lv_image_header_t * header)
{
    hmi_nexus_d211_lvgl_mpp_stream_t stream;
    uint8_t marker_header[4];
    uint8_t sof_payload[15];
    uint32_t read_size = 0;
    enum mpp_pixel_format pixel_format;
    int width;
    int height;
    int scale_shift;

    if(StreamOpen(&stream, dsc) != LV_FS_RES_OK) {
        return LV_RESULT_INVALID;
    }

    if(CheckJpegSoi(&stream) != LV_RESULT_OK) {
        StreamClose(&stream);
        return LV_RESULT_INVALID;
    }

    while(true) {
        uint16_t marker;
        uint16_t segment_size;

        if(StreamRead(&stream, marker_header, sizeof(marker_header), &read_size) != LV_FS_RES_OK ||
           read_size != sizeof(marker_header)) {
            StreamClose(&stream);
            return LV_RESULT_INVALID;
        }

        marker = ReadU16Be(marker_header);
        segment_size = ReadU16Be(marker_header + 2);
        if(segment_size < 2U) {
            StreamClose(&stream);
            return LV_RESULT_INVALID;
        }

        if(IsJpegStartOfFrame(marker)) {
            if(StreamRead(&stream, sof_payload, sizeof(sof_payload), &read_size) != LV_FS_RES_OK ||
               read_size != sizeof(sof_payload)) {
                StreamClose(&stream);
                return LV_RESULT_INVALID;
            }

            if(GetJpegPixelFormat(sof_payload + 5, &pixel_format) != LV_RESULT_OK) {
                StreamClose(&stream);
                return LV_RESULT_INVALID;
            }

            height = (int)ReadU16Be(sof_payload + 1);
            width = (int)ReadU16Be(sof_payload + 3);
            scale_shift = JpegSizeLimit(width, height);
            if(scale_shift > 0) {
                width >>= scale_shift;
                height >>= scale_shift;
                header->reserved_2 = (uint16_t)scale_shift;
            }

            header->w = (uint16_t)width;
            header->h = (uint16_t)height;
            header->cf = ToLvglColorFormat(pixel_format);
            header->flags |= LV_IMAGE_FLAGS_USER8;
            header->stride = (uint16_t)ComputeStride(header->w, pixel_format);
            StreamClose(&stream);
            return header->cf == LV_COLOR_FORMAT_UNKNOWN ? LV_RESULT_INVALID : LV_RESULT_OK;
        }

        if(StreamSeek(&stream, (uint32_t)(segment_size - 2U), LV_FS_SEEK_CUR) != LV_FS_RES_OK) {
            StreamClose(&stream);
            return LV_RESULT_INVALID;
        }
    }
}

static lv_result_t D211MppDecoderInfo(lv_image_decoder_t * decoder,
                                      lv_image_decoder_dsc_t * dsc,
                                      lv_image_header_t * header)
{
    LV_UNUSED(decoder);

    if(dsc == NULL || header == NULL || dsc->src == NULL) {
        return LV_RESULT_INVALID;
    }

    if(dsc->src_type == LV_IMAGE_SRC_FILE) {
        const char * path = (const char *)dsc->src;
        if(hmi_nexus_d211_fake_image_path_looks_like(path)) {
            return ReadFakeImageInfo(path, header);
        }
        if(PathLooksLikePng(path)) {
            return ReadPngInfo(dsc, header);
        }
        if(PathLooksLikeJpeg(path)) {
            return ReadJpegInfo(dsc, header);
        }
        return LV_RESULT_INVALID;
    }

    if(dsc->src_type == LV_IMAGE_SRC_VARIABLE) {
        const lv_image_dsc_t * image = (const lv_image_dsc_t *)dsc->src;
        if(image->header.cf != LV_COLOR_FORMAT_RAW &&
           image->header.cf != LV_COLOR_FORMAT_RAW_ALPHA) {
            return LV_RESULT_INVALID;
        }

        if(ReadJpegInfo(dsc, header) == LV_RESULT_OK) {
            return LV_RESULT_OK;
        }
        return ReadPngInfo(dsc, header);
    }

    return LV_RESULT_INVALID;
}

static int ComputeFramePlaneSizes(struct mpp_buf * buffer, int plane_sizes[3])
{
    uint32_t height_aligned;

    memset(plane_sizes, 0, sizeof(int) * 3U);
    if(buffer == NULL) {
        return -1;
    }

    height_aligned = Align16((uint32_t)buffer->size.height);

    switch(buffer->format) {
        case MPP_FMT_YUV420P:
            buffer->stride[0] = Align16((uint32_t)buffer->size.width);
            buffer->stride[1] = buffer->stride[0] >> 1;
            buffer->stride[2] = buffer->stride[0] >> 1;
            plane_sizes[0] = (int)(buffer->stride[0] * height_aligned);
            plane_sizes[1] = (int)(buffer->stride[1] * (height_aligned >> 1));
            plane_sizes[2] = (int)(buffer->stride[2] * (height_aligned >> 1));
            return 0;
        case MPP_FMT_YUV422P:
            buffer->stride[0] = Align16((uint32_t)buffer->size.width);
            buffer->stride[1] = buffer->stride[0] >> 1;
            buffer->stride[2] = buffer->stride[0] >> 1;
            plane_sizes[0] = (int)(buffer->stride[0] * height_aligned);
            plane_sizes[1] = (int)(buffer->stride[1] * height_aligned);
            plane_sizes[2] = (int)(buffer->stride[2] * height_aligned);
            return 0;
        case MPP_FMT_YUV444P:
            buffer->stride[0] = Align16((uint32_t)buffer->size.width);
            buffer->stride[1] = buffer->stride[0];
            buffer->stride[2] = buffer->stride[0];
            plane_sizes[0] = (int)(buffer->stride[0] * height_aligned);
            plane_sizes[1] = (int)(buffer->stride[1] * height_aligned);
            plane_sizes[2] = (int)(buffer->stride[2] * height_aligned);
            return 0;
        case MPP_FMT_YUV400:
            buffer->stride[0] = Align16((uint32_t)buffer->size.width);
            plane_sizes[0] = (int)(buffer->stride[0] * height_aligned);
            return 0;
        case MPP_FMT_RGB_565:
        case MPP_FMT_RGB_888:
        case MPP_FMT_ARGB_8888:
        case MPP_FMT_XRGB_8888:
            buffer->stride[0] = ComputeStride((uint32_t)buffer->size.width, buffer->format);
            if(buffer->format == MPP_FMT_ARGB_8888 || buffer->format == MPP_FMT_XRGB_8888) {
                plane_sizes[0] = (int)(buffer->stride[0] * (uint32_t)buffer->size.height);
            }
            else {
                plane_sizes[0] = (int)(buffer->stride[0] * height_aligned);
            }
            return 0;
        default:
            break;
    }

    return -1;
}

static uint32_t ComputeDecodedDataSize(const struct mpp_buf * buffer)
{
    struct mpp_buf buffer_copy;
    int plane_sizes[3];

    if(buffer == NULL) {
        return 0U;
    }

    memcpy(&buffer_copy, buffer, sizeof(buffer_copy));
    if(ComputeFramePlaneSizes(&buffer_copy, plane_sizes) != 0) {
        return 0U;
    }

    return (uint32_t)(plane_sizes[0] + plane_sizes[1] + plane_sizes[2]);
}

static void RegisterRgbBuffer(hmi_nexus_d211_lvgl_mpp_decoded_image_t * image)
{
    hmi_nexus_d211_lvgl_buffer_desc_t desc;

    if(image == NULL || image->planes[0] == NULL || image->dma_fds[0] < 0 || image->ge2d_registered) {
        return;
    }

    memset(&desc, 0, sizeof(desc));
    desc.data = image->planes[0];
    desc.size = (size_t)image->plane_sizes[0];
    desc.stride = image->draw_buf.header.stride;
    desc.dma_fd = image->dma_fds[0];
    desc.memory_type = HMI_NEXUS_D211_LVGL_BUFFER_MEMORY_DMABUF;
    hmi_nexus_d211_lvgl_ge2d_register_external_buffer(&desc);
    image->ge2d_registered = true;
}

static void ReleaseDecodedImage(hmi_nexus_d211_lvgl_mpp_decoded_image_t * image)
{
    int i;

    if(image == NULL) {
        return;
    }

    if(image->ge2d_registered) {
        hmi_nexus_d211_lvgl_ge2d_unregister_external_buffer(image->planes[0]);
        image->ge2d_registered = false;
    }

    for(i = 0; i < 3; ++i) {
        if(image->planes[i] != NULL && image->planes[i] != MAP_FAILED && image->plane_sizes[i] > 0) {
            dmabuf_munmap(image->planes[i], image->plane_sizes[i]);
            image->planes[i] = NULL;
        }
        if(image->dma_fds[i] >= 0) {
            dmabuf_free(image->dma_fds[i]);
            image->dma_fds[i] = -1;
        }
    }

    lv_free(image);
}

static lv_result_t AllocateDecodedImageBuffers(hmi_nexus_d211_lvgl_mpp_decoded_image_t * image,
                                               struct mpp_frame * frame,
                                               lv_color_format_t color_format)
{
    int dma_heap_fd;
    int i;
    int plane_sizes[3];

    if(image == NULL || frame == NULL) {
        return LV_RESULT_INVALID;
    }

    if(ComputeFramePlaneSizes(&frame->buf, plane_sizes) != 0 || plane_sizes[0] <= 0) {
        return LV_RESULT_INVALID;
    }

    for(i = 0; i < 3; ++i) {
        image->dma_fds[i] = -1;
    }

    dma_heap_fd = dmabuf_device_open();
    if(dma_heap_fd < 0) {
        return LV_RESULT_INVALID;
    }

    for(i = 0; i < 3; ++i) {
        if(plane_sizes[i] <= 0) {
            continue;
        }

        image->dma_fds[i] = dmabuf_alloc(dma_heap_fd, plane_sizes[i]);
        if(image->dma_fds[i] < 0) {
            dmabuf_device_close(dma_heap_fd);
            ReleaseDecodedImage(image);
            return LV_RESULT_INVALID;
        }

        image->planes[i] = dmabuf_mmap(image->dma_fds[i], plane_sizes[i]);
        if(image->planes[i] == NULL || image->planes[i] == MAP_FAILED) {
            dmabuf_device_close(dma_heap_fd);
            ReleaseDecodedImage(image);
            return LV_RESULT_INVALID;
        }

        image->plane_sizes[i] = plane_sizes[i];
        frame->buf.fd[i] = image->dma_fds[i];
    }

    dmabuf_device_close(dma_heap_fd);

    memcpy(&image->mpp_buf, &frame->buf, sizeof(image->mpp_buf));
    image->draw_buf.header.magic = LV_IMAGE_HEADER_MAGIC;
    image->draw_buf.header.cf = color_format;
    image->draw_buf.header.flags = LV_IMAGE_FLAGS_USER8;
    image->draw_buf.header.w = (uint16_t)frame->buf.size.width;
    image->draw_buf.header.h = (uint16_t)frame->buf.size.height;
    image->draw_buf.header.stride = (uint16_t)frame->buf.stride[0];
    image->draw_buf.data_size = ComputeDecodedDataSize(&image->mpp_buf);
    if(IsYuvColorFormat(color_format)) {
        image->draw_buf.data = (uint8_t *)&image->mpp_buf;
        image->draw_buf.unaligned_data = (uint8_t *)&image->mpp_buf;
    }
    else {
        image->draw_buf.data = image->planes[0];
        image->draw_buf.unaligned_data = image->planes[0];
    }
    image->draw_buf.handlers = NULL;

    return LV_RESULT_OK;
}

static int AllocatorAllocFrameBuffer(struct frame_allocator * allocator,
                                     struct mpp_frame * frame,
                                     int width,
                                     int height,
                                     enum mpp_pixel_format format)
{
    hmi_nexus_d211_lvgl_mpp_allocator_t * wrapper =
        (hmi_nexus_d211_lvgl_mpp_allocator_t *)allocator;

    LV_UNUSED(width);
    LV_UNUSED(height);
    LV_UNUSED(format);

    if(wrapper == NULL || wrapper->frame == NULL || frame == NULL) {
        return -1;
    }

    memcpy(frame, wrapper->frame, sizeof(*frame));
    return 0;
}

static int AllocatorFreeFrameBuffer(struct frame_allocator * allocator,
                                    struct mpp_frame * frame)
{
    LV_UNUSED(allocator);
    LV_UNUSED(frame);
    return 0;
}

static int AllocatorClose(struct frame_allocator * allocator)
{
    free(allocator);
    return 0;
}

static struct alloc_ops g_allocator_ops = {
    .alloc_frame_buffer = AllocatorAllocFrameBuffer,
    .free_frame_buffer = AllocatorFreeFrameBuffer,
    .close_allocator = AllocatorClose,
};

static struct frame_allocator * OpenAllocator(struct mpp_frame * frame)
{
    hmi_nexus_d211_lvgl_mpp_allocator_t * wrapper =
        (hmi_nexus_d211_lvgl_mpp_allocator_t *)malloc(sizeof(*wrapper));

    if(wrapper == NULL) {
        return NULL;
    }

    memset(wrapper, 0, sizeof(*wrapper));
    wrapper->base.ops = &g_allocator_ops;
    wrapper->frame = frame;
    return &wrapper->base;
}

static bool CacheHasRoom(lv_cache_t * cache)
{
    bool evicted = false;

    if(cache == NULL || g_cache_state == NULL) {
        return true;
    }

    lv_mutex_lock(&g_cache_state->lock);
    while(g_cache_state->live_entries >= g_cache_state->max_entries) {
        lv_mutex_unlock(&g_cache_state->lock);
        if(!lv_cache_evict_one(cache, NULL)) {
            return false;
        }
        evicted = true;
        lv_mutex_lock(&g_cache_state->lock);
    }
    lv_mutex_unlock(&g_cache_state->lock);

    LV_UNUSED(evicted);
    return true;
}

static void CacheIncrement(void)
{
    if(g_cache_state == NULL) {
        return;
    }

    lv_mutex_lock(&g_cache_state->lock);
    g_cache_state->live_entries++;
    lv_mutex_unlock(&g_cache_state->lock);
}

static void CacheDecrement(void)
{
    if(g_cache_state == NULL) {
        return;
    }

    lv_mutex_lock(&g_cache_state->lock);
    if(g_cache_state->live_entries > 0U) {
        g_cache_state->live_entries--;
    }
    lv_mutex_unlock(&g_cache_state->lock);
}

static void DefaultImageCacheFree(lv_image_cache_data_t * entry)
{
    lv_draw_buf_t * decoded;

    if(entry == NULL) {
        return;
    }

    decoded = (lv_draw_buf_t *)entry->decoded;
    if(decoded != NULL && lv_draw_buf_has_flag(decoded, LV_IMAGE_FLAGS_ALLOCATED)) {
        lv_draw_buf_destroy(decoded);
    }

    if(entry->src_type == LV_IMAGE_SRC_FILE) {
        lv_free((void *)entry->src);
    }
}

static void D211MppImageCacheFree(void * node, void * user_data)
{
    lv_image_cache_data_t * entry = (lv_image_cache_data_t *)node;

    LV_UNUSED(user_data);

    if(entry != NULL && entry->decoder == g_decoder && entry->user_data != NULL) {
        ReleaseDecodedImage((hmi_nexus_d211_lvgl_mpp_decoded_image_t *)entry->user_data);
        CacheDecrement();
        if(entry->src_type == LV_IMAGE_SRC_FILE) {
            lv_free((void *)entry->src);
        }
        return;
    }

    DefaultImageCacheFree(entry);
}

static void InvalidateDecodedPlanes(hmi_nexus_d211_lvgl_mpp_decoded_image_t * image)
{
    int i;

    if(image == NULL) {
        return;
    }

    for(i = 0; i < 3; ++i) {
        if(image->dma_fds[i] >= 0) {
            dmabuf_sync(image->dma_fds[i], CACHE_INVALID);
        }
    }
}

static lv_result_t D211MppDecoderOpen(lv_image_decoder_t * decoder,
                                      lv_image_decoder_dsc_t * dsc)
{
    hmi_nexus_d211_lvgl_mpp_stream_t stream;
    hmi_nexus_d211_lvgl_mpp_decoded_image_t * image = NULL;
    struct mpp_decoder * mpp_decoder = NULL;
    struct frame_allocator * allocator = NULL;
    struct mpp_frame frame;
    struct mpp_packet packet;
    struct decode_config config;
    enum mpp_codec_type codec_type = MPP_CODEC_VIDEO_DECODER_PNG;
    lv_result_t result = LV_RESULT_INVALID;
    uint32_t file_size = 0;
    uint32_t bytes_read = 0;
    int scale_shift = 0;

    LV_UNUSED(decoder);

    if(dsc == NULL || dsc->src == NULL) {
        return LV_RESULT_INVALID;
    }

    if(StreamOpen(&stream, dsc) != LV_FS_RES_OK) {
        return LV_RESULT_INVALID;
    }

    image = (hmi_nexus_d211_lvgl_mpp_decoded_image_t *)lv_malloc_zeroed(sizeof(*image));
    if(image == NULL) {
        StreamClose(&stream);
        return LV_RESULT_INVALID;
    }
    for(int i = 0; i < 3; ++i) {
        image->dma_fds[i] = -1;
    }

    memset(&config, 0, sizeof(config));
    config.pix_fmt = ToMppPixelFormat(dsc->header.cf);
    if(config.pix_fmt == MPP_FMT_MAX) {
        goto cleanup;
    }

    if(dsc->src_type == LV_IMAGE_SRC_FILE) {
        const char * path = (const char *)dsc->src;
        if(PathLooksLikeJpeg(path)) {
            codec_type = MPP_CODEC_VIDEO_DECODER_MJPEG;
        }
    } else if(dsc->src_type == LV_IMAGE_SRC_VARIABLE) {
        if(CheckJpegSoi(&stream) == LV_RESULT_OK) {
            codec_type = MPP_CODEC_VIDEO_DECODER_MJPEG;
        }
        StreamSeek(&stream, 0U, LV_FS_SEEK_SET);
    }

    if(StreamGetSize(&stream, &file_size) != LV_RESULT_OK || file_size == 0U) {
        goto cleanup;
    }

    mpp_decoder = mpp_decoder_create(codec_type);
    if(mpp_decoder == NULL) {
        goto cleanup;
    }

    config.bitstream_buffer_size = (int)HMI_NEXUS_D211_ALIGN_UP(file_size, 1024U);
    config.extra_frame_num = 0;
    config.packet_count = 1;

    memset(&frame, 0, sizeof(frame));
    frame.buf.size.width = dsc->header.w;
    frame.buf.size.height = dsc->header.h;
    frame.buf.format = config.pix_fmt;
    frame.buf.buf_type = MPP_DMA_BUF_FD;
    if(AllocateDecodedImageBuffers(image, &frame, dsc->header.cf) != LV_RESULT_OK) {
        goto cleanup;
    }

    scale_shift = (int)dsc->header.reserved_2;
    if(scale_shift > 0) {
        struct mpp_scale_ratio scale;
        scale.hor_scale = scale_shift;
        scale.ver_scale = scale_shift;
        mpp_decoder_control(mpp_decoder, MPP_DEC_INIT_CMD_SET_SCALE, &scale);
    }

    allocator = OpenAllocator(&frame);
    if(allocator == NULL) {
        goto cleanup;
    }
    mpp_decoder_control(mpp_decoder, MPP_DEC_INIT_CMD_SET_EXT_FRAME_ALLOCATOR, allocator);

    if(mpp_decoder_init(mpp_decoder, &config) != 0) {
        goto cleanup;
    }

    memset(&packet, 0, sizeof(packet));
    if(mpp_decoder_get_packet(mpp_decoder, &packet, (int)file_size) != 0) {
        goto cleanup;
    }

    if(StreamRead(&stream, packet.data, file_size, &bytes_read) != LV_FS_RES_OK || bytes_read != file_size) {
        goto cleanup;
    }
    packet.size = (int)file_size;
    packet.flag = PACKET_FLAG_EOS;

    if(mpp_decoder_put_packet(mpp_decoder, &packet) != 0 ||
       mpp_decoder_decode(mpp_decoder) != 0) {
        goto cleanup;
    }

    memset(&frame, 0, sizeof(frame));
    if(mpp_decoder_get_frame(mpp_decoder, &frame) != 0) {
        goto cleanup;
    }
    InvalidateDecodedPlanes(image);
    memcpy(&image->mpp_buf, &frame.buf, sizeof(image->mpp_buf));
    mpp_decoder_put_frame(mpp_decoder, &frame);

    image->draw_buf.header.w = (uint16_t)frame.buf.size.width;
    image->draw_buf.header.h = (uint16_t)frame.buf.size.height;
    image->draw_buf.header.stride = (uint16_t)frame.buf.stride[0];
    image->draw_buf.data_size = ComputeDecodedDataSize(&image->mpp_buf);
    if(IsYuvColorFormat(image->draw_buf.header.cf)) {
        image->draw_buf.data = (uint8_t *)&image->mpp_buf;
        image->draw_buf.unaligned_data = (uint8_t *)&image->mpp_buf;
    }
    else {
        image->draw_buf.data = image->planes[0];
        image->draw_buf.unaligned_data = image->planes[0];
        RegisterRgbBuffer(image);
    }
    dsc->decoded = &image->draw_buf;

    if(dsc->cache != NULL && !dsc->args.no_cache && CacheHasRoom(dsc->cache)) {
        lv_image_cache_data_t search_key;
        lv_cache_entry_t * cache_entry;

        memset(&search_key, 0, sizeof(search_key));
        search_key.src_type = dsc->src_type;
        search_key.src = dsc->src;
        search_key.slot.size = image->draw_buf.data_size;
        image->cached = true;
        cache_entry = lv_image_decoder_add_to_cache(g_decoder,
                                                    &search_key,
                                                    &image->draw_buf,
                                                    image);
        if(cache_entry != NULL) {
            dsc->cache_entry = cache_entry;
            CacheIncrement();
        } else {
            image->cached = false;
        }
    }

    result = LV_RESULT_OK;

cleanup:
    if(mpp_decoder != NULL) {
        mpp_decoder_destory(mpp_decoder);
    }
    if(result != LV_RESULT_OK) {
        dsc->decoded = NULL;
        ReleaseDecodedImage(image);
    }
    StreamClose(&stream);
    return result;
}

static void D211MppDecoderClose(lv_image_decoder_t * decoder,
                                lv_image_decoder_dsc_t * dsc)
{
    hmi_nexus_d211_lvgl_mpp_decoded_image_t * image;

    LV_UNUSED(decoder);

    if(dsc == NULL || dsc->decoded == NULL) {
        return;
    }

    image = (hmi_nexus_d211_lvgl_mpp_decoded_image_t *)dsc->decoded;
    if(!image->cached) {
        ReleaseDecodedImage(image);
    }
}

void hmi_nexus_d211_lvgl_mpp_decoder_init(void)
{
    if(g_decoder != NULL) {
        return;
    }

    g_decoder = lv_image_decoder_create();
    if(g_decoder == NULL) {
        return;
    }

    g_cache_state = (hmi_nexus_d211_lvgl_mpp_cache_state_t *)lv_malloc_zeroed(sizeof(*g_cache_state));
    if(g_cache_state != NULL) {
        g_cache_state->max_entries = HMI_NEXUS_D211_MPP_DECODER_CACHE_LIMIT;
        lv_mutex_init(&g_cache_state->lock);
        g_decoder->user_data = g_cache_state;
    }

    lv_image_decoder_set_info_cb(g_decoder, D211MppDecoderInfo);
    lv_image_decoder_set_open_cb(g_decoder, D211MppDecoderOpen);
    lv_image_decoder_set_close_cb(g_decoder, D211MppDecoderClose);

    if(LV_GLOBAL_DEFAULT()->img_cache != NULL) {
        lv_cache_set_free_cb(LV_GLOBAL_DEFAULT()->img_cache,
                             D211MppImageCacheFree,
                             NULL);
    }
}

void hmi_nexus_d211_lvgl_mpp_decoder_deinit(void)
{
    lv_image_decoder_t * decoder = NULL;

    if(g_decoder == NULL) {
        return;
    }

    lv_image_cache_drop(NULL);

    while((decoder = lv_image_decoder_get_next(decoder)) != NULL) {
        if(decoder == g_decoder) {
            lv_image_decoder_delete(decoder);
            break;
        }
    }

    if(g_cache_state != NULL) {
        lv_mutex_delete(&g_cache_state->lock);
        lv_free(g_cache_state);
        g_cache_state = NULL;
    }

    g_decoder = NULL;
}

#else

void hmi_nexus_d211_lvgl_mpp_decoder_init(void)
{
}

void hmi_nexus_d211_lvgl_mpp_decoder_deinit(void)
{
}

#endif
