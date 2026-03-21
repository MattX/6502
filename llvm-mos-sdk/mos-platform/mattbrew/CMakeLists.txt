platform(mattbrew COMPLETE HOSTED PARENT common)

if(NOT CMAKE_CROSSCOMPILING)
  return()
endif()

install(FILES
  mattbrew.h
TYPE INCLUDE)
install(FILES link.ld TYPE LIB)

add_platform_library(mattbrew-crt0
  crt0/reset.S
  crt0/systick.S
)
merge_libraries(mattbrew-crt0
  common-crt0
  common-init-stack
  common-copy-data
  common-zero-bss
  common-exit-loop
)

add_platform_library(mattbrew-c
  delay.c
  getchar.c
  io.c
  lcd.c
  putchar.c
)

target_compile_options(mattbrew-crt0 PUBLIC -mcpu=mosw65c02)
target_link_libraries(mattbrew-crt0 PRIVATE common-asminc)

target_include_directories(mattbrew-c BEFORE PUBLIC .)
target_compile_options(mattbrew-c PUBLIC -mcpu=mosw65c02)
target_link_libraries(mattbrew-c PRIVATE common-asminc)
