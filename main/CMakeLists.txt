idf_component_register(SRCS "main.c" "usb_descriptors.c" "usb_os.c" "mbim_usb.c"
    INCLUDE_DIRS "."
    REQUIRES modem mbim esp_tinyusb esp_driver_uart esp_netif esp_event esp_wifi nvs_flash lwip)
