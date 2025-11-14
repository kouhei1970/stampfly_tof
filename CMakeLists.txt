idf_component_register(
    SRCS
         # Main driver (will be rewritten to use VL53LX API)
         "src/stampfly_tof.c"

         # VL53LX Platform Layer (ESP-IDF I2C implementation)
         "src/vl53lx/platform/vl53lx_platform.c"

         # VL53LX Core Driver (Official STMicroelectronics code)
         "src/vl53lx/core/vl53lx_api.c"
         "src/vl53lx/core/vl53lx_api_calibration.c"
         "src/vl53lx/core/vl53lx_api_core.c"
         "src/vl53lx/core/vl53lx_api_debug.c"
         "src/vl53lx/core/vl53lx_api_preset_modes.c"
         "src/vl53lx/core/vl53lx_core.c"
         "src/vl53lx/core/vl53lx_core_support.c"
         "src/vl53lx/core/vl53lx_dmax.c"
         "src/vl53lx/core/vl53lx_hist_algos_gen3.c"
         "src/vl53lx/core/vl53lx_hist_algos_gen4.c"
         "src/vl53lx/core/vl53lx_hist_char.c"
         "src/vl53lx/core/vl53lx_hist_core.c"
         "src/vl53lx/core/vl53lx_hist_funcs.c"
         "src/vl53lx/core/vl53lx_nvm.c"
         "src/vl53lx/core/vl53lx_nvm_debug.c"
         "src/vl53lx/core/vl53lx_register_funcs.c"
         "src/vl53lx/core/vl53lx_sigma_estimate.c"
         "src/vl53lx/core/vl53lx_silicon_core.c"
         "src/vl53lx/core/vl53lx_wait.c"
         "src/vl53lx/core/vl53lx_xtalk.c"

    INCLUDE_DIRS
         "include"
         "include/vl53lx/core"
         "include/vl53lx/platform"

    REQUIRES driver
)
