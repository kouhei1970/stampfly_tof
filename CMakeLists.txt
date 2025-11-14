idf_component_register(
    SRCS "src/vl53l3cx.c"
         "src/stampfly_tof.c"
    INCLUDE_DIRS "include"
    REQUIRES driver
)
