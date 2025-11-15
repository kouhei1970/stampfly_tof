# Collect all VL53LX driver source files
file(GLOB VL53LX_SRCS "src/vl53lx/*.c")

idf_component_register(
    SRCS "src/vl53lx_platform.c" ${VL53LX_SRCS}
    INCLUDE_DIRS "include/vl53lx" "include"
    REQUIRES driver
)
