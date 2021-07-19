/**
 * @file: crc.h
 * @brief: crc calculation functions for PNG file
 */

#include "lab_png.h"
#pragma once

void make_crc_table(void);
unsigned long update_crc(unsigned long crc, unsigned char *buf, int len);
unsigned long crc(unsigned char *buf, int len);
struct chunk * retrieve_chunk(char *fp);
