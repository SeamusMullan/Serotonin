/**
 * @file sframe_stubs.c
 * @brief libsframe API stubs for binutils userland ports on Serotonin OS
 */

#include <stddef.h>
#include <stdbool.h>
#include "sframe-api.h"

const char *sframe_errmsg(int error) {
    (void)error;
    return "sframe unavailable";
}

unsigned char sframe_fde_create_func_info(uint32_t fre_type, uint32_t fde_type) {
    (void)fre_type;
    (void)fde_type;
    return 0;
}

uint32_t sframe_calc_fre_type(size_t func_size) {
    (void)func_size;
    return 0;
}

sframe_decoder_ctx *sframe_decode(const char *cf_buf, size_t cf_size, int *errp) {
    (void)cf_buf;
    (void)cf_size;
    if (errp) {
        *errp = SFRAME_ERR_INVAL;
    }
    return NULL;
}

void sframe_decoder_free(sframe_decoder_ctx **dctx) {
    if (dctx) {
        *dctx = NULL;
    }
}

unsigned int sframe_decoder_get_hdr_size(const sframe_decoder_ctx *dctx) {
    (void)dctx;
    return 0;
}

uint8_t sframe_decoder_get_abi_arch(const sframe_decoder_ctx *dctx) {
    (void)dctx;
    return 0;
}

uint8_t sframe_decoder_get_version(const sframe_decoder_ctx *dctx) {
    (void)dctx;
    return 0;
}

uint32_t sframe_decoder_get_num_fidx(const sframe_decoder_ctx *dctx) {
    (void)dctx;
    return 0;
}

int8_t sframe_decoder_get_fixed_fp_offset(const sframe_decoder_ctx *dctx) {
    (void)dctx;
    return 0;
}

int8_t sframe_decoder_get_fixed_ra_offset(const sframe_decoder_ctx *dctx) {
    (void)dctx;
    return 0;
}

int sframe_decoder_get_fre(const sframe_decoder_ctx *ctx,
                           unsigned int func_idx,
                           unsigned int fre_idx,
                           sframe_frame_row_entry *fre) {
    (void)ctx;
    (void)func_idx;
    (void)fre_idx;
    (void)fre;
    return SFRAME_ERR;
}

int sframe_decoder_get_funcdesc_v2(const sframe_decoder_ctx *ctx,
                                  unsigned int i,
                                  uint32_t *num_fres,
                                  uint32_t *func_size,
                                  int32_t *func_start_address,
                                  unsigned char *func_info,
                                  uint8_t *rep_block_size) {
    (void)ctx;
    (void)i;
    if (num_fres) *num_fres = 0;
    if (func_size) *func_size = 0;
    if (func_start_address) *func_start_address = 0;
    if (func_info) *func_info = 0;
    if (rep_block_size) *rep_block_size = 0;
    return SFRAME_ERR;
}

sframe_encoder_ctx *sframe_encode(uint8_t ver, uint8_t flags, uint8_t abi_arch,
                                  int8_t fixed_fp_offset, int8_t fixed_ra_offset,
                                  int *errp) {
    (void)ver;
    (void)flags;
    (void)abi_arch;
    (void)fixed_fp_offset;
    (void)fixed_ra_offset;
    if (errp) {
        *errp = SFRAME_ERR_INVAL;
    }
    return NULL;
}

void sframe_encoder_free(sframe_encoder_ctx **encoder) {
    if (encoder) {
        *encoder = NULL;
    }
}

unsigned int sframe_encoder_get_hdr_size(sframe_encoder_ctx *encoder) {
    (void)encoder;
    return 0;
}

uint8_t sframe_encoder_get_abi_arch(sframe_encoder_ctx *encoder) {
    (void)encoder;
    return 0;
}

uint8_t sframe_encoder_get_version(sframe_encoder_ctx *encoder) {
    (void)encoder;
    return 0;
}

uint32_t sframe_encoder_get_num_fidx(sframe_encoder_ctx *encoder) {
    (void)encoder;
    return 0;
}

int sframe_encoder_add_fre(sframe_encoder_ctx *encoder,
                           unsigned int func_idx,
                           sframe_frame_row_entry *frep) {
    (void)encoder;
    (void)func_idx;
    (void)frep;
    return SFRAME_ERR;
}

int sframe_encoder_add_funcdesc(sframe_encoder_ctx *ectx,
                                int64_t start_addr,
                                uint32_t func_size) {
    (void)ectx;
    (void)start_addr;
    (void)func_size;
    return SFRAME_ERR;
}

int sframe_encoder_add_funcdesc_v2(sframe_encoder_ctx *encoder,
                                   int32_t start_addr,
                                   uint32_t func_size,
                                   unsigned char func_info,
                                   uint8_t rep_block_size,
                                   uint32_t num_fres) {
    (void)encoder;
    (void)start_addr;
    (void)func_size;
    (void)func_info;
    (void)rep_block_size;
    (void)num_fres;
    return SFRAME_ERR;
}

char *sframe_encoder_write(sframe_encoder_ctx *encoder,
                           size_t *encoded_size,
                           bool sort_fde_p,
                           int *errp) {
    (void)encoder;
    (void)sort_fde_p;
    if (encoded_size) {
        *encoded_size = 0;
    }
    if (errp) {
        *errp = SFRAME_ERR_INVAL;
    }
    return NULL;
}

void dump_sframe(const sframe_decoder_ctx *decoder, uint64_t addr) {
    (void)decoder;
    (void)addr;
}
