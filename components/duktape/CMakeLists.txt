idf_component_register(
    SRCS 
        "duktape.c"
        "duktape_esp32.c"
    INCLUDE_DIRS "."
    REQUIRES "esp_system" "esp_timer" "freertos"
)

# Duktape compile options for ESP32
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    DUK_USE_DATE_NOW_GETTIMEOFDAY
    DUK_USE_DATE_TZO_GMTIME_R
)

# Disable some warnings for Duktape and include sys/time.h for gettimeofday
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-sign-compare
    -Wno-unused-parameter
    -Wno-implicit-fallthrough
    -include sys/time.h
)
