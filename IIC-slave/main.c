#include <libpynq.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
  const uint32_t my_slave_address = 0x70;   // must match the master

  uint32_t my_register_map[32] = {1,2,3,4,5,6,7,8,9,10,11,12,13,
                                  14,15,16,17,18,19,20,21,22,23,24,
                                  25,26,27,28,29,30,31,32};
  const uint32_t my_register_map_length =
      sizeof(my_register_map) / sizeof(uint32_t);

  pynq_init();

  // The slave needs the switchbox setup too
  switchbox_set_pin(IO_PMODA3, SWB_IIC0_SCL);
  switchbox_set_pin(IO_PMODA4, SWB_IIC0_SDA);

  iic_init(IIC0);
  iic_reset(IIC0);
  iic_set_slave_mode(IIC0, my_slave_address,
                     &(my_register_map[0]), my_register_map_length);

  while (1) {
    iic_slave_mode_handler(IIC0);   // must run regularly
    sleep_msec(10);
  }

  iic_destroy(IIC0);
  pynq_destroy();
  return EXIT_SUCCESS;
}
