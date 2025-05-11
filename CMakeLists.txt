if ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_SENSOR_PRINTER app PRIVATE src/behaviors/behavior_sensor_printer.c)

  zephyr_include_directories(include)
endif()
