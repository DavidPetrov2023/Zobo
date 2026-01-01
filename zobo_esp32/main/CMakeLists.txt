idf_component_register(SRCS "main.c"
                            "motor.c"
                            "led.c"
                            "ble_service.c"
                            "wifi_manager.c"
                            "ota_manager.c"
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "certs/github_ca.pem")
