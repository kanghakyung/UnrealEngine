#ifndef UNREAL_EXECUTABLE_DEF_READER_H
#define UNREAL_EXECUTABLE_DEF_READER_H

/* Generated by flatcc 0.6.2 FlatBuffers schema compiler for C by dvide.com */

#ifndef FLATBUFFERS_COMMON_READER_H
#include "flatbuffers_common_reader.h"
#endif
#ifndef EXECUTABLE_DEBUG_INFO_READER_H
#include "executable_debug_info_reader.h"
#endif
#include "flatcc/flatcc_flatbuffers.h"
#ifndef __alignas_is_defined
#include <stdalign.h>
#endif
#include "flatcc/flatcc_prologue.h"
#undef flatbuffers_identifier
#define flatbuffers_identifier "USH1"
#undef flatbuffers_extension
#define flatbuffers_extension "ush1"


typedef const struct iree_hal_unreal_UnrealShaderDef_table *iree_hal_unreal_UnrealShaderDef_table_t;
typedef struct iree_hal_unreal_UnrealShaderDef_table *iree_hal_unreal_UnrealShaderDef_mutable_table_t;
typedef const flatbuffers_uoffset_t *iree_hal_unreal_UnrealShaderDef_vec_t;
typedef flatbuffers_uoffset_t *iree_hal_unreal_UnrealShaderDef_mutable_vec_t;
typedef const struct iree_hal_unreal_ExecutableDef_table *iree_hal_unreal_ExecutableDef_table_t;
typedef struct iree_hal_unreal_ExecutableDef_table *iree_hal_unreal_ExecutableDef_mutable_table_t;
typedef const flatbuffers_uoffset_t *iree_hal_unreal_ExecutableDef_vec_t;
typedef flatbuffers_uoffset_t *iree_hal_unreal_ExecutableDef_mutable_vec_t;
#ifndef iree_hal_unreal_UnrealShaderDef_file_identifier
#define iree_hal_unreal_UnrealShaderDef_file_identifier "USH1"
#endif
/* deprecated, use iree_hal_unreal_UnrealShaderDef_file_identifier */
#ifndef iree_hal_unreal_UnrealShaderDef_identifier
#define iree_hal_unreal_UnrealShaderDef_identifier "USH1"
#endif
#define iree_hal_unreal_UnrealShaderDef_type_hash ((flatbuffers_thash_t)0x41469a7)
#define iree_hal_unreal_UnrealShaderDef_type_identifier "\xa7\x69\x14\x04"
#ifndef iree_hal_unreal_UnrealShaderDef_file_extension
#define iree_hal_unreal_UnrealShaderDef_file_extension "ush1"
#endif
#ifndef iree_hal_unreal_ExecutableDef_file_identifier
#define iree_hal_unreal_ExecutableDef_file_identifier "USH1"
#endif
/* deprecated, use iree_hal_unreal_ExecutableDef_file_identifier */
#ifndef iree_hal_unreal_ExecutableDef_identifier
#define iree_hal_unreal_ExecutableDef_identifier "USH1"
#endif
#define iree_hal_unreal_ExecutableDef_type_hash ((flatbuffers_thash_t)0xade57ced)
#define iree_hal_unreal_ExecutableDef_type_identifier "\xed\x7c\xe5\xad"
#ifndef iree_hal_unreal_ExecutableDef_file_extension
#define iree_hal_unreal_ExecutableDef_file_extension "ush1"
#endif



struct iree_hal_unreal_UnrealShaderDef_table { uint8_t unused__; };

static inline size_t iree_hal_unreal_UnrealShaderDef_vec_len(iree_hal_unreal_UnrealShaderDef_vec_t vec)
__flatbuffers_vec_len(vec)
static inline iree_hal_unreal_UnrealShaderDef_table_t iree_hal_unreal_UnrealShaderDef_vec_at(iree_hal_unreal_UnrealShaderDef_vec_t vec, size_t i)
__flatbuffers_offset_vec_at(iree_hal_unreal_UnrealShaderDef_table_t, vec, i, 0)
__flatbuffers_table_as_root(iree_hal_unreal_UnrealShaderDef)

__flatbuffers_define_string_field(0, iree_hal_unreal_UnrealShaderDef, source_file_name, 0)
__flatbuffers_define_string_field(1, iree_hal_unreal_UnrealShaderDef, module_name, 0)
__flatbuffers_define_string_field(2, iree_hal_unreal_UnrealShaderDef, entry_point, 0)
__flatbuffers_define_table_field(3, iree_hal_unreal_UnrealShaderDef, debug_info, iree_hal_debug_ExportDef_table_t, 0)

struct iree_hal_unreal_ExecutableDef_table { uint8_t unused__; };

static inline size_t iree_hal_unreal_ExecutableDef_vec_len(iree_hal_unreal_ExecutableDef_vec_t vec)
__flatbuffers_vec_len(vec)
static inline iree_hal_unreal_ExecutableDef_table_t iree_hal_unreal_ExecutableDef_vec_at(iree_hal_unreal_ExecutableDef_vec_t vec, size_t i)
__flatbuffers_offset_vec_at(iree_hal_unreal_ExecutableDef_table_t, vec, i, 0)
__flatbuffers_table_as_root(iree_hal_unreal_ExecutableDef)

__flatbuffers_define_vector_field(0, iree_hal_unreal_ExecutableDef, unreal_shaders, iree_hal_unreal_UnrealShaderDef_vec_t, 0)
__flatbuffers_define_vector_field(1, iree_hal_unreal_ExecutableDef, source_files, iree_hal_debug_SourceFileDef_vec_t, 0)


#include "flatcc/flatcc_epilogue.h"
#endif /* UNREAL_EXECUTABLE_DEF_READER_H */
