idf_component_register(
    SRCS "main.c" "audio_input.c" "audio_output.c" "http_client.c" "leds.c" "wifi.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_wifi esp_http_client nvs_flash esp_event led_strip
)
