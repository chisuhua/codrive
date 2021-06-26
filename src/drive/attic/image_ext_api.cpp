#include "inc/pps.h"
#include "inc/pps_ext_image.h"
#include "utils/lang/error.h"
#include "utils/lang/string.h"

#undef HSA_API
#ifdef HSA_EXPORT_IMAGES
#define HSA_API HSA_API_EXPORT
#else
#define HSA_API HSA_API_IMPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/ 

status_t HSA_API hsa_ext_image_get_capability(
    device_t agent,
    hsa_ext_image_geometry_t geometry,
    const hsa_ext_image_format_t *image_format,
    uint32_t *capability_mask)
{
    utils::Error(utils::fmt("%s is not implemented!", "hsa_ext_image_get_capability"));
    return ERROR;
};

status_t HSA_API hsa_ext_image_get_capability_with_layout(
    device_t agent,
    hsa_ext_image_geometry_t geometry,
    const hsa_ext_image_format_t *image_format,
    hsa_ext_image_data_layout_t image_data_layout,
    uint32_t *capability_mask)
{
    utils::Error(utils::fmt("%s is not implemented!", "hsa_ext_image_get_capability"));
    return ERROR;
};

status_t HSA_API hsa_ext_image_data_get_info(
    device_t agent,
    const hsa_ext_image_descriptor_t *image_descriptor,
    hsa_access_permission_t access_permission,
    hsa_ext_image_data_info_t *image_data_info)
{
    utils::Error(utils::fmt("%s is not implemented!", "hsa_ext_image_get_capability"));
    return ERROR;
};
status_t HSA_API hsa_ext_image_data_get_info_with_layout(
    device_t agent,
    const hsa_ext_image_descriptor_t *image_descriptor,
    hsa_access_permission_t access_permission,
    hsa_ext_image_data_layout_t image_data_layout,
    size_t image_data_row_pitch,
    size_t image_data_slice_pitch,
    hsa_ext_image_data_info_t *image_data_info)
{
    utils::Error(utils::fmt("%s is not implemented!", "hsa_ext_image_get_capability"));
    return ERROR;
};
status_t HSA_API hsa_ext_image_create(
    device_t agent,
    const hsa_ext_image_descriptor_t *image_descriptor,
    const void *image_data,
    hsa_access_permission_t access_permission,
    hsa_ext_image_t *image)
{
    utils::Error(utils::fmt("%s is not implemented!", "hsa_ext_image_create"));
    return ERROR;
};

status_t HSA_API hsa_ext_image_create_with_layout(
    device_t agent,
    const hsa_ext_image_descriptor_t *image_descriptor,
    const void *image_data,
    hsa_access_permission_t access_permission,
    hsa_ext_image_data_layout_t image_data_layout,
    size_t image_data_row_pitch,
    size_t image_data_slice_pitch,
    hsa_ext_image_t *image)
{
    utils::Error(utils::fmt("%s is not implemented!", "hsa_ext_image_create"));
    return ERROR;
};

status_t HSA_API hsa_ext_image_destroy(
    device_t agent,
    hsa_ext_image_t image)
{
    utils::Error(utils::fmt("%s is not implemented!", "hsa_ext_image_create"));
    return ERROR;
};

status_t HSA_API hsa_ext_image_copy(
    device_t agent,
    hsa_ext_image_t src_image,
    const hsa_dim3_t* src_offset,
    hsa_ext_image_t dst_image,
    const hsa_dim3_t* dst_offset,
    const hsa_dim3_t* range)
{
    utils::Error(utils::fmt("%s is not implemented!", "hsa_ext_image_create"));
    return ERROR;
};

status_t HSA_API hsa_ext_image_import(
    device_t agent,
    const void *src_memory,
    size_t src_row_pitch,
    size_t src_slice_pitch,
    hsa_ext_image_t dst_image,
    const hsa_ext_image_region_t *image_region)
{
    utils::Error(utils::fmt("%s is not implemented!", "hsa_ext_image_create"));
    return ERROR;
};

status_t HSA_API hsa_ext_image_export(
    device_t agent,
    hsa_ext_image_t src_image,
    void *dst_memory,
    size_t dst_row_pitch,
    size_t dst_slice_pitch,
    const hsa_ext_image_region_t *image_region)
{
    utils::Error(utils::fmt("%s is not implemented!", "hsa_ext_image_create"));
    return ERROR;
};

status_t HSA_API hsa_ext_image_clear(
    device_t agent,
    hsa_ext_image_t image,
    const void* data,
    const hsa_ext_image_region_t *image_region)
{
    utils::Error(utils::fmt("%s is not implemented!", "hsa_ext_image_create"));
    return ERROR;
};

status_t HSA_API hsa_ext_sampler_create(
    device_t agent,
    const hsa_ext_sampler_descriptor_t *sampler_descriptor,
    hsa_ext_sampler_t *sampler)
{
    utils::Error(utils::fmt("%s is not implemented!", "hsa_ext_image_create"));
    return ERROR;
};

status_t HSA_API hsa_ext_sampler_destroy(
    device_t agent,
    hsa_ext_sampler_t sampler)
{
    utils::Error(utils::fmt("%s is not implemented!", "hsa_ext_image_create"));
    return ERROR;
};

    
#ifdef __cplusplus
}  // end extern "C" block
#endif /*__cplusplus*/ 

