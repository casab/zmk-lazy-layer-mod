# Lazy layer-mod behavior for ZMK.
if (CONFIG_ZMK_BEHAVIOR_LAZY_LAYER_MOD)
  zephyr_library()
  zephyr_library_sources(src/behavior_lazy_layer_mod.c)
  zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
endif()
