#pragma once
#include <stdbool.h>
#include "urlsource_snapshot_v1.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct hs_data_driver hs_data_driver;
hs_data_driver *hs_data_driver_create(void);
void hs_data_driver_destroy(hs_data_driver *driver);
void hs_data_driver_configure(hs_data_driver *driver,const char *uuid,int age_ms);
// Nonblocking cached value. False means use the ordinary shader control.
bool hs_data_driver_read(hs_data_driver *driver,const char *uuid,int age_ms,float *value);
#ifdef HS_DATA_DRIVER_TEST
hs_data_driver *hs_data_driver_create_for_test(urlsource_snapshot_v1_fn reader);
#endif
#ifdef __cplusplus
}
#endif
