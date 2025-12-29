idf_component_register(
  SRCS
    "src/BluetoothA2DPCommon.cpp"
    "src/BluetoothA2DPSource.cpp"
  INCLUDE_DIRS src
  REQUIRES bt esp_common freertos hal log nvs_flash
)

# We only use A2DP Source with our own PCM callback (no I2S output).
target_compile_options(${COMPONENT_LIB} PUBLIC
  -DA2DP_LEGACY_I2S_SUPPORT=0
  -DA2DP_I2S_AUDIOTOOLS=0
  -DESP32_CMAKE=1
  -Wno-error
  -Wno-format
  -fpermissive
)


