#include "app_config.h"
#include "cfs.h"
#include "cfs-fat.h"
#include "watchdog.h"
#include "config_mapping.h"
#include "sd_mount.h"
#include "logger.h"
#include "uart_handler.h"
#include "microSD.h"

#ifdef APP_CONFIG_DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

#define CONF_FILE_TIMESTAMP_CHECK 0

// this sequence is used to verify the storage location
#define CHECK_SEQUENCE  0x4242

// configuration instance with default values
app_config_t system_config = {
  {0}
};
bool cfg_load_ok;

#ifdef APP_CONF_STORE_EEPROM
app_config_t ee_system_config EEMEM;
#endif

char app_config_buffer[MAX_FILE_SIZE];
/*----------------------------------------------------------------------------*/
static int8_t
app_config_load_internal()
{
#if APP_CONF_STORE_EEPROM
  log_i("Loading config from EEPROM...\n");
  eeprom_read_block(&system_config, &ee_system_config, sizeof (system_config));
  // check if data is valid
  if (system_config._check_sequence != CHECK_SEQUENCE) {
    log_e("Loading config from EEPROM failed.\n");
    return -1;
  }
  return 0;
#endif
}
/*----------------------------------------------------------------------------*/
/**
 * Stores config data to EEPROM.
 */
static void
app_config_save_internal()
{
#if APP_CONF_STORE_EEPROM
  log_i("Saving config to EEPROM...\n");
  system_config._check_sequence = CHECK_SEQUENCE;
  eeprom_update_block(&system_config, &ee_system_config, sizeof (system_config));
#endif
}
/*----------------------------------------------------------------------------*/
static int8_t
app_config_load_microSD()
{
  int fd;

  // And open it
  fd = cfs_open("inga.cfg", CFS_READ);

  // In case something goes wrong, we cannot save this file
  if (fd == -1) {
    log_e("Failed opening config file\n");
    return -1;
  }


  // do nothing if modification timestamp of file is older/equal than stored
#if CONF_FILE_TIMESTAMP_CHECK
  log_i("Checking config file timestamp...\n");
  if ((cfs_fat_get_last_date(fd) <= system_config._mod_date)
          && (cfs_fat_get_last_time(fd) <= system_config._mod_time)) {
    log_i("Config file is older than stored config, will not be loaded\n");
    cfs_close(fd);
    return -1;
  }
#endif

  // update modification timestamps
  system_config._mod_date = cfs_fat_get_last_date(fd);
  system_config._mod_time = cfs_fat_get_last_time(fd);


  int size = cfs_read(fd, app_config_buffer, MAX_FILE_SIZE);
  app_config_buffer[size] = '\0'; // string null terminator
  log_v("actually read: %d\n", size);
  log_i("Loaded data from microSD card\n");

  process_post(&config_process, event_config_update, NULL);

  cfs_close(fd);

  // needed to unblock SPI at INGA V1.4
  microSD_switchoff();

  return 0;
}
/*----------------------------------------------------------------------------*/
/**
 * Loads default config data as fallback.
 */
static void
app_config_load_defaults()
{
  system_config._check_sequence = CHECK_SEQUENCE;
  // node defaults
  system_config.node.id = 42;
  // output defaults
  system_config.output.sdcard = false;
  system_config.output.usb = false;
  system_config.output.radio = false;
  system_config.output.block_size = 0;
  // accelerometer defaults
  system_config.acc.enabled = true;
  system_config.acc.rate = 0;
  system_config.acc.g_range = 0;
  // gyroscope defaults
  system_config.gyro.enabled = true;
  system_config.gyro.rate = 0;
  system_config.gyro.dps = 0;
  // pressure defaults
  system_config.pressure.enabled = true;
  system_config.pressure.rate = 0;
  // temperature defaults
  system_config.temp.enabled = true;
  system_config.temp.rate = 0;
}

process_event_t event_config_update;
/*----------------------------------------------------------------------------*/
PROCESS(config_process, "Config process");
/*----------------------------------------------------------------------------*/
PROCESS_THREAD(config_process, ev, data)
{
  PROCESS_BEGIN();
  log_v("config_process: started\n");

  event_config_update = process_alloc_event();

  while (1) {

    PROCESS_WAIT_EVENT_UNTIL(ev == event_config_update);

    // reconfigure
    parse_ini(app_config_buffer, &inga_conf_file);
    app_config_save_internal();

    print_config();

    init_sensors();
  }

  PROCESS_END();
}
/*----------------------------------------------------------------------------*/
int8_t
app_config_init(bool sd_mounted)
{
  // initialize struct to zero
  memset(&system_config, 0, sizeof (app_config_t));

  if (app_config_load_internal() == 0) {
    log_i("Loaded config from internal data\n");
  } else {
    log_w("Loading from internal data failed\n");
    app_config_load_defaults();
    log_i("Defaults loaded\n");
  }

  if ((!sd_mounted) || (app_config_load_microSD() != 0)) {
    process_post(&config_process, event_config_update, NULL);
  }

  return 0;
}

/*----------------------------------------------------------------------------*/
#ifdef APP_CONFIG_DEBUG
void
print_config()
{
  log_v("\n\nConfig: \n");
  log_v("_check_sequence:  %d\n", system_config._check_sequence);
  log_v("output.sdcard:    %d\n", system_config.output.sdcard);
  log_v("output.usb:       %d\n", system_config.output.usb);
  log_v("output.radio:     %d\n", system_config.output.radio);
  log_v("output.block_size:%d\n", system_config.output.block_size);
  log_v("acc.enabled:      %d\n", system_config.acc.enabled);
  log_v("acc.rate:         %d\n", system_config.acc.rate);
  log_v("acc.g_range:      %d\n", system_config.acc.g_range);
  log_v("gyro.enabled:     %d\n", system_config.gyro.enabled);
  log_v("gyro.rate:        %d\n", system_config.gyro.rate);
  log_v("gyro.dps:         %d\n", system_config.gyro.dps);
  log_v("pressure.enabled: %d\n", system_config.pressure.enabled);
  log_v("pressure.rate:    %d\n", system_config.pressure.rate);
  log_v("temp.enabled:     %d\n", system_config.temp.enabled);
  log_v("temp.rate:        %d\n", system_config.temp.rate);
}
#endif
/*----------------------------------------------------------------------------*/
